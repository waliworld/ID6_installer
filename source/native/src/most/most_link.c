/* The MOST UDS tunnel over the DSI client.
 *
 * most_request() wraps one inner UDS request in a 0x0F0B tunnel datagram for instance 0xF0 (FBlock 0x06,
 * FktID 0x050) and waits on a condition variable for the matching Status report, which h_report() hands
 * across under g_mu. Open order: wait for the MOST client, then register the key, then wait for
 * informationConnect; the server only announces the target after the register.
 *
 * g_mu and g_cv are created once and never destroyed, so a DSI thread that outlives a failed close can
 * still lock them safely. Reply bytes are only read byte-wise. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include "most_link.h"
#include "dsi_abi.h"
#include "dsi_client.h"
#include "most_proto.h"

#define MOST_ROLE        "MOSTSubSystem"
#define MOST_FBLOCK      0x06
#define MOST_INSTANCE    0xF0
#define MOST_KEY         ((uint16_t)((MOST_FBLOCK << 8) | MOST_INSTANCE))
#define MOST_FKTID       0x050
#define MOST_ROUTINE     0x0F0B
#define MOST_DIAG_SRC    0x63

#define REQ_MAX          16
#define REPLY_CAP_DEF    64     /* most_request's reply cap */
#define FRAME_MAX        80     /* 9 header bytes and the inner request */
#define JOIN_STEP_MS     50
#define JOIN_MAX_MS      3000
#define PENDING_TOTAL_MS 5000   /* responsePending may stretch one request this long in all */
#define STOP_SLICE_MS    100    /* how often a wait looks at the running flag */

enum uds_result { UDS_POSITIVE, UDS_NEGATIVE, UDS_PENDING, UDS_MISMATCH };

struct most_ctx {
    int timeout_ms;          /* the open's; most_request's wait */
    pthread_t thr;
    int thr_running;         /* created and not yet joined; caller thread only */
    /* Below, guarded by g_mu. */
    int have_client, have_target;
    int registry_lacks_key;  /* decides NO_TARGET over NO_SERVER at the open timeout */
    int route_refused;       /* outer 7F 31 to the request in flight */
    int disconnected, thread_exited, closing;
    int waiting, reply_ready, pending_seen;
    uint8_t req[REQ_MAX];
    size_t req_len;
    uint8_t *resp;           /* the waiter's buffer; NULL once it must not be written */
    size_t resp_cap;         /* min(caller's cap, MOST_INNER_MAX) */
    size_t *resp_len;
};

static struct most_ctx g_most;
static int g_stuck;          /* a DSI thread never stopped: no second client in this process */
static pthread_mutex_t g_mu;
static pthread_cond_t g_cv;
static clockid_t g_clk = CLOCK_REALTIME;
static int g_sync_inited;
static const volatile sig_atomic_t *g_running;   /* waits end early once it reads 0 */

static void sync_init_once(void)
{
    pthread_condattr_t ca;
    if (g_sync_inited) return;
    pthread_mutex_init(&g_mu, NULL);
    pthread_condattr_init(&ca);
    /* Deadlines must use the clock the condition variable really runs on. */
#ifdef __APPLE__
    g_clk = CLOCK_REALTIME;   /* no pthread_condattr_setclock */
#else
    g_clk = pthread_condattr_setclock(&ca, CLOCK_MONOTONIC) == 0 ? CLOCK_MONOTONIC : CLOCK_REALTIME;
#endif
    pthread_cond_init(&g_cv, &ca);
    pthread_condattr_destroy(&ca);
    g_sync_inited = 1;
}

static void deadline_in(struct timespec *ts, int ms)
{
    clock_gettime(g_clk, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec += 1; ts->tv_nsec -= 1000000000L; }
}

static int ts_after(const struct timespec *a, const struct timespec *b)
{
    return a->tv_sec != b->tv_sec ? a->tv_sec > b->tv_sec : a->tv_nsec > b->tv_nsec;
}

static int stopping(void) { return g_running && !*g_running; }

/* pthread_cond_timedwait on g_cv up to dl, in slices that notice the running flag. ETIMEDOUT at dl or
   once stopping; 0 otherwise, so the caller rechecks its condition. Caller holds g_mu. */
static int wait_until(const struct timespec *dl)
{
    struct timespec slice, now;
    if (stopping()) return ETIMEDOUT;
    deadline_in(&slice, STOP_SLICE_MS);
    if (ts_after(&slice, dl)) slice = *dl;
    if (pthread_cond_timedwait(&g_cv, &g_mu, &slice) != ETIMEDOUT) return 0;
    clock_gettime(g_clk, &now);
    return !ts_after(dl, &now) || stopping() ? ETIMEDOUT : 0;
}

/* Classifies resp as the reply to req. Positive 0x22 must echo the DID, 0x01 the PID and 0x19 the
   subfunction; 0x2C and 0x2F echo up to three bytes, so MHD's three-byte 2F 01 <slot> echoes two. */
static enum uds_result uds_check(const uint8_t *req, size_t req_len, const uint8_t *resp, size_t resp_len)
{
    size_t echo = 0;
    if (req_len < 1 || resp_len < 1) return UDS_MISMATCH;
    if (resp[0] == 0x7F) {
        if (resp_len < 3 || resp[1] != req[0]) return UDS_MISMATCH;
        return resp[2] == 0x78 ? UDS_PENDING : UDS_NEGATIVE;
    }
    if (resp[0] != (uint8_t)(req[0] + 0x40)) return UDS_MISMATCH;
    if (req[0] == 0x22) echo = 2;
    else if (req[0] == 0x01 || req[0] == 0x19) echo = 1;   /* the PID, the subfunction */
    else if (req[0] == 0x2C || req[0] == 0x2F) echo = req_len > 3 ? 3 : 2;
    if (echo && (req_len < 1 + echo || resp_len < 1 + echo || memcmp(req + 1, resp + 1, echo)))
        return UDS_MISMATCH;
    return UDS_POSITIVE;
}

static void wake(void) { pthread_cond_broadcast(&g_cv); }

static void h_connected(void *u) { (void)u; }   /* responseMOSTClient is the real gate */

static void h_disconnected(void *u)
{
    struct most_ctx *m = u;
    pthread_mutex_lock(&g_mu);
    m->have_client = m->have_target = 0;
    m->disconnected = 1;
    wake();
    pthread_mutex_unlock(&g_mu);
}

static void h_most_client(void *u, uint32_t client_id, uint16_t device_id)
{
    struct most_ctx *m = u;
    (void)client_id; (void)device_id;
    pthread_mutex_lock(&g_mu);
    m->have_client = 1;
    wake();
    pthread_mutex_unlock(&g_mu);
}

/* Registries can arrive in pieces, so one without the key does not end the open's wait. */
static void h_registry(void *u, const uint16_t *pairs, int n_pairs)
{
    struct most_ctx *m = u;
    int i;
    for (i = 0; i < n_pairs; i++)
        if (pairs[2 * i + 1] == MOST_KEY) return;
    pthread_mutex_lock(&g_mu);
    m->registry_lacks_key = 1;
    pthread_mutex_unlock(&g_mu);
}

static void h_target_up(void *u, uint16_t device)
{
    struct most_ctx *m = u;
    (void)device;
    pthread_mutex_lock(&g_mu);
    m->have_target = 1;
    wake();
    pthread_mutex_unlock(&g_mu);
}

static void h_target_down(void *u)
{
    struct most_ctx *m = u;
    pthread_mutex_lock(&g_mu);
    m->have_target = 0;
    m->disconnected = 1;
    wake();
    pthread_mutex_unlock(&g_mu);
}

/* A FktID 0x050 report: [src][tgt], then either 71 01 <routine> <ecu> <len:2> <inner> (the ECU's reply) or
   7F 31 <nrc> (the tunnel routine itself refused). 7F 31 78 and an inner 7F xx 78 mean pending: keep waiting. */
static void h_report(void *u, uint16_t fkt, uint8_t op, const uint8_t *p, size_t n)
{
    struct most_ctx *m = u;
    static uint8_t inner[MOST_INNER_MAX];   /* static: small DSI thread stack; reports are serialized */
    size_t ilen = 0;
    enum uds_result r;
    (void)op;
    pthread_mutex_lock(&g_mu);
    if (!m->waiting || m->reply_ready || fkt != MOST_FKTID || n < 2) goto out;
    if (n >= 5 && p[2] == 0x7F && p[3] == 0x31 && p[4] == 0x78) { m->pending_seen = 1; wake(); goto out; }
    if (n >= 4 && p[2] == 0x7F && p[3] == 0x31) { m->route_refused = 1; wake(); goto out; }
    if (most_tunnel_parse(p + 2, n - 2, MOST_ROUTINE, inner, m->resp_cap, &ilen) != 0) goto out;
    r = uds_check(m->req, m->req_len, inner, ilen);
    if (r == UDS_MISMATCH) goto out;   /* a reply to some other request */
    if (r == UDS_PENDING) { m->pending_seen = 1; wake(); goto out; }
    if (m->resp && m->resp_cap) {
        size_t cp = ilen < m->resp_cap ? ilen : m->resp_cap;
        if (cp) memcpy(m->resp, inner, cp);
        if (m->resp_len) *m->resp_len = cp;
    } else if (m->resp_len) {
        *m->resp_len = 0;
    }
    m->reply_ready = 1;
    wake();
out:
    pthread_mutex_unlock(&g_mu);
}

static void *most_run(void *arg)
{
    struct most_ctx *m = arg;
    const dsi_handlers_t h = {
        h_connected, h_disconnected, h_most_client, h_registry, h_target_up, h_target_down, h_report
    };
    sigset_t set;
    /* Stop signals belong to the caller's thread. SIGALRM stays deliverable as the hard bound. */
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    dsi_client_run(MOST_ROLE, &h, m);
    pthread_mutex_lock(&g_mu);
    m->thread_exited = m->disconnected = 1;
    m->have_client = m->have_target = 0;
    wake();
    pthread_mutex_unlock(&g_mu);
    return NULL;
}

/* Stops the DSI loop and joins the thread, giving up after JOIN_MAX_MS. 0 when joined. */
static int most_teardown(struct most_ctx *m)
{
    int exited = 0, waited = 0;
    pthread_mutex_lock(&g_mu);
    m->closing = 1;
    m->resp = NULL;
    m->resp_len = NULL;
    wake();
    pthread_mutex_unlock(&g_mu);
    while (waited < JOIN_MAX_MS) {
        struct timespec dl;
        pthread_mutex_lock(&g_mu);
        exited = m->thread_exited;
        pthread_mutex_unlock(&g_mu);
        if (exited) break;
        dsi_client_stop();   /* a no-op until DSIClientRun is running */
        deadline_in(&dl, JOIN_STEP_MS);
        pthread_mutex_lock(&g_mu);
        if (!m->thread_exited) pthread_cond_timedwait(&g_cv, &g_mu, &dl);
        exited = m->thread_exited;
        pthread_mutex_unlock(&g_mu);
        waited += JOIN_STEP_MS;
    }
    m->thr_running = 0;
    if (!exited) { g_stuck = 1; return -1; }   /* left running; the process is expected to exit */
    pthread_join(m->thr, NULL);
    return 0;
}

void most_set_running(const volatile sig_atomic_t *running) { g_running = running; }

enum most_open_rc most_open(int timeout_ms)
{
    struct most_ctx *m = &g_most;
    struct timespec dl;
    enum most_open_rc rc;
    int have_client;
    if (g_stuck || m->thr_running) return MOST_NO_SERVER;
    if (most_dsi_bind() != 0) return MOST_NO_LIBRARY;
    sync_init_once();
    memset(m, 0, sizeof *m);
    m->timeout_ms = timeout_ms;
    dsi_client_set_target(MOST_KEY, MOST_FBLOCK, MOST_INSTANCE);   /* before the client runs */
    if (pthread_create(&m->thr, NULL, most_run, m) != 0) return MOST_NO_SERVER;
    m->thr_running = 1;

    deadline_in(&dl, timeout_ms);
    pthread_mutex_lock(&g_mu);
    while (!m->have_client && !m->disconnected && !m->thread_exited)
        if (wait_until(&dl) == ETIMEDOUT) break;
    have_client = m->have_client;
    pthread_mutex_unlock(&g_mu);
    if (!have_client) return MOST_NO_SERVER;

    dsi_register_target();
    deadline_in(&dl, timeout_ms);
    pthread_mutex_lock(&g_mu);
    while (!m->have_target && !m->disconnected && !m->thread_exited)
        if (wait_until(&dl) == ETIMEDOUT) break;
    if (m->have_target) rc = MOST_UP;
    else if (m->registry_lacks_key && !m->disconnected && !m->thread_exited) rc = MOST_NO_TARGET;
    else rc = MOST_NO_SERVER;
    pthread_mutex_unlock(&g_mu);
    return rc;
}

enum most_rc most_request_wait(uint8_t ecu, const uint8_t *uds, size_t uds_len,
                               uint8_t *resp, size_t resp_cap, size_t *resp_len, unsigned timeout_ms)
{
    struct most_ctx *m = &g_most;
    uint8_t frame[FRAME_MAX];
    size_t flen;
    struct timespec dl, hard;
    enum most_rc rc;
    if (!g_sync_inited) return MOST_CLOSED;
    pthread_mutex_lock(&g_mu);
    m->route_refused = 0;   /* a refusal answers one request only */
    if (m->closing || m->disconnected || m->thread_exited || !m->have_target ||
        uds_len == 0 || uds_len > sizeof m->req) {
        pthread_mutex_unlock(&g_mu);
        return MOST_CLOSED;
    }
    memcpy(m->req, uds, uds_len);
    m->req_len = uds_len;
    m->resp = resp;
    m->resp_cap = resp_cap < MOST_INNER_MAX ? resp_cap : MOST_INNER_MAX;
    m->resp_len = resp_len;
    m->reply_ready = m->pending_seen = 0;
    m->waiting = 1;
    pthread_mutex_unlock(&g_mu);

    /* Sent outside the lock; a report arriving at once sets the sticky reply_ready. */
    flen = most_tunnel_build(frame, sizeof frame, MOST_DIAG_SRC, MOST_INSTANCE, MOST_ROUTINE, ecu, uds, uds_len);
    if (!flen || dsi_send_fn(MOST_FBLOCK, MOST_INSTANCE, MOST_FKTID, 0, frame, flen) != 0) {
        pthread_mutex_lock(&g_mu);
        m->waiting = 0;
        m->resp = NULL;
        m->resp_len = NULL;
        pthread_mutex_unlock(&g_mu);
        return MOST_CLOSED;
    }
    deadline_in(&dl, (int)timeout_ms);
    deadline_in(&hard, timeout_ms > PENDING_TOTAL_MS ? (int)timeout_ms : PENDING_TOTAL_MS);
    pthread_mutex_lock(&g_mu);
    for (;;) {
        while (!m->reply_ready && !m->route_refused && !m->closing && !m->disconnected &&
               !m->thread_exited && !m->pending_seen)
            if (wait_until(&dl) == ETIMEDOUT) break;
        if (m->reply_ready || m->route_refused || m->closing || m->disconnected || m->thread_exited) break;
        if (!m->pending_seen || stopping()) break;   /* timed out */
        m->pending_seen = 0;           /* pending: re-arm, never past the hard cap */
        deadline_in(&dl, (int)timeout_ms);
        if (ts_after(&dl, &hard)) dl = hard;
    }
    if (m->closing || m->disconnected || m->thread_exited) rc = MOST_CLOSED;
    else if (m->route_refused) rc = MOST_NOROUTE;
    else if (m->reply_ready) rc = MOST_REPLY;
    else rc = MOST_TIMEOUT;
    m->waiting = 0;
    m->resp = NULL;
    m->resp_len = NULL;
    pthread_mutex_unlock(&g_mu);
    return rc;
}

enum most_rc most_request(uint8_t ecu, const uint8_t *uds, size_t uds_len,
                          uint8_t *resp, size_t resp_cap, size_t *resp_len)
{
    return most_request_wait(ecu, uds, uds_len, resp, resp_cap < REPLY_CAP_DEF ? resp_cap : REPLY_CAP_DEF,
                             resp_len, (unsigned)g_most.timeout_ms);
}

int most_close(void)
{
    struct most_ctx *m = &g_most;
    if (!m->thr_running) return g_stuck ? -1 : 0;
    /* Target, then client, then stop. Each sends nothing without a MOST client. */
    dsi_unregister_target();
    dsi_unregister_client();
    return most_teardown(m);
}
