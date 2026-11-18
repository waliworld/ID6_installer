/* The MOST tunnel against the fake DSI and DME. Host only.
 *
 * The DSI client blocks on its own thread and most_request() on this one. fake_dme's run hook does the
 * connect handshake on the client thread (connect, MOST client, wait for our register, target up), then
 * parks until stopped; a driver thread delivers later events once the tunnel send is out. An alarm()
 * fails a hung case. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "most/dsi_abi.h"
#include "most/most_link.h"
#include "fake_dme.h"
#include "fake_dsi.h"

static int failures, checks;
#define CHECK(e) do { ++checks; if (!(e)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); } } while (0)

static int bind_rc;
int most_fake_bind(void) { return bind_rc; }

static void sleep_us(long us)
{
    struct timespec ts;
    ts.tv_sec = us / 1000000L;
    ts.tv_nsec = (us % 1000000L) * 1000L;
    nanosleep(&ts, NULL);
}

static long ms_since(const struct timespec *t0)
{
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec - t0->tv_sec) * 1000 + (t1.tv_nsec - t0->tv_nsec) / 1000000;
}

static int req_index(uint32_t id)
{
    int i;
    for (i = 0; i < fake_nreq; i++)
        if (fake_req[i].id == id) return i;
    return -1;
}

static int wait_req_count(uint32_t id, int n)
{
    int tries;
    for (tries = 0; tries < 20000; tries++) {
        if (fake_dsi_req_count(id) >= n) return 1;
        sleep_us(200);
    }
    return 0;
}

static void (*g_action)(void);

static void setup(void (*hs)(void), void (*action)(void))
{
    fake_dsi_reset();
    fake_dme_set_handshake(hs);
    fake_dsi_set_run_hook(fake_dme_run_hook);
    g_action = action;
}

static void *driver(void *arg)
{
    (void)arg;
    wait_req_count(0x02, 1);
    sleep_us(2000);            /* let the request reach its wait; the flags are sticky anyway */
    if (g_action) g_action();
    return NULL;
}

static void hs_partial_registry_then_up(void)
{
    fake_dsi_connect();
    fake_dme_give_client();
    wait_req_count(0x01, 1);
    fake_dme_give_registry_without_key();
    sleep_us(100000);
    fake_dme_give_target_up();
}

static void hs_client_only(void)
{
    fake_dsi_connect();
    fake_dme_give_client();
}

static const uint8_t UDS_READ[3] = { 0x22, 0x42, 0x05 };
static const uint8_t INNER_OK[5] = { 0x62, 0x42, 0x05, 0x31, 0xE5 };
static const uint8_t PENDING[3] = { 0x7F, 0x22, 0x78 };

static void reply_ok(void) { fake_dme_deliver_tunnel_reply(0x050, INNER_OK, sizeof INNER_OK); }
static void reply_nrc(void) { static const uint8_t n[3] = { 0x7F, 0x22, 0x31 }; fake_dme_deliver_tunnel_reply(0x050, n, 3); }
static void foreign_then_ok(void)
{
    static const uint8_t other[5] = { 0x62, 0x42, 0x05, 0xAA, 0xAA };
    fake_dme_deliver_tunnel_reply(0x051, other, sizeof other);   /* wrong FktID */
    sleep_us(1000);
    reply_ok();
}
static void stale_then_ok(void)
{
    static const uint8_t stale[5] = { 0x62, 0x42, 0x01, 0xAA, 0xBB };   /* another DID */
    fake_dme_deliver_tunnel_reply(0x050, stale, sizeof stale);
    sleep_us(1000);
    reply_ok();
}
static void pending_then_ok(void)
{
    fake_dme_deliver_tunnel_reply(0x050, PENDING, sizeof PENDING);
    sleep_us(1000);
    reply_ok();
}
static void route_refusal(void)
{
    static const uint8_t d[5] = { 0xF0, 0x63, 0x7F, 0x31, 0x31 };
    fake_dme_deliver_payload(0x050, d, sizeof d);
}
static void drop(void) { fake_dsi_disconnect(); }
static void close_now(void) { most_close(); }
static void reply_100(void)
{
    uint8_t inner[100];
    size_t i;
    memcpy(inner, INNER_OK, 3);
    for (i = 3; i < sizeof inner; i++) inner[i] = (uint8_t)i;
    fake_dme_deliver_tunnel_reply(0x050, inner, sizeof inner);
}

/* Pending about 200 ms in, inside a 300 ms wait; the reply about 400 ms in, past it. */
static void *late_pending(void *arg)
{
    int outer = arg != NULL;
    static const uint8_t outer_pending[5] = { 0xF0, 0x63, 0x7F, 0x31, 0x78 };
    wait_req_count(0x02, 1);
    sleep_us(200000);
    if (outer) fake_dme_deliver_payload(0x050, outer_pending, sizeof outer_pending);
    else fake_dme_deliver_tunnel_reply(0x050, PENDING, sizeof PENDING);
    sleep_us(200000);
    reply_ok();
    return NULL;
}

static void *endless_pending(void *arg)
{
    int i;
    (void)arg;
    wait_req_count(0x02, 1);
    for (i = 0; i < 60; i++) {
        fake_dme_deliver_tunnel_reply(0x050, PENDING, sizeof PENDING);
        sleep_us(100000);
    }
    return NULL;
}

static void *refuse_then_reply(void *arg)
{
    (void)arg;
    wait_req_count(0x02, 1);
    sleep_us(2000);
    route_refusal();
    wait_req_count(0x02, 2);
    sleep_us(2000);
    reply_ok();
    return NULL;
}

static enum most_rc request_with(void *(*fn)(void *), void *arg, uint8_t *resp, size_t *rlen)
{
    pthread_t t;
    enum most_rc rc;
    pthread_create(&t, NULL, fn, arg);
    rc = most_request(MOST_ECU_DME, UDS_READ, sizeof UDS_READ, resp, 64, rlen);
    pthread_join(t, NULL);
    return rc;
}

static int got_ok(const uint8_t *resp, size_t n) { return n == sizeof INNER_OK && !memcmp(resp, INNER_OK, n); }

static void on_alarm(int s)
{
    (void)s;
    if (write(2, "WATCHDOG: test_most hung\n", 25) < 0) {}
    _exit(1);
}

/* The exact requests DSI is handed for an open, one read and a close; little-endian host words. */
static void test_wire_bytes(void)
{
    static const char *const want[] = {
        "0F 626F6F73746465636B:0",
        "01 42000700:4 01000000:4 F006:2",
        ("02 42000700:4 00000000:4 01000000:4 00000000:4 0103:2 0101:2 06:1 F0:1 5000:2 00:1 42000000:4 "
            "0C000000:4 63F031010F0B120003224205:1"),
        "05 42000700:4 01000000:4 F006:2",
        "10",
    };
    static const uint32_t reg[12] = { 0x80000000u, 0x80000001u, 0x80000002u, 0x80000003u, 0x80000004u,
        0x80000005u, 0x80000006u, 0x80000007u, 0xC0000000u, 0xC0000001u, 0xC0000002u, 0xC0000004u };
    static const uint32_t notify[4] = { 0xC0000002u, 0xC0000004u, 0xC0000001u, 0xC0000000u };
    static const uint8_t data[2] = { 0x31, 0xE5 };
    const uint16_t probe = 1;
    uint8_t resp[64];
    size_t rlen = 0;
    char line[512];
    int i, j, b;
    size_t o;

    alarm(10);
    fake_dsi_reset();
    fake_dme_start(FAKE_UP);
    fake_dme_plain(0x4205, data, sizeof data);
    CHECK(most_open(2000) == MOST_UP);
    CHECK(most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_REPLY && got_ok(resp, rlen));
    CHECK(most_close() == 0);
    fake_dme_stop();
    CHECK(!strcmp(fake_role, "MOSTSubSystem") && fake_major == 4 && fake_minor == 5);
    CHECK(fake_reg_n == 12 && !memcmp(fake_reg_ids, reg, sizeof reg));
    CHECK(fake_nnotify == 4 && !memcmp(fake_notify, notify, sizeof notify));
    CHECK(fake_cb_off_ok && fake_ncb == 4 && fake_free_calls == 1 && fake_opens == fake_closes);
    CHECK(fake_nreq == 5 && fake_stop_nreq == 5);
    if (*(const uint8_t *)&probe != 1) { alarm(0); return; }
    for (i = 0; i < fake_nreq && i < 5; i++) {
        o = (size_t)snprintf(line, sizeof line, "%02X", (unsigned)fake_req[i].id);
        for (j = 0; j < fake_req[i].n; j++) {
            line[o++] = ' ';
            for (b = 0; b < fake_req[i].w[j].size; b++)
                o += (size_t)snprintf(line + o, sizeof line - o, "%02X", fake_req[i].w[j].bytes[b]);
            o += (size_t)snprintf(line + o, sizeof line - o, ":%d", fake_req[i].w[j].align);
        }
        if (strcmp(line, want[i])) printf("wire %d\n  want %s\n  got  %s\n", i, want[i], line);
        CHECK(!strcmp(line, want[i]));
    }
    alarm(0);
}

static void test_missing_library(void)
{
    alarm(10);
    setup(fake_dme_hs_up, NULL);
    bind_rc = -1;
    CHECK(most_open(200) == MOST_NO_LIBRARY);
    CHECK(fake_client_inits == 0);
    CHECK(most_close() == 0 && fake_nreq == 0);
    alarm(0);
}

static void test_close_without_client(void)
{
    alarm(10);
    setup(NULL, NULL);
    CHECK(most_close() == 0);
    CHECK(fake_nreq == 0 && fake_stop_calls == 0);
    CHECK(most_open(100) == MOST_NO_SERVER);
    CHECK(fake_free_calls == 0);          /* a failed open leaves the close to its caller */
    CHECK(most_close() == 0);
    CHECK(fake_free_calls == 1);
    CHECK(fake_dsi_req_count(0x05) == 0 && fake_dsi_req_count(0x10) == 0);
    alarm(0);
}

static void test_simple(void (*hs)(void), void (*action)(void), int open_ms, enum most_rc want, int want_ok)
{
    uint8_t resp[64];
    size_t rlen = 0;
    pthread_t t;
    enum most_rc rc;
    alarm(10);
    setup(hs, action);
    CHECK(most_open(open_ms) == MOST_UP);
    pthread_create(&t, NULL, driver, NULL);
    rc = most_request(MOST_ECU_DME, UDS_READ, sizeof UDS_READ, resp, sizeof resp, &rlen);
    pthread_join(t, NULL);
    CHECK(rc == want);
    if (want_ok) CHECK(got_ok(resp, rlen));
    CHECK(most_close() == 0);
    alarm(0);
}

static void test_nrc_is_a_reply(void)
{
    uint8_t resp[64];
    size_t rlen = 0;
    pthread_t t;
    alarm(10);
    setup(fake_dme_hs_up, reply_nrc);
    CHECK(most_open(1000) == MOST_UP);
    pthread_create(&t, NULL, driver, NULL);
    CHECK(most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_REPLY);
    pthread_join(t, NULL);
    CHECK(rlen == 3 && resp[0] == 0x7F && resp[1] == 0x22 && resp[2] == 0x31);
    most_close();
    alarm(0);
}

static void test_no_target(void)
{
    uint8_t resp[64];
    size_t rlen = 0;
    struct timespec t0;
    alarm(10);
    setup(fake_dme_hs_no_target, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK(most_open(300) == MOST_NO_TARGET);
    CHECK(ms_since(&t0) >= 250);          /* a registry without the key does not end the wait */
    CHECK(most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_CLOSED);
    CHECK(most_close() == 0);
    alarm(0);
}

static void test_open_variants(void)
{
    alarm(10);
    setup(hs_partial_registry_then_up, NULL);
    CHECK(most_open(1000) == MOST_UP);
    CHECK(most_close() == 0);
    setup(hs_client_only, NULL);
    CHECK(most_open(150) == MOST_NO_SERVER);
    CHECK(fake_dsi_req_count(0x05) == 0 && fake_dsi_req_count(0x10) == 0);
    CHECK(most_close() == 0);
    CHECK(fake_dsi_req_count(0x05) == 1 && fake_dsi_req_count(0x10) == 1 && fake_free_calls == 1);
    CHECK(most_close() == 0 && fake_dsi_req_count(0x10) == 1);
    alarm(0);
}

static void test_close_order_and_twice(void)
{
    int stops;
    alarm(10);
    setup(fake_dme_hs_up, NULL);
    CHECK(most_open(1000) == MOST_UP);
    CHECK(most_close() == 0);
    CHECK(fake_dsi_req_count(0x05) == 1 && fake_dsi_req_count(0x10) == 1);
    CHECK(req_index(0x05) < req_index(0x10) && req_index(0x10) < fake_stop_nreq);
    CHECK(fake_free_calls == 1 && fake_run_blocks_stops && !fake_run_blocks_alarm);
    stops = fake_stop_calls;
    fake_nreq = 0;
    CHECK(most_close() == 0);
    CHECK(fake_nreq == 0 && fake_stop_calls == stops);
    alarm(0);
}

static void test_pending(void)
{
    uint8_t resp[64];
    size_t rlen = 0;
    struct timespec t0;
    pthread_t t;
    long elapsed;
    alarm(15);
    setup(fake_dme_hs_up, NULL);
    CHECK(most_open(300) == MOST_UP);
    CHECK(request_with(late_pending, NULL, resp, &rlen) == MOST_REPLY && got_ok(resp, rlen));
    CHECK(request_with(late_pending, resp, resp, &rlen) == MOST_REPLY && got_ok(resp, rlen));
    most_close();
    setup(fake_dme_hs_up, NULL);
    CHECK(most_open(150) == MOST_UP);
    pthread_create(&t, NULL, endless_pending, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK(most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_TIMEOUT);
    elapsed = ms_since(&t0);
    pthread_join(t, NULL);
    CHECK(elapsed >= 4500 && elapsed < 6000);   /* the 5 s cap */
    most_close();
    alarm(0);
}

static void test_close_during_wait(void)
{
    uint8_t resp[64];
    size_t rlen = 0;
    pthread_t t;
    struct timespec t0;
    alarm(10);
    setup(fake_dme_hs_up, close_now);
    CHECK(most_open(5000) == MOST_UP);
    pthread_create(&t, NULL, driver, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK(most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_CLOSED);
    CHECK(ms_since(&t0) < 500);
    pthread_join(t, NULL);
    CHECK(most_close() == 0);
    alarm(0);
}

static void test_refusal_does_not_stick(void)
{
    uint8_t resp[64];
    size_t rlen = 0;
    pthread_t t;
    alarm(10);
    setup(fake_dme_hs_up, NULL);
    CHECK(most_open(2000) == MOST_UP);
    pthread_create(&t, NULL, refuse_then_reply, NULL);
    CHECK(most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_NOROUTE);
    CHECK(most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_REPLY);
    pthread_join(t, NULL);
    CHECK(got_ok(resp, rlen));
    most_close();
    alarm(0);
}

static void test_many_reads_one_client(void)
{
    static const uint8_t data[2] = { 0x31, 0xE5 };
    uint8_t resp[64];
    size_t rlen = 0;
    int i, ok = 1;
    alarm(10);
    fake_dsi_reset();
    fake_dme_start(FAKE_UP);
    fake_dme_plain(0x4205, data, sizeof data);
    fake_dme_pending(2);
    CHECK(most_open(1000) == MOST_UP);
    for (i = 0; i < 20; i++)
        ok &= most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_REPLY && got_ok(resp, rlen);
    CHECK(ok);
    CHECK(fake_client_inits == 1 && fake_dme_count("224205") == 20 && fake_dme_overlaps() == 0);
    fake_dme_drop_link_after(21);
    CHECK(most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_CLOSED);
    CHECK(most_close() == 0);
    fake_dme_stop();
    /* A later open in the same process starts from clean state. */
    fake_dsi_reset();
    fake_dme_start(FAKE_UP);
    fake_dme_plain(0x4205, data, sizeof data);
    CHECK(most_open(1000) == MOST_UP);
    CHECK(most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_REPLY && got_ok(resp, rlen));
    CHECK(most_close() == 0);
    fake_dme_stop();
    alarm(0);
}

/* A raw fake reply through most_request_wait; the setters run on this thread before the request. */
static enum most_rc raw_request(const char *prefix, const uint8_t *rq, size_t rqn, const uint8_t *reply,
                                size_t n, size_t cap, unsigned ms, uint8_t *resp, size_t *rlen)
{
    enum most_rc rc;
    fake_dsi_reset();
    fake_dme_start(FAKE_UP);
    fake_dme_raw(prefix, reply, n);
    CHECK(most_open(2000) == MOST_UP);
    rc = most_request_wait(MOST_ECU_DME, rq, rqn, resp, cap, rlen, ms);
    CHECK(most_close() == 0);
    fake_dme_stop();
    return rc;
}

static void test_request_wait_honours_timeout(void)
{
    uint8_t resp[64];
    size_t rlen = 0;
    struct timespec t0;
    long ms;
    alarm(10);
    fake_dsi_reset();
    fake_dme_start(FAKE_UP);
    fake_dme_silent_window(1, 1);
    CHECK(most_open(2000) == MOST_UP);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK(most_request_wait(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen, 300) == MOST_TIMEOUT);
    ms = ms_since(&t0);
    CHECK(ms >= 250 && ms < 600);
    CHECK(most_close() == 0);
    fake_dme_stop();
    alarm(0);
}

static void test_request_wait_cap_512(void)
{
    static const uint8_t rq[] = { 0x19, 0x02, 0xAF };
    static uint8_t big[513], resp[512];
    size_t rlen = 0;
    int i;
    alarm(15);
    big[0] = 0x59; big[1] = 0x02; big[2] = 0xAF;
    for (i = 3; i < 513; i++) big[i] = (uint8_t)i;
    CHECK(raw_request("1902AF", rq, sizeof rq, big, 512, 512, 500, resp, &rlen) == MOST_REPLY);
    CHECK(rlen == 512 && !memcmp(resp, big, 512));
    CHECK(raw_request("1902AF", rq, sizeof rq, big, 513, 1024, 300, resp, &rlen) == MOST_TIMEOUT);
    CHECK(raw_request("1902AF", rq, sizeof rq, big, 65, 64, 300, resp, &rlen) == MOST_TIMEOUT);
    alarm(0);
}

static void test_raw_obd_reply(void)
{
    static const uint8_t rq[] = { 0x01, 0x0C }, reply[] = { 0x41, 0x0C, 0x1A, 0xF8 };
    uint8_t resp[64];
    size_t rlen = 0;
    alarm(10);
    CHECK(raw_request("010C", rq, sizeof rq, reply, sizeof reply, sizeof resp, 500, resp, &rlen) == MOST_REPLY);
    CHECK(rlen == 4 && !memcmp(resp, reply, 4));
    alarm(0);
}

static void test_raw_negative(void)
{
    static const uint8_t rq[] = { 0x2F, 0x01, 0x03 }, reply[] = { 0x7F, 0x2F, 0x31 };
    uint8_t resp[64];
    size_t rlen = 0;
    alarm(10);
    CHECK(raw_request("2F01", rq, sizeof rq, reply, sizeof reply, sizeof resp, 500, resp, &rlen) == MOST_REPLY);
    CHECK(rlen == 3 && !memcmp(resp, reply, 3));
    alarm(0);
}

/* A late positive reply to an earlier PID or subfunction is not this request's: it is waited past. */
static const uint8_t *g_late, *g_right;
static size_t g_late_n, g_right_n;
static void late_then_right(void)
{
    fake_dme_deliver_tunnel_reply(0x050, g_late, g_late_n);
    sleep_us(20000);
    fake_dme_deliver_tunnel_reply(0x050, g_right, g_right_n);
}

static void echo_case(const uint8_t *rq, size_t rqn, const uint8_t *late, size_t ln, const uint8_t *right,
                      size_t rn)
{
    uint8_t resp[64];
    size_t rlen = 0;
    pthread_t t;
    alarm(10);
    setup(fake_dme_hs_up, late_then_right);
    g_late = late; g_late_n = ln; g_right = right; g_right_n = rn;
    CHECK(most_open(1000) == MOST_UP);
    pthread_create(&t, NULL, driver, NULL);
    CHECK(most_request_wait(MOST_ECU_DME, rq, rqn, resp, sizeof resp, &rlen, 1000) == MOST_REPLY);
    pthread_join(t, NULL);
    CHECK(rlen == rn && !memcmp(resp, right, rn));
    CHECK(most_close() == 0);
    alarm(0);
}

static void test_pid_and_subfunction_echo(void)
{
    static const uint8_t kph[] = { 0x01, 0x0D }, rpm_reply[] = { 0x41, 0x0C, 0x1A, 0xF8 },
                         kph_reply[] = { 0x41, 0x0D, 0x3C };
    static const uint8_t codes[] = { 0x19, 0x02, 0xAF }, other[] = { 0x59, 0x0A, 0xFF },
                         codes_reply[] = { 0x59, 0x02, 0xFF, 0x12, 0x34, 0x56, 0x2F };
    echo_case(kph, sizeof kph, rpm_reply, sizeof rpm_reply, kph_reply, sizeof kph_reply);
    echo_case(codes, sizeof codes, other, sizeof other, codes_reply, sizeof codes_reply);
}

/* most_request still sends the pinned tunnel record and takes the open's timeout. */
static void test_most_request_unchanged(void)
{
    static const uint8_t data[2] = { 0x31, 0xE5 };
    static const uint8_t want[] = { 0x63, 0xF0, 0x31, 0x01, 0x0F, 0x0B, 0x12, 0x00, 0x03, 0x22, 0x42, 0x05 };
    uint8_t resp[64];
    size_t rlen = 0;
    struct timespec t0;
    int i;
    alarm(10);
    fake_dsi_reset();
    fake_dme_start(FAKE_UP);
    fake_dme_plain(0x4205, data, sizeof data);
    CHECK(most_open(2000) == MOST_UP);
    CHECK(most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_REPLY && got_ok(resp, rlen));
    i = req_index(0x02);
    CHECK(i >= 0 && fake_req[i].w[fake_req[i].n - 1].size == (int)sizeof want &&
          !memcmp(fake_req[i].w[fake_req[i].n - 1].bytes, want, sizeof want));
    fake_dme_silent_window(2, 1);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK(most_request(MOST_ECU_DME, UDS_READ, 3, resp, sizeof resp, &rlen) == MOST_TIMEOUT);
    CHECK(ms_since(&t0) >= 1900 && ms_since(&t0) < 2600);
    CHECK(most_close() == 0);
    fake_dme_stop();
    alarm(0);
}

int main(void)
{
    signal(SIGALRM, on_alarm);
    test_missing_library();
    bind_rc = 0;
    test_close_without_client();
    test_wire_bytes();
    test_simple(fake_dme_hs_up, reply_ok, 1000, MOST_REPLY, 1);
    test_simple(fake_dme_hs_up, NULL, 150, MOST_TIMEOUT, 0);
    test_simple(fake_dme_hs_up, drop, 2000, MOST_CLOSED, 0);
    test_simple(fake_dme_hs_up, foreign_then_ok, 2000, MOST_REPLY, 1);
    test_simple(fake_dme_hs_up, stale_then_ok, 2000, MOST_REPLY, 1);
    test_simple(fake_dme_hs_up, pending_then_ok, 2000, MOST_REPLY, 1);
    test_simple(fake_dme_hs_up, route_refusal, 2000, MOST_NOROUTE, 0);
    test_simple(fake_dme_hs_up, reply_100, 300, MOST_TIMEOUT, 0);   /* over 64 inner bytes: dropped */
    test_nrc_is_a_reply();
    test_no_target();
    test_open_variants();
    test_close_order_and_twice();
    test_pending();
    test_close_during_wait();
    test_refusal_does_not_stick();
    test_many_reads_one_client();
    test_request_wait_honours_timeout();
    test_request_wait_cap_512();
    test_raw_obd_reply();
    test_raw_negative();
    test_most_request_unchanged();
    test_pid_and_subfunction_echo();
    printf("test-most: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
