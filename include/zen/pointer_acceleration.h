/*
 * Copyright (c) 2026 Takashi Imai
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ZEN_POINTER_ACCEL_GAIN_ONE 1000000U

struct zen_pointer_accel_curve {
    uint16_t base_gain_milli;
    uint16_t takeoff_speed;
    uint16_t full_speed;
    uint16_t max_gain_milli;
    uint16_t reference_interval_ms;
    uint16_t idle_reset_ms;
};

struct zen_pointer_accel_axis_state {
    int32_t remainder;
};

struct zen_pointer_accel_state {
    struct zen_pointer_accel_axis_state x;
    struct zen_pointer_accel_axis_state y;
    int64_t last_frame_time_ms;
    bool have_frame_time;
};

uint32_t zen_pointer_accel_vector_speed(int32_t x, int32_t y);

uint32_t zen_pointer_accel_normalize_speed(uint32_t speed, uint16_t reference_interval_ms,
                                           int64_t elapsed_ms);

/*
 * Returns the sensitivity multiplier in millionths, produced by integrating a
 * continuous gain curve. The derivative of output speed is bounded by
 * max_gain_milli.
 */
uint32_t zen_pointer_accel_multiplier(const struct zen_pointer_accel_curve *curve,
                                      uint32_t speed);

void zen_pointer_accel_reset(struct zen_pointer_accel_state *state);

void zen_pointer_accel_apply_frame(const struct zen_pointer_accel_curve *curve,
                                   struct zen_pointer_accel_state *state, int32_t x, int32_t y,
                                   int64_t now_ms, int64_t collection_elapsed_ms,
                                   int32_t *out_x, int32_t *out_y);
