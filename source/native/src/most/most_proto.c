#include <string.h>
#include "most_proto.h"

size_t most_tunnel_build(uint8_t *out, size_t cap, uint8_t src, uint8_t tgt, uint16_t routine,
                         uint8_t ecu, const uint8_t *inner, size_t inner_len)
{
    if (inner_len > 0xFFFFu || inner_len > cap || cap - inner_len < 9) return 0;
    out[0] = src; out[1] = tgt;
    out[2] = 0x31; out[3] = 0x01;
    out[4] = (uint8_t)(routine >> 8); out[5] = (uint8_t)routine;
    out[6] = ecu;
    out[7] = (uint8_t)(inner_len >> 8); out[8] = (uint8_t)inner_len;
    if (inner_len) memcpy(out + 9, inner, inner_len);
    return inner_len + 9;
}

int most_tunnel_parse(const uint8_t *p, size_t n, uint16_t routine, uint8_t *inner_out, size_t cap,
                      size_t *inner_len)
{
    size_t len;
    *inner_len = 0;
    if (n < 7 || p[0] != 0x71 || p[1] != 0x01) return -1;
    if (p[2] != (uint8_t)(routine >> 8) || p[3] != (uint8_t)routine) return -1;
    len = ((size_t)p[5] << 8) | p[6];
    if (len > n - 7 || len > cap) return -1;
    if (len) memcpy(inner_out, p + 7, len);
    *inner_len = len;
    return 0;
}
