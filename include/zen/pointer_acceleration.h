/*
 * Copyright (c) 2026 Takashi Imai
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ZEN_POINTER_ACCEL_MULTIPLIER_ONE 1000U

struct zen_pointer_accel_curve {
    uint16_t base_multiplier_milli;
    uint16_t takeoff_speed;
    uint16_t full_speed;
    uint16_t max_multiplier_milli;
    uint16_t attack_smoothing_milli;
    uint16_t release_smoothing_milli;
    uint16_t reference_interval_ms;
    uint16_t idle_reset_ms;
};

struct zen_pointer_accel_axis_state {
    int64_t extra_milli;
};

struct zen_pointer_accel_state {
    struct zen_pointer_accel_axis_state x;
    struct zen_pointer_accel_axis_state y;
    int64_t frame_x;
    int64_t frame_y;
    int64_t frame_start_time_ms;
    int64_t last_frame_time_ms;
    uint16_t multiplier_milli;
    bool frame_active;
    bool have_frame_time;
};

enum zen_pointer_accel_axis {
    ZEN_POINTER_ACCEL_AXIS_X,
    ZEN_POINTER_ACCEL_AXIS_Y,
};

uint32_t zen_pointer_accel_vector_speed(int64_t x, int64_t y);

uint16_t zen_pointer_accel_multiplier(const struct zen_pointer_accel_curve *curve,
                                      uint32_t speed);

void zen_pointer_accel_axis_reset(struct zen_pointer_accel_axis_state *state);

int64_t zen_pointer_accel_scale_axis(struct zen_pointer_accel_axis_state *state,
                                     int64_t value, uint16_t multiplier_milli);

void zen_pointer_accel_reset(struct zen_pointer_accel_state *state,
                             const struct zen_pointer_accel_curve *curve);

int64_t zen_pointer_accel_process_axis(const struct zen_pointer_accel_curve *curve,
                                       struct zen_pointer_accel_state *state,
                                       enum zen_pointer_accel_axis axis, int64_t value,
                                       bool sync, int64_t now_ms);
