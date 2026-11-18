/* A DME behind the fake DSI. See fake_dme.h.
 *
 * The send hook runs on the sender's thread with the DSI client's send lock held, so it only copies the
 * inner UDS into a one-slot mailbox; the responder thread answers. The mailbox lock is never held across a
 * delivery, because delivering takes the link's own lock. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "fake_dme.h"

#define TUNNEL_SEND 0x02
#define TUNNEL_HDR  9          /* 63 F0 31 01 0F0B 12 <len:2>, then the inner UDS */
#define TICK_MS     2
#define HOLD_US     1000       /* default: how long a request stays in the mailbox before its reply */
#define REQ_MAX     16
#define REPLY_MAX   1024
#define HEX_MAX     (2 * REQ_MAX + 1)
#define DATA_MAX    64
#define MAX_PLAIN   16
#define MAX_REFUSE  4
#define MAX_RAW     16

static struct {
    struct { uint16_t did; uint8_t data[DATA_MAX]; size_t n; } plain[MAX_PLAIN];
    int n_plain;
    struct { char prefix[HEX_MAX]; uint8_t nrc; int left; } refuse[MAX_REFUSE];
    int n_refuse;
    struct { char prefix[HEX_MAX]; uint8_t reply[REPLY_MAX]; size_t n; } raw[MAX_RAW];
    int n_raw;
    int silent_from, silent_n, tunnel_refused, drop_at, pending, hold_ms;
    uint8_t slot[REQ_MAX];
    size_t slot_n;
    int slot_seq, slot_full, seq, overlaps;
    char seen[FAKE_MAX_REQS][HEX_MAX];
    int n_seen;
    int stop;
} g;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_thr;
static int g_thr_running;
static void (*g_handshake)(void);

static void sleep_us(long us)
{
    struct timespec ts;
    ts.tv_sec = us / 1000000L;
    ts.tv_nsec = (us % 1000000L) * 1000L;
    nanosleep(&ts, NULL);
}

/* DSI stream payloads, in native byte order. */
typedef struct { uint8_t b[0x400]; size_t n; } pb_t;
static void p32(pb_t *p, uint32_t v) { memcpy(p->b + p->n, &v, 4); p->n += 4; }
static void p16(pb_t *p, uint16_t v) { memcpy(p->b + p->n, &v, 2); p->n += 2; }
static void deliver(uint32_t id, const pb_t *p) { fake_dsi_deliver(0x200, id, p->b, p->n); }

void fake_dme_give_client(void)
{
    pb_t p;
    p.n = 0;
    p32(&p, 0x00070042u);
    p16(&p, 0x0101);
    deliver(0x80000000u, &p);
}

void fake_dme_give_target_up(void)
{
    pb_t p;
    p.n = 0;
    p32(&p, 2);
    p16(&p, 0x0300); p16(&p, 0x0500);
    p16(&p, 0x0301); p16(&p, 0x06F0);
    deliver(0x80000003u, &p);
}

void fake_dme_give_registry_without_key(void)
{
    pb_t p;
    p.n = 0;
    p32(&p, 2);
    p16(&p, 0x0101); p16(&p, 0x8200);
    p16(&p, 0x0301); p16(&p, 0x06F1);
    deliver(0xC0000001u, &p);
}

/* informationProcessMessage, kind 2, flags 0: a report rather than a transmission status. */
void fake_dme_deliver_payload(uint16_t fkt, const uint8_t *payload, size_t n)
{
    pb_t p;
    p.n = 0;
    p32(&p, 0);                      /* transport */
    p32(&p, 2);                      /* kind */
    p32(&p, 0);                      /* flags */
    p16(&p, 0x0101);                 /* target */
    p16(&p, 0x0301);                 /* source */
    p.b[p.n++] = 0x06;               /* FBlock */
    p.b[p.n++] = 0xF0;               /* instance */
    p16(&p, fkt);
    p.b[p.n++] = 0x0C;               /* OpType */
    p32(&p, 0x1234);                 /* client echo */
    p32(&p, (uint32_t)n);
    memcpy(p.b + p.n, payload, n);
    p.n += n;
    deliver(0x80000001u, &p);
}

void fake_dme_deliver_tunnel_reply(uint16_t fkt, const uint8_t *inner, size_t ilen)
{
    uint8_t data[TUNNEL_HDR + REPLY_MAX];
    size_t dn = 0;
    data[dn++] = 0xF0; data[dn++] = 0x63;
    data[dn++] = 0x71; data[dn++] = 0x01;
    data[dn++] = 0x0F; data[dn++] = 0x0B;
    data[dn++] = 0x12;
    data[dn++] = (uint8_t)(ilen >> 8); data[dn++] = (uint8_t)ilen;
    memcpy(data + dn, inner, ilen);
    fake_dme_deliver_payload(fkt, data, dn + ilen);
}

/* Polls the record rather than the send hook, which a test may replace. */
static void wait_for_register(void)
{
    int tries;
    for (tries = 0; tries < 20000 && fake_dsi_req_count(0x01) == 0; tries++) sleep_us(200);
}

void fake_dme_hs_up(void)
{
    fake_dsi_connect();
    fake_dme_give_client();
    wait_for_register();             /* the server announces the target only after the register */
    fake_dme_give_target_up();
}

void fake_dme_hs_no_target(void)
{
    fake_dsi_connect();
    fake_dme_give_client();
    wait_for_register();
    fake_dme_give_registry_without_key();
}

void fake_dme_set_handshake(void (*hs)(void)) { g_handshake = hs; }

void fake_dme_run_hook(void *app)
{
    int hold;
    (void)app;
    if (g_handshake) g_handshake();
    while (__atomic_load_n(&fake_stop_calls, __ATOMIC_ACQUIRE) == 0) sleep_us(500);
    pthread_mutex_lock(&g_mu);
    hold = g.hold_ms;
    pthread_mutex_unlock(&g_mu);
    sleep_us(hold * 1000L);          /* a slow DME is a slow stop too */
}

static void to_hex(char *out, const uint8_t *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02X", b[i]);
    out[2 * n] = '\0';
}

static int starts_with(const char *hex, const char *prefix) { return !strncmp(hex, prefix, strlen(prefix)); }

static size_t negative(uint8_t *out, uint8_t sid, uint8_t nrc)
{
    out[0] = 0x7F; out[1] = sid; out[2] = nrc;
    return 3;
}

/* Caller holds g_mu. */
static size_t dme_reply(const uint8_t *rq, size_t n, uint8_t *out)
{
    uint16_t did;
    int i;
    if (rq[0] != 0x22) return negative(out, rq[0], 0x11);
    if (n != 3) return negative(out, 0x22, 0x13);
    did = (uint16_t)(rq[1] << 8 | rq[2]);
    for (i = 0; i < g.n_plain; i++)
        if (g.plain[i].did == did) {
            out[0] = 0x62; out[1] = rq[1]; out[2] = rq[2];
            memcpy(out + 3, g.plain[i].data, g.plain[i].n);
            return 3 + g.plain[i].n;
        }
    return negative(out, 0x22, 0x31);
}

static void answer(const uint8_t *rq, size_t n, int seq)
{
    static const uint8_t pending[] = { 0x7F, 0x22, 0x78 };
    uint8_t out[REPLY_MAX];
    size_t on = 0;
    char hex[HEX_MAX];
    int drop, silent, refused, pend, hold, i;
    to_hex(hex, rq, n);
    pthread_mutex_lock(&g_mu);
    drop = g.drop_at && seq >= g.drop_at;
    if (drop) g.drop_at = 0;
    silent = g.silent_from > 0 && seq >= g.silent_from && (g.silent_n < 0 || seq < g.silent_from + g.silent_n);
    refused = g.tunnel_refused;
    pend = g.pending;
    hold = g.hold_ms;
    if (!drop && !silent && !refused) {
        for (i = 0; i < g.n_refuse && !on; i++)
            if (g.refuse[i].left != 0 && starts_with(hex, g.refuse[i].prefix)) {
                if (g.refuse[i].left > 0) g.refuse[i].left--;
                on = negative(out, rq[0], g.refuse[i].nrc);
            }
        for (i = 0; i < g.n_raw && !on; i++)
            if (starts_with(hex, g.raw[i].prefix)) { memcpy(out, g.raw[i].reply, g.raw[i].n); on = g.raw[i].n; }
        if (!on) on = dme_reply(rq, n, out);
    }
    pthread_mutex_unlock(&g_mu);
    sleep_us(hold ? hold * 1000L : HOLD_US);   /* a send while the slot is full counts as an overlap */
    pthread_mutex_lock(&g_mu);
    g.slot_full = 0;
    pthread_mutex_unlock(&g_mu);
    if (drop) {
        fake_dsi_disconnect();
    } else if (refused) {
        static const uint8_t outer[] = { 0xF0, 0x63, 0x7F, 0x31, 0x31 };
        fake_dme_deliver_payload(0x050, outer, sizeof outer);
    } else if (on) {
        for (i = 0; i < pend; i++) fake_dme_deliver_tunnel_reply(0x050, pending, sizeof pending);
        fake_dme_deliver_tunnel_reply(0x050, out, on);
    }
}

static void *responder(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_mu);
    while (!g.stop) {
        if (g.slot_full) {
            uint8_t rq[REQ_MAX];
            size_t n = g.slot_n;
            int seq = g.slot_seq;
            memcpy(rq, g.slot, n);
            pthread_mutex_unlock(&g_mu);
            answer(rq, n, seq);
            pthread_mutex_lock(&g_mu);
        } else {
            struct timespec dl;
            clock_gettime(CLOCK_REALTIME, &dl);
            dl.tv_nsec += TICK_MS * 1000000L;
            if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
            pthread_cond_timedwait(&g_cv, &g_mu, &dl);
        }
    }
    pthread_mutex_unlock(&g_mu);
    return NULL;
}

static void on_send(const fake_req_t *r)
{
    const fake_write_t *w;
    size_t n;
    if (r->id != TUNNEL_SEND || r->n < 1) return;
    w = &r->w[r->n - 1];
    if (w->size <= TUNNEL_HDR) return;
    n = (size_t)w->size - TUNNEL_HDR;
    if (n > REQ_MAX) n = REQ_MAX;
    pthread_mutex_lock(&g_mu);
    if (g.slot_full) g.overlaps++;
    memcpy(g.slot, w->bytes + TUNNEL_HDR, n);
    g.slot_n = n;
    g.slot_seq = ++g.seq;
    g.slot_full = 1;
    if (g.n_seen < FAKE_MAX_REQS) to_hex(g.seen[g.n_seen++], g.slot, n);
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_mu);
}

void fake_dme_start(enum fake_handshake h)
{
    pthread_mutex_lock(&g_mu);
    memset(&g, 0, sizeof g);
    pthread_mutex_unlock(&g_mu);
    fake_dme_set_handshake(h == FAKE_UP ? fake_dme_hs_up : h == FAKE_NO_TARGET ? fake_dme_hs_no_target : NULL);
    fake_dsi_set_run_hook(fake_dme_run_hook);
    fake_dsi_set_send_hook(on_send);
    if (pthread_create(&g_thr, NULL, responder, NULL) == 0) g_thr_running = 1;
}

void fake_dme_stop(void)
{
    if (!g_thr_running) return;
    pthread_mutex_lock(&g_mu);
    g.stop = 1;
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_mu);
    pthread_join(g_thr, NULL);
    g_thr_running = 0;
    fake_dsi_set_send_hook(NULL);
}

void fake_dme_plain(uint16_t did, const uint8_t *data, size_t n)
{
    pthread_mutex_lock(&g_mu);
    if (g.n_plain < MAX_PLAIN && n <= DATA_MAX) {
        g.plain[g.n_plain].did = did;
        memcpy(g.plain[g.n_plain].data, data, n);
        g.plain[g.n_plain].n = n;
        g.n_plain++;
    }
    pthread_mutex_unlock(&g_mu);
}

void fake_dme_raw(const char *hex_request_prefix, const uint8_t *reply, size_t n)
{
    pthread_mutex_lock(&g_mu);
    if (g.n_raw < MAX_RAW && n <= REPLY_MAX) {
        snprintf(g.raw[g.n_raw].prefix, HEX_MAX, "%s", hex_request_prefix);
        memcpy(g.raw[g.n_raw].reply, reply, n);
        g.raw[g.n_raw].n = n;
        g.n_raw++;
    }
    pthread_mutex_unlock(&g_mu);
}

void fake_dme_refuse(const char *hex_prefix, uint8_t nrc, int times)
{
    pthread_mutex_lock(&g_mu);
    if (g.n_refuse < MAX_REFUSE) {
        snprintf(g.refuse[g.n_refuse].prefix, HEX_MAX, "%s", hex_prefix);
        g.refuse[g.n_refuse].nrc = nrc;
        g.refuse[g.n_refuse].left = times == 0 ? -1 : times;
        g.n_refuse++;
    }
    pthread_mutex_unlock(&g_mu);
}

#define SETTER(name, field) void name(int v) { pthread_mutex_lock(&g_mu); g.field = v; pthread_mutex_unlock(&g_mu); }
SETTER(fake_dme_pending, pending)
SETTER(fake_dme_tunnel_refused, tunnel_refused)
SETTER(fake_dme_drop_link_after, drop_at)
SETTER(fake_dme_hold_ms, hold_ms)
#undef SETTER

void fake_dme_silent_window(int from_request, int n_requests)
{
    pthread_mutex_lock(&g_mu);
    g.silent_from = from_request;
    g.silent_n = n_requests;
    pthread_mutex_unlock(&g_mu);
}

int fake_dme_count(const char *hex_prefix)
{
    int i, n = 0;
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < g.n_seen; i++) n += starts_with(g.seen[i], hex_prefix);
    pthread_mutex_unlock(&g_mu);
    return n;
}

int fake_dme_overlaps(void)
{
    int n;
    pthread_mutex_lock(&g_mu);
    n = g.overlaps;
    pthread_mutex_unlock(&g_mu);
    return n;
}
