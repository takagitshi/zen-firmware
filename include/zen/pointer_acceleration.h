/*
 * Copyright (c) 2026 Takashi Imai
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#define ZEN_POINTER_ACCEL_MULTIPLIER_ONE 1000U

struct zen_pointer_accel_curve {
    uint16_t takeoff_speed;
    uint16_t full_speed;
    uint16_t max_multiplier_milli;
};

struct zen_pointer_accel_axis_state {
    int64_t extra_milli;
};

struct zen_pointer_accel_state {
    struct zen_pointer_accel_axis_state x;
    struct zen_pointer_accel_axis_state y;
};

uint32_t zen_pointer_accel_vector_speed(int64_t x, int64_t y);

uint16_t zen_pointer_accel_multiplier(const struct zen_pointer_accel_curve *curve,
                                      uint32_t speed);

void zen_pointer_accel_axis_reset(struct zen_pointer_accel_axis_state *state);

int64_t zen_pointer_accel_scale_axis(struct zen_pointer_accel_axis_state *state,
                                     int64_t value, uint16_t multiplier_milli);

void zen_pointer_accel_reset(struct zen_pointer_accel_state *state);

void zen_pointer_accel_apply(const struct zen_pointer_accel_curve *curve,
                             struct zen_pointer_accel_state *state, int64_t x, int64_t y,
                             int64_t *out_x, int64_t *out_y);
