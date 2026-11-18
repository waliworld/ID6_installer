/* Bytes of the MOST UDS tunnel: RoutineControl 0x0F0B on FBlock 0x06 carrying one inner request. */
#ifndef MOST_PROTO_H
#define MOST_PROTO_H

#include <stddef.h>
#include <stdint.h>

/* [src][tgt] 31 01 <routine:2> <ecu> <len:2> <inner>, big-endian. Returns the length, or 0 if it won't fit. */
size_t most_tunnel_build(uint8_t *out, size_t cap, uint8_t src, uint8_t tgt, uint16_t routine,
                         uint8_t ecu, const uint8_t *inner, size_t inner_len);
/* Parses 71 01 <routine:2> <ecu> <len:2> <inner> (src/tgt already removed). 0 and the inner copied, or -1. */
int most_tunnel_parse(const uint8_t *p, size_t n, uint16_t routine, uint8_t *inner_out, size_t cap,
                      size_t *inner_len);

#endif
