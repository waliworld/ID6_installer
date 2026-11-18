/* The DSI functions on the host. Callbacks are kept in a side table because the ARM offsets
 * (0xCC..0xDC, 4-byte pointers) would overlap with 8-byte host pointers. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include "most/dsi_abi.h"
#include "fake_dsi.h"

fake_req_t fake_req[FAKE_MAX_REQS];
int fake_nreq;
uint32_t fake_reg_ids[16];
int fake_reg_n;
uint32_t fake_reg_session;
uint32_t fake_notify[8];
int fake_nnotify;
char fake_role[64];
int fake_major, fake_minor;
int fake_stop_calls, fake_free_calls, fake_opens, fake_closes;
int fake_fail_write_at;
int fake_cb_off_ok = 1;
int fake_send_rc, fake_run_rc, fake_reg_rc, fake_notify_rc;
unsigned fake_cb_offs[8];
int fake_ncb;
int fake_client_inits;
int fake_stop_nreq = -1;
int fake_run_blocks_stops, fake_run_blocks_alarm;

static void *g_cb[4];                       /* connected, disconnected, response, exit */
static uint8_t g_app_blk[DSI_APP_BYTES];
static void (*g_hook)(void *);
static void (*g_send_hook)(const fake_req_t *);
static uint32_t g_next_id = 100;
static fake_write_t g_w[FAKE_MAX_WRITES];
static int g_nw;
static uint8_t g_rd[0x2000];
static size_t g_rd_n, g_rd_pos;
static uint32_t g_event[4];

void fake_dsi_reset(void)
{
    fake_nreq = fake_reg_n = fake_nnotify = 0;
    fake_reg_session = 0;
    fake_role[0] = 0;
    fake_stop_calls = fake_free_calls = fake_opens = fake_closes = 0;
    fake_fail_write_at = 0;
    fake_cb_off_ok = 1;
    fake_send_rc = fake_run_rc = fake_reg_rc = fake_notify_rc = 0;
    fake_ncb = 0;
    fake_client_inits = 0;
    fake_stop_nreq = -1;
    memset(g_cb, 0, sizeof g_cb);
    g_hook = NULL;
    g_send_hook = NULL;
    g_nw = 0;
    g_rd_n = g_rd_pos = 0;
    g_next_id = 100;
}

void fake_dsi_set_run_hook(void (*hook)(void *app)) { g_hook = hook; }
void fake_dsi_set_send_hook(void (*hook)(const fake_req_t *r)) { g_send_hook = hook; }

/* Other threads poll this while the link sends. The acquire pairs with the send's release, so every entry below the
   count it reads is whole. */
int fake_dsi_req_count(uint32_t id)
{
    int i, n = 0, total = __atomic_load_n(&fake_nreq, __ATOMIC_ACQUIRE);
    for (i = 0; i < total; i++)
        if (fake_req[i].id == id)
            n++;
    return n;
}

void fake_dsi_set_cb(void *app, unsigned off, void *fn)
{
    (void)app;
    if (fake_ncb < 8) fake_cb_offs[fake_ncb++] = off;
    switch (off) {
    case DSI_APP_ON_CONNECTED:    g_cb[0] = fn; break;
    case DSI_APP_ON_DISCONNECTED: g_cb[1] = fn; break;
    case DSI_APP_ON_RESPONSE:     g_cb[2] = fn; break;
    case DSI_APP_ON_EXIT_LOOP:    g_cb[3] = fn; break;
    default: fake_cb_off_ok = 0;
    }
}

void fake_dsi_connect(void) { if (g_cb[0]) ((void (*)(void *))g_cb[0])(g_app_blk); }
void fake_dsi_disconnect(void) { if (g_cb[1]) ((void (*)(void *))g_cb[1])(g_app_blk); }

void fake_dsi_deliver(uint32_t type, uint32_t id, const void *payload, size_t n)
{
    uint8_t stream[DSI_STREAM_BYTES];
    memset(stream, 0, sizeof stream);
    if (n > sizeof g_rd) n = sizeof g_rd;
    if (n) memcpy(g_rd, payload, n);
    g_rd_n = n;
    g_rd_pos = 0;
    g_event[0] = 0; g_event[1] = type; g_event[2] = id; g_event[3] = 0;
    if (g_cb[2])
        ((void (*)(void *, void *, const void *))g_cb[2])(g_app_blk, stream, g_event);
}

void DSIInit(void) {}

int DSIClientInit(void *app, const char *ifname, const char *role, int major, int minor)
{
    (void)ifname;
    fake_client_inits++;
    memset(app, 0, DSI_APP_BYTES);
    strncpy(fake_role, role, sizeof fake_role - 1);
    fake_major = major;
    fake_minor = minor;
    return 0;
}

int DSIClientRun(void *app)
{
    sigset_t cur;

    pthread_sigmask(SIG_BLOCK, NULL, &cur);   /* NULL set: only reads this thread's mask */
    fake_run_blocks_stops = sigismember(&cur, SIGINT) && sigismember(&cur, SIGTERM) && sigismember(&cur, SIGHUP);
    fake_run_blocks_alarm = sigismember(&cur, SIGALRM);
    if (g_hook) g_hook(app);
    return fake_run_rc;
}
int DSIClientStop(void *app, int unused)
{
    (void)app; (void)unused;
    if (fake_stop_nreq < 0)            /* before the count: the run hook parks until it sees a stop */
        fake_stop_nreq = __atomic_load_n(&fake_nreq, __ATOMIC_ACQUIRE);
    __atomic_add_fetch(&fake_stop_calls, 1, __ATOMIC_RELEASE);   /* the DSI thread polls it: fake_dme_run_hook */
    return 0;
}
void DSIClientFree(void *app) { (void)app; fake_free_calls++; }

int DSIClientSendRegister(void *app, const uint32_t *ids, int n, uint32_t session)
{
    (void)app;
    if (n > 16) n = 16;
    memcpy(fake_reg_ids, ids, (size_t)n * 4);
    fake_reg_n = n;
    fake_reg_session = session;
    return fake_reg_rc;
}

int DSIClientSetNotification(void *app, uint32_t attr)
{
    (void)app;
    if (fake_nnotify < 8) fake_notify[fake_nnotify++] = attr;
    return fake_notify_rc;
}

int DSIClientSendRequest(void *app, uint32_t id, uint32_t session, void *stream)
{
    fake_req_t *r;
    (void)app; (void)stream;
    if (fake_send_rc) return fake_send_rc;
    if (fake_nreq >= FAKE_MAX_REQS) return -1;
    r = &fake_req[fake_nreq];
    r->id = id;
    r->session = session;
    r->n = g_nw;
    memcpy(r->w, g_w, sizeof(fake_write_t) * (size_t)g_nw);
    /* Last, and a release: a thread polling the count only sees whole entries. Sends are serialized by the DSI
       client's send lock, so the plain read of the count here races with nothing that writes it. */
    __atomic_store_n(&fake_nreq, fake_nreq + 1, __ATOMIC_RELEASE);
    if (g_send_hook) g_send_hook(r);
    return 0;
}

uint32_t DSICreateId(void) { return g_next_id++; }

void DSIOpen(void *stream)
{
    memset(stream, 0, DSI_STREAM_BYTES);
    g_nw = 0;
    fake_opens++;
}
void DSIClose(void *stream) { (void)stream; fake_closes++; }

static void set_err(void *stream) { int one = 1; memcpy((char *)stream + DSI_STREAM_ERROR, &one, sizeof one); }

int DSIWrite(void *stream, const void *p, int size, int align)
{
    fake_write_t *w;
    if (fake_fail_write_at && g_nw + 1 == fake_fail_write_at) { set_err(stream); return -1; }
    if (g_nw >= FAKE_MAX_WRITES || size > FAKE_WRITE_BYTES) { set_err(stream); return -1; }
    w = &g_w[g_nw++];
    w->size = size;
    w->align = align;
    memcpy(w->bytes, p, (size_t)size);
    return 0;
}

int DSIRead(void *stream, void *p, int size, int align)
{
    (void)align;
    if (g_rd_pos + (size_t)size > g_rd_n) { set_err(stream); return -1; }
    memcpy(p, g_rd + g_rd_pos, (size_t)size);
    g_rd_pos += (size_t)size;
    return 0;
}

int DSIWriteString2(void *stream, const char **s)
{
    size_t n = strlen(*s);
    fake_write_t *w;
    if (g_nw >= FAKE_MAX_WRITES || n >= FAKE_WRITE_BYTES) { set_err(stream); return -1; }
    w = &g_w[g_nw++];
    w->size = (int)n;
    w->align = 0;                         /* marks a string write */
    memcpy(w->bytes, *s, n);
    return 0;
}
