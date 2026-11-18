/* Host workers only: decides what the fake DSI does when a worker falls back to MOST.
 * ID6_FAKE_MOST unset: no library, as on a unit without DSI. "up": a DME answering the gauge DIDs, OBD
 * 01 0C / 01 0D, the MHD slot selector, and the fault code read and clear. "no-server": the library loads
 * but no server answers. "drop": the link is lost at the fifth read. "mhd-refuse": "up", but 2F gets
 * 7F 2F 31. "codes-512" and "codes-513": "up", but 19 02 returns 127 codes in 512 or 513 bytes.
 * ID6_FAKE_MOST_DELAY_MS (0-2000) slows each reply and the close by that much.
 * Each bind prints a marker the tests look for. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "most/dsi_abi.h"
#include "fake_dme.h"

int fake_most_binds;   /* most_fake_bind calls in this process */

static void report(void)
{
    fprintf(stderr, "FAKE-MOST exit unregister_target=%d unregister_client=%d free=%d\n",
            fake_dsi_req_count(0x05), fake_dsi_req_count(0x10), fake_free_calls);
}

/* 59 02, the status mask, 127 codes of four bytes, then padding to n bytes. */
static void long_codes(size_t n)
{
    uint8_t b[513];
    size_t i;
    memset(b, 0, sizeof b);
    b[0] = 0x59; b[1] = 0x02; b[2] = 0xFF;
    for (i = 0; i < 127; ++i) {
        b[3 + 4 * i] = 0x01; b[4 + 4 * i] = (uint8_t)(i >> 8); b[5 + 4 * i] = (uint8_t)i; b[6 + 4 * i] = 0x08;
    }
    fake_dme_raw("1902", b, n);
}

int most_fake_bind(void)
{
    static const struct { uint16_t did; uint8_t n, b[2]; } dids[] = {
        {0x4201,2,{0x63,0xca}}, {0x4205,2,{0x31,0xe5}}, {0x5889,2,{0x10,0x4e}}, {0x4a49,2,{0x00,0x4e}},
        {0x580f,1,{0x82,0}}, {0x56d7,2,{0x11,0x69}}, {0x586f,2,{0x09,0xc1}}, {0x4300,2,{0x0b,0xf8}}
    };
    static const uint8_t rpm[] = {0x41,0x0C,0x1A,0xF8}, kph[] = {0x41,0x0D,0x3C}, cleared[] = {0x54};
    static const uint8_t codes[] = {0x59,0x02,0xFF,0x12,0x34,0x56,0x2F,0x0A,0xBC,0xDE,0x08};
    const char *mode = getenv("ID6_FAKE_MOST"), *delay = getenv("ID6_FAKE_MOST_DELAY_MS");
    unsigned i;
    ++fake_most_binds;
    fprintf(stderr, "FAKE-MOST bind %s\n", mode ? mode : "unset");
    if (!mode) return -1;
    fake_dsi_reset();
    atexit(report);
    if (!strcmp(mode, "no-server")) { fake_dme_start(FAKE_NO_SERVER); return 0; }
    if (strcmp(mode, "up") && strcmp(mode, "drop") && strcmp(mode, "mhd-refuse") &&
        strcmp(mode, "codes-512") && strcmp(mode, "codes-513") &&
        strcmp(mode, "bm3-unknown")) return -1;
    fake_dme_start(FAKE_UP);
    if (delay && atoi(delay) > 0 && atoi(delay) <= 2000) fake_dme_hold_ms(atoi(delay));
    if (!strcmp(mode, "drop")) fake_dme_drop_link_after(5);
    if (!strcmp(mode, "mhd-refuse")) fake_dme_refuse("2F", 0x31, 0);
    if (!strcmp(mode, "codes-512")) long_codes(512);
    if (!strcmp(mode, "codes-513")) long_codes(513);
    for (i = 0; i < sizeof dids / sizeof dids[0]; ++i) fake_dme_plain(dids[i].did, dids[i].b, dids[i].n);
    fake_dme_raw("010C", rpm, sizeof rpm);
    fake_dme_raw("010D", kph, sizeof kph);
    for (i = 0; i < 4; ++i) {   /* 2F 01 <slot> echoes the slot */
        char hex[7];
        uint8_t echo[3] = {0x6F, 0x01, 0};
        echo[2] = (uint8_t)i;
        snprintf(hex, sizeof hex, "2F01%02X", i);
        fake_dme_raw(hex, echo, sizeof echo);
    }
    {
        static const uint8_t define[] = {0x6c, 0x02, 0xf3, 0x01};
        static const uint8_t map[] = {0x7d}, map_read[] = {0x63, 1};
        static const uint8_t sound[] = {0x6e, 0x5f, 0xfd};
        static const uint8_t flap_auto[] = {0x6f, 0x60, 0xfd, 0};
        static const uint8_t flap_adjust[] = {0x6f, 0x60, 0xfd, 3};
        uint8_t rom[258] = {0x62, 0xf3, 0x01};
        const char *profile = !strcmp(mode, "bm3-unknown") ? "BM3_R #UNKNOWN" : "BM3_R #011000";
        memcpy(rom + 3, profile, strlen(profile));
        fake_dme_raw("2C02F30124807FFE5000FF", define, sizeof define);
        fake_dme_raw("22F301", rom, sizeof rom);
        fake_dme_raw("3D14D001D006020101", map, sizeof map);
        fake_dme_raw("2314D001D00601", map_read, sizeof map_read);
        fake_dme_raw("2E5FFD00", sound, sizeof sound);
        fake_dme_raw("2F60FD00", flap_auto, sizeof flap_auto);
        fake_dme_raw("2F60FD0314FE", flap_adjust, sizeof flap_adjust);
        fake_dme_raw("2F60FD032328", flap_adjust, sizeof flap_adjust);
    }
    fake_dme_raw("1902AF", codes, sizeof codes);
    fake_dme_raw("14FFFFFF", cleared, sizeof cleared);
    return 0;
}
