#ifndef BOOSTDECK_CORE_H
#define BOOSTDECK_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { BD_BOOST, BD_AFR, BD_TIMING, BD_IAT, BD_TRANS, BD_RAIL, BD_OIL, BD_COOLANT,
#ifdef PERF_LAB
       BD_ETHANOL,
#endif
       BD_COUNT };
enum { BD_MAX_PACKET = 512, BD_STALE_MS = 2500, BD_TEMP_STALE_MS = 3500,
       BD_UDP_PORT = 42860 };
typedef struct {
    uint32_t seq;
    unsigned demo, valid;
    uint64_t sent_ms; /* sender CLOCK_MONOTONIC, shared by processes on the same QNX node */
    double value[BD_COUNT]; /* psi, gasoline-equivalent AFR, degrees, C, C, bar, bar, C */
    unsigned age_ms[BD_COUNT];
} bd_frame;
uint64_t bd_mono_ms(void);
int bd_decode_did(unsigned did, const unsigned char *data, size_t len, double *out);
int bd_encode(const bd_frame *f, char *out, size_t len);
int bd_parse(const char *data, size_t len, bd_frame *out);
int bd_is_fresh(const bd_frame *f, unsigned i, uint64_t elapsed_ms);
double bd_display_value(unsigned i, double value, int metric);
int bd_bar(unsigned i, double value); /* normalized 0..1000, independent of units */
const char *bd_unit(unsigned i, int metric);
#ifdef __cplusplus
}
#endif
#endif
