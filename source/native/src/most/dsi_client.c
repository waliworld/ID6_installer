/* Client for DSYSMOSTServer, one per process.
 *
 * Callbacks arrive on the DSI thread; sends and register calls may come from any thread. g_mu guards the state
 * they read (client id, device ids, target up, running). g_send_mu serializes every use of a stream, because
 * the library's own thread safety is unknown. Handlers run with no lock held, so a handler may send. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "dsi_abi.h"
#include "dsi_client.h"

#define MAX_PAIRS_KEPT 512
#define MAX_PAIRS_SEEN 4096
#define MAX_CONNECT_KEPT 16
#define MAX_MSG_LEN 0x1000

#ifdef MOST_HOST
/* Host pointers are 8 bytes and would overlap at the 4-byte ARM offsets, so the fake keeps the callbacks. */
void fake_dsi_set_cb(void *app, unsigned off, void *fn);
#define SET_CB(app, off, fn) fake_dsi_set_cb((app), (off), (void *)(fn))
#else
#define SET_CB(app, off, fn) do { void *f_ = (void *)(fn); memcpy((char *)(app) + (off), &f_, sizeof f_); } while (0)
#endif

static uint32_t g_app_mem[(DSI_APP_BYTES + 3) / 4] __attribute__((aligned(8)));
static void *const g_app = g_app_mem;

static pthread_mutex_t g_mu, g_send_mu;
static int g_init;
static int g_running;                 /* g_app is initialized and not yet freed */
static dsi_handlers_t g_h;
static void *g_u;
static int g_have_client, g_term_up;
static uint16_t g_key = 0x06F0;       /* FBlock 0x06, instance 0xF0 */
static uint8_t g_fb = 0x06, g_inst = 0xF0;
static int g_loglevel;
static uint32_t g_client;
static uint16_t g_device, g_term_dev;
static char g_name[32] = "boostdeck";   /* request 0x0F */

void dsi_client_set_name(const char *name)
{
    size_t n = strlen(name);
    if (n >= sizeof g_name)
        n = sizeof g_name - 1;
    memcpy(g_name, name, n);
    g_name[n] = 0;
}

void most_log_level(int level) { g_loglevel = level; }

static void most_log(int level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void most_log(int level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    if (level > g_loglevel)
        return;
    fprintf(stderr, "[%s] ", tag);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}


static void state_init_once(void)
{
    if (!g_init) {
        pthread_mutex_init(&g_mu, NULL);
        pthread_mutex_init(&g_send_mu, NULL);
        g_init = 1;
    }
}

void dsi_client_set_target(uint16_t key, uint8_t fblock, uint8_t inst)
{
    state_init_once();
    pthread_mutex_lock(&g_mu);
    g_key = key;
    g_fb = fblock;
    g_inst = inst;
    pthread_mutex_unlock(&g_mu);
}

static int stream_failed(const void *stream)
{
    int e;
    memcpy(&e, (const char *)stream + DSI_STREAM_ERROR, sizeof e);
    return e != 0;
}

static int w32(void *s, uint32_t v) { return most_dsi.Write(s, &v, 4, 4); }
static int w16(void *s, uint16_t v) { return most_dsi.Write(s, &v, 2, 2); }
static int w8(void *s, uint8_t v) { return most_dsi.Write(s, &v, 1, 1); }

/* Send the open stream as request id, then close it. Caller holds g_send_mu, so if g_running reads 1 here,
   DSIClientFree can't run until this send is done: dsi_client_run clears it, then takes g_send_mu to free.
   get_state's own check can't promise that, because the loop can end between it and g_send_mu. */
static int finish(void *stream, uint32_t id, uint32_t session)
{
    int rc = -1, running;
    pthread_mutex_lock(&g_mu);
    running = g_running;
    pthread_mutex_unlock(&g_mu);
    if (!running) {
        most_log(MOST_LOG_W, "dsi", "request 0x%02x: the client is gone", (unsigned)id);
    } else if (stream_failed(stream)) {
        most_log(MOST_LOG_W, "dsi", "request 0x%02x: stream write failed", (unsigned)id);
    } else {
        int r = most_dsi.ClientSendRequest(g_app, id, session, stream);   /* -1 on failure, else the session id */
        if (r == -1)
            most_log(MOST_LOG_W, "dsi", "request 0x%02x: DSIClientSendRequest failed", (unsigned)id);
        else {
            most_log(MOST_LOG_T, "dsi", "request 0x%02x -> %d", (unsigned)id, r);
            rc = 0;
        }
    }
    most_dsi.Close(stream);
    return rc;
}

static int get_state(uint32_t *client, uint16_t *device, int need_terminal, uint16_t *term)
{
    int ok;
    pthread_mutex_lock(&g_mu);
    ok = g_running && g_have_client && (!need_terminal || g_term_up);
    if (ok) {
        *client = g_client;
        *device = g_device;
        if (term) *term = g_term_dev;
    }
    pthread_mutex_unlock(&g_mu);
    return ok ? 0 : -1;
}

static int key_request(uint32_t id)
{
    uint32_t stream[DSI_STREAM_BYTES / 4] __attribute__((aligned(8)));
    uint32_t client;
    uint16_t dev, key;
    int rc;
    if (get_state(&client, &dev, 0, NULL) != 0)
        return -1;
    pthread_mutex_lock(&g_mu);
    key = g_key;
    pthread_mutex_unlock(&g_mu);
    pthread_mutex_lock(&g_send_mu);
    most_dsi.Open(stream);
    w32(stream, client);
    w32(stream, 1);
    w16(stream, key);
    rc = finish(stream, id, most_dsi.CreateId());
    pthread_mutex_unlock(&g_send_mu);
    return rc;
}

int dsi_register_target(void) { return key_request(0x01); }
int dsi_unregister_target(void) { return key_request(0x05); }

int dsi_unregister_client(void)
{
    uint32_t stream[DSI_STREAM_BYTES / 4] __attribute__((aligned(8)));
    uint32_t client;
    uint16_t dev;
    int rc;
    if (get_state(&client, &dev, 0, NULL) != 0)   /* nothing to unregister until responseMOSTClient */
        return -1;
    pthread_mutex_lock(&g_send_mu);
    most_dsi.Open(stream);
    rc = finish(stream, 0x10, most_dsi.CreateId());
    pthread_mutex_unlock(&g_send_mu);
    return rc;
}

int dsi_send_fn(uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const uint8_t *payload, size_t n)
{
    uint32_t stream[DSI_STREAM_BYTES / 4] __attribute__((aligned(8)));
    uint32_t client;
    uint16_t src, tgt;
    int rc;
    if (n > MAX_MSG_LEN)
        return -1;
    if (get_state(&client, &src, 1, &tgt) != 0)
        return -1;
    pthread_mutex_lock(&g_send_mu);
    most_dsi.Open(stream);
    w32(stream, client);
    w32(stream, 0);                    /* transport: control channel */
    w32(stream, 1);                    /* kind: message */
    w32(stream, 0);                    /* flags */
    w16(stream, tgt);
    w16(stream, src);
    w8(stream, fblock);
    w8(stream, inst);
    w16(stream, fkt);
    w8(stream, op);                    /* OpType */
    w32(stream, client & 0xFFFFu);
    w32(stream, (uint32_t)n);
    if (n)
        most_dsi.Write(stream, payload, (int)n, 1);
    rc = finish(stream, 0x02, most_dsi.CreateId());
    pthread_mutex_unlock(&g_send_mu);
    return rc;
}

/* Callbacks, on the DSI thread. */

static void on_connected(void *app)
{
    static const uint32_t ids[12] = { 0x80000000u, 0x80000001u, 0x80000002u, 0x80000003u, 0x80000004u,
        0x80000005u, 0x80000006u, 0x80000007u, 0xC0000000u, 0xC0000001u, 0xC0000002u, 0xC0000004u };
    static const uint32_t attrs[4] = { 0xC0000002u, 0xC0000004u, 0xC0000001u, 0xC0000000u };
    uint32_t stream[DSI_STREAM_BYTES / 4] __attribute__((aligned(8)));
    const char *name = g_name;
    uint32_t session = most_dsi.CreateId();
    int i, r;
    (void)app;
    most_log(MOST_LOG_I, "dsi", "connected");
    pthread_mutex_lock(&g_send_mu);
    r = most_dsi.ClientSendRegister(g_app, ids, 12, session);
    if (r < 0)
        most_log(MOST_LOG_W, "dsi", "DSIClientSendRegister failed: %d", r);
    most_dsi.Open(stream);
    most_dsi.WriteString2(stream, &name);
    finish(stream, 0x0F, session);
    for (i = 0; i < 4; i++) {
        r = most_dsi.ClientSetNotification(g_app, attrs[i]);
        if (r < 0)
            most_log(MOST_LOG_W, "dsi", "most_dsi.ClientSetNotification(0x%08x) failed: %d", (unsigned)attrs[i], r);
    }
    pthread_mutex_unlock(&g_send_mu);
    if (g_h.connected) g_h.connected(g_u);
}

static void on_disconnected(void *app)
{
    (void)app;
    most_log(MOST_LOG_W, "dsi", "disconnected");
    pthread_mutex_lock(&g_mu);
    g_have_client = 0;
    g_term_up = 0;
    pthread_mutex_unlock(&g_mu);
    if (g_h.disconnected) g_h.disconnected(g_u);
}

static void on_exit_loop(void *app)
{
    (void)app;
    most_log(MOST_LOG_I, "dsi", "loop exit");
}

#define RD(v, n) (most_dsi.Read(stream, &(v), (n), (n)), !stream_failed(stream))

static void read_message(void *stream)
{
    uint32_t transport = 0, kind = 0, flags = 0, client = 0, len = 0;
    uint16_t target = 0, source = 0, fkt = 0;
    uint8_t fb = 0, inst = 0, op = 0;
    static uint8_t buf[MAX_MSG_LEN];   /* static: the DSI thread's stack is small; callbacks are serialized */
    char hexs[2 * 16 + 1];
    unsigned i;
    int match;
    if (!RD(transport, 4) || !RD(kind, 4) ||
        ((kind == 1 || kind == 2) && !RD(flags, 4)) ||
        !RD(target, 2) || !RD(source, 2) || !RD(fb, 1) || !RD(inst, 1) || !RD(fkt, 2) || !RD(op, 1) ||
        !RD(client, 4) || !RD(len, 4)) {
        most_log(MOST_LOG_W, "dsi.rx", "record unreadable: kind=%u fkt=%03x", (unsigned)kind, fkt);
        return;
    }
    if (len > MAX_MSG_LEN) {
        most_log(MOST_LOG_W, "dsi", "message length 0x%x rejected", (unsigned)len);
        return;
    }
    if (len) {
        most_dsi.Read(stream, buf, (int)len, 1);
        if (stream_failed(stream)) {
            most_log(MOST_LOG_W, "dsi", "message payload read failed");
            return;
        }
    }
    for (i = 0; i < len && i < 16; i++)
        snprintf(hexs + 2 * i, 3, "%02x", buf[i]);
    hexs[2 * i] = 0;
    /* A reply to 22 F190 (62 F1 90 and the VIN) is never logged: its bytes are left out, its length stays. */
    for (i = 0; i + 3 <= len; i++)
        if (buf[i] == 0x62 && buf[i + 1] == 0xF1 && buf[i + 2] == 0x90) {
            strcpy(hexs, "(F190 reply, not shown)");
            break;
        }
    /* Every record at info level, so a trace shows whether anything arrived at all. */
    most_log(MOST_LOG_I, "dsi.rx", "kind=%u tr=%u flags=0x%x tgt=%04x src=%04x %02x.%02x fkt=%03x op=%x client=%x len=%u p=%s",
          (unsigned)kind, (unsigned)transport, (unsigned)flags, target, source, fb, inst, fkt, op,
          (unsigned)client, (unsigned)len, hexs);
    /* Inbound records are kind 2. Flags 4..9 (transmission status) and 0x13..0x15 (segment skipped) are not
       reports. */
    if (kind == 2 && ((flags >= 4 && flags <= 9) || (flags >= 0x13 && flags <= 0x15))) {
        most_log(MOST_LOG_W, "dsi.status", "value=0x%x fkt=%03x", (unsigned)flags, fkt);
        return;
    }
    pthread_mutex_lock(&g_mu);
    match = fb == g_fb && inst == g_inst;
    pthread_mutex_unlock(&g_mu);
    if ((kind == 1 || kind == 2) && match && g_h.report)
        g_h.report(g_u, fkt, op, buf, len);
}

static void read_connect(void *stream)
{
    uint32_t n, i;
    uint16_t dev, key, want;
    if (!RD(n, 4))
        return;
    if (n > MAX_CONNECT_KEPT) n = MAX_CONNECT_KEPT;
    pthread_mutex_lock(&g_mu);
    want = g_key;
    pthread_mutex_unlock(&g_mu);
    for (i = 0; i < n; i++) {
        if (!RD(dev, 2) || !RD(key, 2))
            return;
        if (key == want) {
            pthread_mutex_lock(&g_mu);
            g_term_up = 1;
            g_term_dev = dev;
            pthread_mutex_unlock(&g_mu);
            most_log(MOST_LOG_I, "dsi", "target up, device 0x%04x", dev);
            if (g_h.target_up) g_h.target_up(g_u, dev);
            return;
        }
    }
}

static void read_disconnect(void *stream)
{
    uint32_t n, i;
    uint16_t key, want;
    if (!RD(n, 4))
        return;
    if (n > MAX_CONNECT_KEPT) n = MAX_CONNECT_KEPT;
    pthread_mutex_lock(&g_mu);
    want = g_key;
    pthread_mutex_unlock(&g_mu);
    for (i = 0; i < n; i++) {
        if (!RD(key, 2))
            return;
        if (key == want) {
            pthread_mutex_lock(&g_mu);
            g_term_up = 0;
            pthread_mutex_unlock(&g_mu);
            most_log(MOST_LOG_I, "dsi", "target down");
            if (g_h.target_down) g_h.target_down(g_u);
            return;
        }
    }
}

static void read_registry(void *stream)
{
    uint32_t n, i;
    static uint16_t pairs[MAX_PAIRS_KEPT * 2];
    if (!RD(n, 4))
        return;
    if (n > MAX_PAIRS_SEEN) {
        most_log(MOST_LOG_W, "dsi", "registry count %u rejected", (unsigned)n);
        return;
    }
    if (n > MAX_PAIRS_KEPT) n = MAX_PAIRS_KEPT;
    for (i = 0; i < n; i++)
        if (!RD(pairs[2 * i], 2) || !RD(pairs[2 * i + 1], 2))
            return;
    if (g_h.registry) g_h.registry(g_u, pairs, (int)n);
}

static void on_response(void *app, void *stream, const void *event)
{
    uint32_t type, id;
    (void)app;
    memcpy(&type, (const char *)event + DSI_EVENT_TYPE, 4);
    memcpy(&id, (const char *)event + DSI_EVENT_ID, 4);
    if (type == 0x201 || type == 0x204 || type == 0x205) {
        most_log(MOST_LOG_W, "dsi", "%s id=0x%08x", type == 0x201 ? "response invalid" : "request rejected", (unsigned)id);
        return;
    }
    switch (id) {
    case 0x80000000u: {
        uint32_t client;
        uint16_t dev;
        if (!RD(client, 4) || !RD(dev, 2))
            return;
        pthread_mutex_lock(&g_mu);
        g_client = client;
        g_device = dev;
        g_have_client = 1;
        pthread_mutex_unlock(&g_mu);
        most_log(MOST_LOG_I, "dsi", "client 0x%08x device 0x%04x", (unsigned)client, dev);
        if (g_h.most_client) g_h.most_client(g_u, client, dev);
        break;
    }
    case 0x80000001u: read_message(stream); break;
    case 0x80000003u: read_connect(stream); break;
    case 0x80000004u: read_disconnect(stream); break;
    case 0xC0000001u: read_registry(stream); break;
    default:
        most_log(MOST_LOG_D, "dsi", "event 0x%08x ignored", (unsigned)id);
    }
}

/* Lifecycle. */

int dsi_client_run(const char *role, const dsi_handlers_t *h, void *u)
{
    int rc;
    state_init_once();
    pthread_mutex_lock(&g_mu);
    g_h = *h;
    g_u = u;
    g_have_client = g_term_up = 0;
    pthread_mutex_unlock(&g_mu);

    most_dsi.Init();
    rc = most_dsi.ClientInit(g_app, "DSYSMOSTServer", role, 4, 5);
    if (rc != 0) {
        most_log(MOST_LOG_E, "dsi", "DSIClientInit(%s) failed: %d", role, rc);
        return -1;
    }
    SET_CB(g_app, DSI_APP_ON_CONNECTED, on_connected);
    SET_CB(g_app, DSI_APP_ON_DISCONNECTED, on_disconnected);
    SET_CB(g_app, DSI_APP_ON_RESPONSE, on_response);
    SET_CB(g_app, DSI_APP_ON_EXIT_LOOP, on_exit_loop);
    pthread_mutex_lock(&g_mu);
    g_running = 1;
    pthread_mutex_unlock(&g_mu);

    rc = most_dsi.ClientRun(g_app);
    if (rc != 0)
        most_log(MOST_LOG_E, "dsi", "DSIClientRun(%s) returned %d", role, rc);

    pthread_mutex_lock(&g_mu);
    g_running = 0;
    g_have_client = g_term_up = 0;
    pthread_mutex_unlock(&g_mu);
    pthread_mutex_lock(&g_send_mu);        /* wait out any send still using the stream */
    most_dsi.ClientFree(g_app);
    pthread_mutex_unlock(&g_send_mu);
    return rc;
}

void dsi_client_stop(void)
{
    int running;
    if (!g_init)
        return;
    pthread_mutex_lock(&g_mu);
    running = g_running;
    pthread_mutex_unlock(&g_mu);
    if (running)
        most_dsi.ClientStop(g_app, 0);
}
