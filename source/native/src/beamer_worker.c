#define _POSIX_C_SOURCE 200809L
#include "diag_link.h"
#include "gauge_core.h"

#include <poll.h>
#include <string.h>
#include <unistd.h>

enum { FOG = 1, HIGH = 2, POSITION = 4, REVERSE = 8, QUICK = 16,
       PULSE_MS = 160, PERIOD_MS = 500, QUICK_MS = 250 };
static const unsigned char lamp_functions[] = {0x08, 0x05, 0x01, 0x0f};

/* One pulse per selected lamp in a step. Each is independently timed by the FEM. */
static const unsigned char patterns[][22] = {
    {FOG, HIGH, 0, FOG, HIGH, 0},
    {POSITION, REVERSE, 0, POSITION, REVERSE, 0},
    {FOG | POSITION, HIGH | REVERSE, 0, FOG | REVERSE, HIGH | POSITION, 0},
    {FOG, REVERSE, HIGH | POSITION, 0, FOG | REVERSE,
     HIGH | QUICK, HIGH | QUICK, POSITION, FOG | POSITION, HIGH | REVERSE, 0,
     REVERSE, FOG, POSITION | REVERSE, HIGH | QUICK, HIGH | QUICK,
     FOG | POSITION, REVERSE, 0, HIGH | POSITION, FOG | REVERSE, 0}
};
static const unsigned lengths[] = {6, 6, 6, sizeof patterns[0]};
static volatile sig_atomic_t cancelled;

static void stop(int sig) {
    (void)sig;
    cancelled = 1;
}

static int emit(unsigned char value) {
    return write(STDOUT_FILENO, &value, 1) == 1;
}

static int request(struct diag_link *link, unsigned ecu, const unsigned char *req, unsigned length,
                   const unsigned char *prefix, unsigned prefix_length, unsigned char *reply) {
    unsigned negative;
    return diag_link_request(link, ecu, req, length, prefix, prefix_length, reply, 64, 250, &negative);
}

static int supported(struct diag_link *link) {
    const unsigned char identity[] = {0x22, 0xf1, 0x50};
    const unsigned char expected[] = {0x62, 0xf1, 0x50, 0x0f, 0x16, 0xb0};
    const unsigned char capability[] = {0x22, 0xd5, 0x42}, positive[] = {0x62, 0xd5, 0x42};
    unsigned char reply[64];
    return request(link, 0x40, identity, sizeof identity, expected, sizeof expected, reply) == sizeof expected &&
           request(link, 0x40, capability, sizeof capability, positive, sizeof positive, reply) > 3;
}

/* Return an explicit stop reason if either signal cannot confirm Park and zero speed. */
static unsigned char parking_status(struct diag_link *link) {
    const unsigned char gear[] = {0x22, 0xda, 0x2e}, gear_ok[] = {0x62, 0xda, 0x2e};
    const unsigned char speed[] = {0x01, 0x0d}, speed_ok[] = {0x41, 0x0d};
    unsigned char reply[64];
    if (request(link, 0x18, gear, sizeof gear, gear_ok, sizeof gear_ok, reply) != 5)
        return 'T';
    if (reply[3] != 0 || reply[4] != 6) return 'P';
    if (request(link, 0x12, speed, sizeof speed, speed_ok, sizeof speed_ok, reply) != 3)
        return 'V';
    return reply[2] == 0 ? 0 : 'M';
}

enum PulseResult { PULSE_FAILED, PULSE_ACCEPTED, PULSE_UNSUPPORTED };

static enum PulseResult pulse(struct diag_link *link, unsigned lamp) {
    const unsigned char req[] = {0x2e, 0xd5, 0x42, 0, lamp, 0, PULSE_MS / 10};
    const unsigned char positive[] = {0x6e, 0xd5, 0x42};
    unsigned char reply[64];
    unsigned negative;
    if (cancelled) return PULSE_FAILED;
    int n = diag_link_request(link, 0x40, req, sizeof req, positive, sizeof positive,
                              reply, sizeof reply, 250, &negative);
    if (n == sizeof positive) return PULSE_ACCEPTED;
    if (n == -2 && negative == 0x31) return PULSE_UNSUPPORTED;
    return PULSE_FAILED;
}

static void wait_until(uint64_t deadline) {
    uint64_t now;
    while (!cancelled && (now = bd_mono_ms()) < deadline) poll(0, 0, deadline - now);
}

static unsigned char show(struct diag_link *link, unsigned pattern) {
    unsigned unavailable = 0;
    int accepted = 0;
    if (cancelled || !emit('S')) return 0;

    for (unsigned i = 0; i < lengths[pattern] && !cancelled; ++i) {
        unsigned beat = patterns[pattern][i];
        unsigned mask = (beat & ~QUICK) & ~unavailable;
        unsigned active = 0;
        uint64_t step = bd_mono_ms();
        unsigned char blocked = parking_status(link);
        if (blocked) return blocked;
        for (unsigned index = 0; index < sizeof lamp_functions; ++index) {
            unsigned lamp = 1u << index;
            if (!(mask & lamp)) continue;
            enum PulseResult result = pulse(link, lamp_functions[index]);
            if (result == PULSE_FAILED) return 'N';
            if (result == PULSE_UNSUPPORTED) {
                unavailable |= lamp;
                continue;
            }
            active |= lamp;
            accepted = 1;
        }
        if (cancelled || !emit(active)) return 0;
        wait_until(bd_mono_ms() + PULSE_MS);
        if (cancelled || !emit(0)) return 0;
        wait_until(step + ((beat & QUICK) ? QUICK_MS : PERIOD_MS));
    }
    return accepted ? (unavailable ? 'K' : 'D') : 'L';
}

int main(int argc, char **argv) {
    if (argc != 4 || strcmp(argv[1], "--host") ||
        strlen(argv[3]) != 1 || argv[3][0] < '0' || argv[3][0] > '3') return 2;

    struct diag_link_cfg config = {argv[2], 6801, DIAG_HSFZ, DIAG_ONESHOT, 0, 0};
    struct diag_link link;
    diag_link_signals();
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, 0);
    sigaction(SIGINT, &action, 0);
    diag_link_init(&link, &config);
    unsigned char result;
    if (!diag_link_reserve_oneshot(&link)) result = 'B';
    else if (!diag_link_up(&link)) result = 'G';
    else if (cancelled) result = 0;
    else if (!supported(&link)) result = 'U';
    else if ((result = parking_status(&link)) != 0) {
        /* Neither the diagnostic session nor the lamps have been changed. */
    }
    else {
        unsigned char reply[64];
        const unsigned char extended[] = {0x10, 0x03}, extended_ok[] = {0x50, 0x03};
        const unsigned char normal[] = {0x10, 0x01}, normal_ok[] = {0x50, 0x01};
        if (cancelled) result = 0;
        else {
            int session = request(&link, 0x40, extended, sizeof extended, extended_ok, sizeof extended_ok, reply);
            result = session < 2 ? 'E' : show(&link, argv[3][0] - '0');
            /* Even a timed-out session request may have reached the FEM. */
            (void)request(&link, 0x40, normal, sizeof normal, normal_ok, sizeof normal_ok, reply);
        }
    }
    if (result && !cancelled) (void)emit(result);
    diag_link_close(&link);
    return 0;
}
