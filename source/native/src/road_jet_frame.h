#ifndef ROAD_JET_FRAME_H
#define ROAD_JET_FRAME_H

#include <stdint.h>

enum {
    ROAD_JET_PORT = 56311,
    ROAD_JET_STALE_MS = 1700,
    ROAD_JET_MOST_STALE_MS = 2500,
    ROAD_JET_ROAD_BASE = 57300,
    ROAD_JET_ROAD_FRAMES = 12,
    ROAD_JET_SPRITE_BASE = 57320,
    ROAD_JET_HEIGHTS = 17,
    ROAD_JET_LANES = 5,
    ROAD_JET_NO_SPRITE = 57337
};
enum { ROAD_JET_WAITING, ROAD_JET_ACTIVE, ROAD_JET_PEDAL_ONLY, ROAD_JET_MOST_PEDAL };

struct road_jet_frame {
    uint64_t started_ms;
    int32_t mode;
    int32_t pedal;        /* tenths of a percent */
    int32_t steering;     /* hundredths of a degree, positive to the right */
};

#endif
