/*
 * Copyright (c) 2026 Takashi Imai
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

enum zen_gesture_direction {
    ZEN_GESTURE_NONE,
    ZEN_GESTURE_LEFT,
    ZEN_GESTURE_RIGHT,
    ZEN_GESTURE_UP,
    ZEN_GESTURE_DOWN,
};

enum zen_gesture_axis {
    ZEN_GESTURE_AXIS_X,
    ZEN_GESTURE_AXIS_Y,
};

struct zen_gesture_state {
    int32_t x;
    int32_t y;
    int64_t cooldown_until_ms;
    bool cooling_down;
};

void zen_gesture_state_reset(struct zen_gesture_state *state);

enum zen_gesture_direction zen_gesture_state_update(struct zen_gesture_state *state,
                                                     enum zen_gesture_axis axis, int32_t value,
                                                     bool sync, int64_t now_ms, uint32_t threshold,
                                                     uint32_t cooldown_ms);
