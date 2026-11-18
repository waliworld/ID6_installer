/* The shared HSFZ-then-MOST link against an HSFZ listener on loopback and the fake DSI and DME. Host only.
 *
 * Each case runs in its own forked process: the fake binds once per process, and the link's MOST state,
 * alarms and stop flag are process wide. The parent never starts a thread and bounds each case itself. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "diag_link.h"
#include "diagnostic_lease.h"
#include "gauge_core.h"
#include "fake_dme.h"
#include "fake_dsi.h"

extern int fake_most_binds;

static int failures;
#define CHECK(e) do { if (!(e)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); } } while (0)

static const unsigned char READ_4201[] = {0x22,0x42,0x01}, PFX_4201[] = {0x62,0x42,0x01};
static const unsigned char REPLY_4201[] = {0x62,0x42,0x01,0x63,0xca};

static void sleep_ms(int ms) { poll(0, 0, ms); }

static void start(struct diag_link *l, const char *fake, const char *host, unsigned port, int transport,
                  int kind, int session)
{
    static struct diag_link_cfg c;
    if (fake) setenv("ID6_FAKE_MOST", fake, 1);
    else unsetenv("ID6_FAKE_MOST");
    memset(&c, 0, sizeof c);
    c.host = host; c.port = port; c.transport = transport; c.kind = kind; c.dsi_name = "boostdeck";
    c.session = session;
    diag_link_init(l, &c);
}

static void finish(struct diag_link *l)
{
    diag_link_close(l);
    fake_dme_stop();
}

static int read_4201(struct diag_link *l, unsigned timeout_ms, unsigned char *reply)
{
    unsigned negative;
    return diag_link_request(l, 0x12, READ_4201, 3, PFX_4201, 3, reply, 64, timeout_ms, &negative);
}

static int listener(unsigned *port)
{
    struct sockaddr_in a;
    socklen_t n = sizeof a;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) || getsockname(fd, (struct sockaddr *)&a, &n)) {
        printf("FAIL listener\n");
        exit(1);
    }
    *port = ntohs(a.sin_port);
    return fd;
}

/* A port nothing listens on, so a connect is refused. */
static unsigned refused_port(void)
{
    unsigned port;
    close(listener(&port));
    return port;
}

/* A one-connection HSFZ gateway in its own process. It checks the request to DME 0x12 (none when uds is
 * null), answers, then waits for the close. Its exit status is 0 when the request matched. */
static pid_t hsfz_server(unsigned *port, const unsigned char *uds, unsigned n,
                         const unsigned char *reply, unsigned rn)
{
    int fd = listener(port);
    pid_t pid;
    listen(fd, 1);
    fflush(NULL);
    pid = fork();
    if (pid == 0) {
        unsigned char rq[64], out[64] = {0,0,0,0,0,1,0x12,0xf4};
        size_t got = 0;
        ssize_t k;
        int ok = 1, c = accept(fd, 0, 0);
        if (uds) {
            while (got < 8 + n && (k = recv(c, rq + got, 8 + n - got, 0)) > 0) got += (size_t)k;
            ok = got == 8 + n && rq[3] == n + 2 && rq[5] == 1 && rq[6] == 0xf4 && rq[7] == 0x12 &&
                 !memcmp(rq + 8, uds, n);
            out[3] = (unsigned char)(rn + 2);
            memcpy(out + 8, reply, rn);
            if (send(c, out, 8 + rn, 0) != (ssize_t)(8 + rn)) ok = 0;
        }
        while (recv(c, rq, sizeof rq, 0) > 0) {}
        _exit(ok ? 0 : 1);
    }
    close(fd);
    return pid;
}

static int exited_ok(pid_t pid)
{
    int status = 0;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Another process holding the diagnostic lease until release_holder. */
static int holder_fd = -1;
static pid_t hold_lease(void)
{
    int ready[2], hold[2];
    char c = 0;
    pid_t pid;
    if (pipe(ready) || pipe(hold)) exit(1);
    fflush(NULL);
    pid = fork();
    if (pid == 0) {
        int lock = reserve_gateway();
        close(ready[0]); close(hold[1]);
        if (write(ready[1], lock >= 0 ? "y" : "n", 1) != 1) _exit(1);
        while (read(hold[0], &c, 1) > 0) {}
        _exit(0);
    }
    close(ready[1]); close(hold[0]);
    CHECK(read(ready[0], &c, 1) == 1 && c == 'y');
    close(ready[0]);
    holder_fd = hold[1];
    return pid;
}

static void release_holder(pid_t pid)
{
    close(holder_fd);
    CHECK(exited_ok(pid));
}

static void test_hsfz_ok_never_binds(void)
{
    struct diag_link l;
    unsigned char reply[64];
    unsigned port;
    pid_t server = hsfz_server(&port, READ_4201, 3, REPLY_4201, 5);
    start(&l, "up", "127.0.0.1", port, DIAG_AUTO, DIAG_LOOP, 0);
    CHECK(diag_link_up(&l) == 1);
    CHECK(!diag_link_on_most(&l));
    CHECK(read_4201(&l, 500, reply) == 5);
    CHECK(!memcmp(reply, REPLY_4201, 5));
    finish(&l);
    CHECK(exited_ok(server));
    CHECK(fake_most_binds == 0);
}

static void test_loop_switches_after_5(void)
{
    struct diag_link l;
    unsigned char reply[64];
    int i;
    start(&l, "up", "127.0.0.1", refused_port(), DIAG_AUTO, DIAG_LOOP, 0);
    for (i = 0; i < 4; ++i) CHECK(diag_link_up(&l) == 0);
    CHECK(fake_most_binds == 0);
    CHECK(diag_link_up(&l) == 1);
    CHECK(diag_link_on_most(&l));
    CHECK(read_4201(&l, 500, reply) == 5);
    CHECK(!memcmp(reply, REPLY_4201, 5));
    finish(&l);
}

static void test_oneshot_switches_after_1(void)
{
    struct diag_link l;
    unsigned char reply[64];
    start(&l, "up", "127.0.0.1", refused_port(), DIAG_AUTO, DIAG_ONESHOT, 0);
    CHECK(diag_link_reserve_oneshot(&l) == 1);
    CHECK(diag_link_up(&l) == 1);
    CHECK(diag_link_on_most(&l));
    CHECK(read_4201(&l, 500, reply) == 5);
    finish(&l);
}

static void test_no_host_goes_straight_to_most(void)
{
    struct diag_link l;
    unsigned char reply[64];
    start(&l, "up", 0, 6801, DIAG_AUTO, DIAG_LOOP, 0);
    CHECK(diag_link_up(&l) == 1);
    CHECK(diag_link_on_most(&l));
    CHECK(read_4201(&l, 500, reply) == 5);
    finish(&l);
}

static void test_transport_most_ignores_host(void)
{
    struct diag_link l;
    unsigned char reply[64];
    unsigned port;
    pid_t server = hsfz_server(&port, 0, 0, 0, 0);
    start(&l, "up", "127.0.0.1", port, DIAG_MOST, DIAG_LOOP, 0);
    CHECK(diag_link_up(&l) == 1);
    CHECK(diag_link_on_most(&l));
    CHECK(read_4201(&l, 500, reply) == 5);
    finish(&l);
    kill(server, SIGTERM);
    waitpid(server, 0, 0);
}

static void test_transport_hsfz_never_most(void)
{
    struct diag_link l;
    int i;
    start(&l, "up", "127.0.0.1", refused_port(), DIAG_HSFZ, DIAG_LOOP, 0);
    for (i = 0; i < 10; ++i) CHECK(diag_link_up(&l) == 0);
    CHECK(!diag_link_on_most(&l));
    CHECK(fake_most_binds == 0);
    finish(&l);
}

static void test_no_library_disables_most(void)
{
    struct diag_link l;
    int i;
    start(&l, 0, 0, 6801, DIAG_AUTO, DIAG_LOOP, 0);
    for (i = 0; i < 3; ++i) CHECK(diag_link_up(&l) == 0);
    CHECK(fake_most_binds == 1);
    finish(&l);
}

static void test_most_nrc_is_minus2(void)
{
    static const unsigned char rq[] = {0x2f,0x01,0x00}, prefix[] = {0x6f,0x01,0x00};
    struct diag_link l;
    unsigned char reply[64];
    unsigned negative = 0;
    start(&l, "mhd-refuse", 0, 6801, DIAG_AUTO, DIAG_ONESHOT, 0);
    CHECK(diag_link_reserve_oneshot(&l) == 1);
    CHECK(diag_link_up(&l) == 1);
    CHECK(diag_link_request(&l, 0x12, rq, 3, prefix, 3, reply, sizeof reply, 1600, &negative) == -2);
    CHECK(negative == 0x31);
    CHECK(!diag_link_lost(&l));
    finish(&l);
}

static void test_most_slot_echo(void)
{
    static const unsigned char rq[] = {0x2f,0x01,0x02}, prefix[] = {0x6f,0x01,0x02};
    static const unsigned char rpm[] = {0x01,0x0c}, rpm_prefix[] = {0x41,0x0c};
    struct diag_link l;
    unsigned char reply[64];
    unsigned negative;
    start(&l, "up", 0, 6801, DIAG_AUTO, DIAG_ONESHOT, 0);
    CHECK(diag_link_reserve_oneshot(&l) == 1);
    CHECK(diag_link_up(&l) == 1);
    CHECK(diag_link_request(&l, 0x12, rq, 3, prefix, 3, reply, sizeof reply, 1600, &negative) == 3);
    CHECK(diag_link_request(&l, 0x12, rpm, 2, rpm_prefix, 2, reply, sizeof reply, 250, &negative) == 4);
    CHECK(reply[2] == 0x1a && reply[3] == 0xf8);
    /* a positive reply to another request is not this one's */
    CHECK(diag_link_request(&l, 0x12, rpm, 2, prefix, 3, reply, sizeof reply, 250, &negative) == -3);
    finish(&l);
}

static void test_most_timeout_is_minus3_and_kept(void)
{
    struct diag_link l;
    unsigned char reply[64];
    uint64_t t0;
    start(&l, "up", 0, 6801, DIAG_AUTO, DIAG_LOOP, 0);
    CHECK(diag_link_up(&l) == 1);
    fake_dme_silent_window(1, 1);
    t0 = bd_mono_ms();
    CHECK(read_4201(&l, 250, reply) == -3);
    CHECK(bd_mono_ms() - t0 >= 1900);   /* max(timeout_ms, 2000) */
    CHECK(read_4201(&l, 250, reply) == 5);
    CHECK(!diag_link_lost(&l));
    CHECK(diag_link_up(&l) == 1);
    diag_link_drop(&l);   /* nothing on MOST */
    CHECK(diag_link_on_most(&l) && diag_link_up(&l) == 1);
    CHECK(read_4201(&l, 250, reply) == 5);
    finish(&l);
}

static void test_most_closed_is_minus1_lost(void)
{
    struct diag_link l;
    unsigned char reply[64];
    int i;
    start(&l, "drop", 0, 6801, DIAG_AUTO, DIAG_LOOP, 0);
    CHECK(diag_link_up(&l) == 1);
    for (i = 0; i < 4; ++i) CHECK(read_4201(&l, 500, reply) == 5);
    CHECK(!diag_link_lost(&l));
    CHECK(read_4201(&l, 500, reply) == -1);
    CHECK(diag_link_lost(&l));
    CHECK(diag_link_up(&l) == 0);
    finish(&l);
}

static void test_egs_on_most_is_minus3(void)
{
    static const unsigned char rq[] = {0x22,0xda,0x12}, prefix[] = {0x62,0xda,0x12};
    struct diag_link l;
    unsigned char reply[64];
    unsigned negative;
    uint64_t t0;
    start(&l, "up", 0, 6801, DIAG_AUTO, DIAG_LOOP, 0);
    CHECK(diag_link_up(&l) == 1);
    t0 = bd_mono_ms();
    CHECK(diag_link_request(&l, 0x18, rq, 3, prefix, 3, reply, sizeof reply, 500, &negative) == -3);
    CHECK(bd_mono_ms() - t0 < 100);
    CHECK(fake_dme_count("22DA12") == 0);
    CHECK(!diag_link_lost(&l));
    finish(&l);
}

static void *late_reply(void *arg)
{
    (void)arg;
    while (fake_dme_count("224201") < 1) sleep_ms(1);
    sleep_ms(1000);
    fake_dme_deliver_tunnel_reply(0x050, REPLY_4201, sizeof REPLY_4201);
    return NULL;
}

static void test_most_wait_floor(void)
{
    struct diag_link l;
    unsigned char reply[64];
    pthread_t t;
    uint64_t t0;
    start(&l, "up", 0, 6801, DIAG_AUTO, DIAG_LOOP, 0);
    CHECK(diag_link_up(&l) == 1);
    fake_dme_silent_window(1, 1);
    CHECK(pthread_create(&t, 0, late_reply, 0) == 0);
    t0 = bd_mono_ms();
    CHECK(read_4201(&l, 250, reply) == 5);
    CHECK(bd_mono_ms() - t0 >= 950);
    pthread_join(t, 0);
    finish(&l);
}

static void test_failed_open_retries_after_30s(void)
{
    struct diag_link l;
    uint64_t t0;
    start(&l, "no-server", 0, 6801, DIAG_AUTO, DIAG_LOOP, 0);
    CHECK(diag_link_up(&l) == 0);
    t0 = bd_mono_ms();
    CHECK(l.most_retry_at > t0 + 29500 && l.most_retry_at <= t0 + 30000);
    CHECK(diag_link_up(&l) == 0);
    CHECK(bd_mono_ms() - t0 < 100);   /* no second open inside the 30 s */
    CHECK(fake_client_inits == 1);
    CHECK(fake_free_calls == 1);      /* the failed open was closed */
    finish(&l);
}

static void test_oneshot_failed_open_is_final(void)
{
    struct diag_link l;
    start(&l, "no-server", "127.0.0.1", refused_port(), DIAG_AUTO, DIAG_ONESHOT, 0);
    CHECK(diag_link_reserve_oneshot(&l) == 1);
    CHECK(diag_link_up(&l) == 0);
    l.most_retry_at = 0;
    CHECK(diag_link_up(&l) == 0);
    CHECK(fake_client_inits == 1);
    finish(&l);
}

static void test_loop_busy_lease_minus3(void)
{
    struct diag_link l;
    unsigned char reply[64];
    uint64_t t0, dt;
    int probe;
    pid_t holder = hold_lease();
    start(&l, "up", 0, 6801, DIAG_AUTO, DIAG_LOOP, 0);
    CHECK(diag_link_up(&l) == 1);
    t0 = bd_mono_ms();
    CHECK(read_4201(&l, 250, reply) == -3);
    dt = bd_mono_ms() - t0;
    CHECK(dt >= 250 && dt <= 450);
    CHECK(fake_dme_count("22") == 0);
    release_holder(holder);
    CHECK(read_4201(&l, 250, reply) == 5);
    probe = reserve_gateway();   /* the lease is held only for the request */
    CHECK(probe >= 0);
    close(probe);
    finish(&l);
}

static void test_oneshot_busy_waits_2s(void)
{
    struct diag_link l;
    uint64_t t0, dt;
    int probe;
    pid_t holder = hold_lease();
    start(&l, "up", 0, 6801, DIAG_AUTO, DIAG_ONESHOT, 0);
    t0 = bd_mono_ms();
    CHECK(diag_link_reserve_oneshot(&l) == 0);
    CHECK(errno == EADDRINUSE);
    dt = bd_mono_ms() - t0;
    CHECK(dt >= 1950 && dt <= 2300);
    release_holder(holder);
    CHECK(diag_link_reserve_oneshot(&l) == 1);
    CHECK(reserve_gateway() < 0);    /* held for the run */
    finish(&l);
    probe = reserve_gateway();
    CHECK(probe >= 0);
    close(probe);
}

static void test_hsfz_session_lease(void)
{
    struct diag_link l;
    unsigned port;
    pid_t server = hsfz_server(&port, 0, 0, 0, 0);
    int probe;
    uint64_t now;
    start(&l, "up", "127.0.0.1", port, DIAG_AUTO, DIAG_LOOP, 1);
    CHECK(diag_link_up(&l) == 1);
    CHECK(reserve_gateway() < 0 && errno == EADDRINUSE);
    now = bd_mono_ms();
    CHECK(diag_link_should_yield(&l, now) == 0);
    CHECK(diag_link_should_yield(&l, now + DIAGNOSTIC_LEASE_MS) == 1);
    probe = reserve_gateway();
    CHECK(probe >= 0);
    close(probe);
    CHECK(diag_link_up(&l) == 0);    /* the yield backs off */
    CHECK(exited_ok(server));
    CHECK(fake_most_binds == 0);
    finish(&l);
}

static void test_reply_cap(void)
{
    static const unsigned char rq[] = {0x19,0x02,0xaf}, prefix[] = {0x59,0x02};
    struct diag_link l;
    unsigned char reply[512];
    unsigned negative;
    start(&l, "codes-512", 0, 6801, DIAG_AUTO, DIAG_ONESHOT, 0);
    CHECK(diag_link_reserve_oneshot(&l) == 1);
    CHECK(diag_link_up(&l) == 1);
    CHECK(diag_link_request(&l, 0x12, rq, 3, prefix, 2, reply, sizeof reply, 1800, &negative) == 512);
    CHECK(reply[2] == 0xff && reply[3] == 0x01 && reply[6] == 0x08 && reply[3 + 4 * 126 + 2] == 126);
    CHECK(diag_link_request(&l, 0x12, rq, 3, prefix, 2, reply, 64, 250, &negative) == -3);
    finish(&l);
}

static void test_reply_over_cap(void)
{
    static const unsigned char rq[] = {0x19,0x02,0xaf}, prefix[] = {0x59,0x02};
    struct diag_link l;
    unsigned char reply[512];
    unsigned negative;
    start(&l, "codes-513", 0, 6801, DIAG_AUTO, DIAG_ONESHOT, 0);
    CHECK(diag_link_reserve_oneshot(&l) == 1);
    CHECK(diag_link_up(&l) == 1);
    CHECK(diag_link_request(&l, 0x12, rq, 3, prefix, 2, reply, sizeof reply, 1800, &negative) == -3);
    CHECK(!diag_link_lost(&l));
    finish(&l);
}

static void test_link_sigterm_during_request(void)
{
    int err[2], up[2], status = 0, waited;
    char buf[4096], c;
    size_t got = 0;
    ssize_t k;
    struct pollfd p;
    uint64_t t0;
    pid_t pid;
    if (pipe(err) || pipe(up)) exit(1);
    fflush(NULL);
    pid = fork();
    if (pid == 0) {
        struct diag_link l;
        unsigned char reply[64];
        dup2(err[1], 2);
        close(err[0]); close(up[0]);
        diag_link_signals();
        start(&l, "up", 0, 6801, DIAG_AUTO, DIAG_LOOP, 0);
        if (diag_link_up(&l) != 1) _exit(3);
        fake_dme_silent_window(1, -1);
        if (write(up[1], "u", 1) != 1) _exit(4);
        while (diag_running) {
            diag_link_tick(&l);
            read_4201(&l, 250, reply);
        }
        finish(&l);
        exit(0);
    }
    close(err[1]); close(up[1]);
    p.fd = up[0]; p.events = POLLIN; p.revents = 0;
    CHECK(poll(&p, 1, 8000) == 1 && read(up[0], &c, 1) == 1);
    sleep_ms(300);
    t0 = bd_mono_ms();
    kill(pid, SIGTERM);
    for (waited = 0; waited < 5000 && waitpid(pid, &status, WNOHANG) != pid; waited += 5) sleep_ms(5);
    CHECK(bd_mono_ms() - t0 < 3000);
    if (waited >= 5000) { kill(pid, SIGKILL); waitpid(pid, &status, 0); }
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    while (got < sizeof buf - 1 && (k = read(err[0], buf + got, sizeof buf - 1 - got)) > 0) got += (size_t)k;
    buf[got] = 0;
    CHECK(strstr(buf, "FAKE-MOST exit unregister_target=1 unregister_client=1 free=1") != 0);
    close(err[0]); close(up[0]);
}

/* Once HSFZ has connected, losing it never moves the process to MOST. */
static void test_hsfz_seen_never_most(void)
{
    struct diag_link l;
    unsigned port;
    int i;
    pid_t server = hsfz_server(&port, 0, 0, 0, 0);
    start(&l, "up", "127.0.0.1", port, DIAG_AUTO, DIAG_LOOP, 0);
    CHECK(diag_link_up(&l) == 1);
    diag_link_drop(&l);
    CHECK(exited_ok(server));
    for (i = 0; i < 12; ++i) CHECK(diag_link_up(&l) == 0);
    CHECK(l.hsfz_fails >= 10);
    CHECK(!diag_link_on_most(&l));
    CHECK(fake_most_binds == 0);
    finish(&l);
}

/* A loop worker that saw a waiter during its request lets it take the lease before its next one. */
static void test_loop_yields_to_waiter(void)
{
    struct diag_link l;
    unsigned char reply[64];
    int got[2];
    char c = 0;
    pid_t pid;
    uint64_t t0;
    start(&l, "up", 0, 6801, DIAG_AUTO, DIAG_LOOP, 0);
    CHECK(diag_link_up(&l) == 1);
    fake_dme_hold_ms(100);
    if (pipe(got)) exit(1);
    fflush(NULL);
    pid = fork();
    if (pid == 0) {   /* a one-shot worker: retries every 20 ms for up to 2 s */
        uint64_t until;
        int lock = -1;
        close(got[0]);
        sleep_ms(30);
        for (until = bd_mono_ms() + 2000; lock < 0 && bd_mono_ms() < until; sleep_ms(20))
            lock = reserve_gateway();
        if (write(got[1], lock >= 0 ? "y" : "n", 1) != 1) _exit(1);
        sleep_ms(10);
        _exit(lock >= 0 ? 0 : 1);
    }
    close(got[1]);
    CHECK(read_4201(&l, 250, reply) == 5);
    CHECK(l.most_yield_until > bd_mono_ms());
    t0 = bd_mono_ms();
    CHECK(read_4201(&l, 250, reply) == 5);   /* the yield is not a busy lease */
    CHECK(bd_mono_ms() - t0 >= 100);
    CHECK(read(got[0], &c, 1) == 1 && c == 'y');
    CHECK(exited_ok(pid));
    close(got[0]);
    finish(&l);
}

/* SIGTERM while the MOST open waits for a server ends the wait. */
static void test_sigterm_during_open(void)
{
    int up[2], status = 0, waited;
    char c;
    struct pollfd p;
    uint64_t t0;
    pid_t pid;
    if (pipe(up)) exit(1);
    fflush(NULL);
    pid = fork();
    if (pid == 0) {
        struct diag_link l;
        close(up[0]);
        diag_link_signals();
        start(&l, "no-server", 0, 6801, DIAG_AUTO, DIAG_LOOP, 0);
        if (write(up[1], "u", 1) != 1) _exit(4);
        while (diag_running) diag_link_up(&l);
        finish(&l);
        _exit(0);
    }
    close(up[1]);
    p.fd = up[0]; p.events = POLLIN; p.revents = 0;
    CHECK(poll(&p, 1, 8000) == 1 && read(up[0], &c, 1) == 1);
    sleep_ms(300);   /* inside the 2 s wait for a server */
    t0 = bd_mono_ms();
    kill(pid, SIGTERM);
    for (waited = 0; waited < 5000 && waitpid(pid, &status, WNOHANG) != pid; waited += 5) sleep_ms(5);
    CHECK(bd_mono_ms() - t0 < 1000);
    if (waited >= 5000) { kill(pid, SIGKILL); waitpid(pid, &status, 0); }
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(up[0]);
}

static int run(const char *name, void (*fn)(void))
{
    int status = 0, waited;
    pid_t pid;
    fflush(NULL);
    pid = fork();
    if (pid == 0) {
        fn();
        fflush(stdout);
        exit(failures != 0);
    }
    for (waited = 0; waited < 30000 && waitpid(pid, &status, WNOHANG) != pid; waited += 5) sleep_ms(5);
    if (waited >= 30000) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        printf("FAIL %s: timed out\n", name);
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        printf("FAIL %s (status 0x%x)\n", name, status);
        return 1;
    }
    printf("ok %s\n", name);
    return 0;
}

#define RUN(fn) (++tests, bad += run(#fn, fn))

int main(void)
{
    int tests = 0, bad = 0;
    RUN(test_hsfz_ok_never_binds);
    RUN(test_loop_switches_after_5);
    RUN(test_oneshot_switches_after_1);
    RUN(test_no_host_goes_straight_to_most);
    RUN(test_transport_most_ignores_host);
    RUN(test_transport_hsfz_never_most);
    RUN(test_no_library_disables_most);
    RUN(test_most_nrc_is_minus2);
    RUN(test_most_slot_echo);
    RUN(test_most_timeout_is_minus3_and_kept);
    RUN(test_most_closed_is_minus1_lost);
    RUN(test_egs_on_most_is_minus3);
    RUN(test_most_wait_floor);
    RUN(test_failed_open_retries_after_30s);
    RUN(test_oneshot_failed_open_is_final);
    RUN(test_loop_busy_lease_minus3);
    RUN(test_oneshot_busy_waits_2s);
    RUN(test_hsfz_session_lease);
    RUN(test_reply_cap);
    RUN(test_reply_over_cap);
    RUN(test_link_sigterm_during_request);
    RUN(test_hsfz_seen_never_most);
    RUN(test_loop_yields_to_waiter);
    RUN(test_sigterm_during_open);
    printf("test-diag-link: %d tests, %d failures\n", tests, bad);
    return bad != 0;
}
