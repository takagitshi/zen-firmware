/* SPDX-License-Identifier: MIT */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

#define PMW3610_READ_RETRY_MIN_MS 1
#define PMW3610_READ_RETRY_MAX_MS 64

struct pmw3610_report_accumulator {
    int64_t dx;
    int64_t dy;
    int64_t last_report_time;
    bool have_last_report;
    bool report_scheduled;
};

struct pmw3610_frame_retry {
    int16_t x;
    int16_t y;
    bool send_y;
};

static inline int16_t pmw3610_decode_delta12(uint8_t low, uint8_t high_nibble) {
    uint16_t raw = (uint16_t)low | (((uint16_t)high_nibble & 0x0fU) << 8);

    return (raw & 0x0800U) != 0U ? (int16_t)(raw - 0x1000U) : (int16_t)raw;
}

static inline uint8_t pmw3610_next_retry_delay(uint8_t current_ms) {
    return current_ms < (PMW3610_READ_RETRY_MAX_MS / 2)
               ? (uint8_t)(current_ms * 2)
               : PMW3610_READ_RETRY_MAX_MS;
}

static inline struct pmw3610_frame_retry
pmw3610_frame_retry_result(int16_t x, int16_t y, int x_error, int y_error) {
    bool have_x = x != 0;
    bool have_y = y != 0;
    bool send_y = have_y && (!have_x || x_error == 0);

    return (struct pmw3610_frame_retry){
        .x = have_x && x_error != 0 ? x : 0,
        .y = have_y && (!send_y || y_error != 0) ? y : 0,
        .send_y = send_y,
    };
}

static inline void pmw3610_report_accumulator_init(struct pmw3610_report_accumulator *state) {
    state->dx = 0;
    state->dy = 0;
    state->last_report_time = 0;
    state->have_last_report = false;
    state->report_scheduled = false;
}

static inline void pmw3610_report_accumulate(struct pmw3610_report_accumulator *state,
                                             int32_t dx, int32_t dy) {
    state->dx += dx;
    state->dy += dy;
}

static inline bool
pmw3610_report_prepare_schedule(struct pmw3610_report_accumulator *state, int64_t now,
                                int32_t interval_ms, int64_t *delay_ms) {
    if (state->report_scheduled || (state->dx == 0 && state->dy == 0)) {
        return false;
    }

    *delay_ms = 0;
    if (state->have_last_report) {
        int64_t elapsed = now - state->last_report_time;
        if (elapsed < interval_ms) {
            *delay_ms = interval_ms - elapsed;
        }
    }

    state->report_scheduled = true;
    return true;
}

static inline int16_t pmw3610_clamp_report_delta(int64_t value) {
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static inline bool pmw3610_report_take(struct pmw3610_report_accumulator *state, int64_t now,
                                       int16_t *dx, int16_t *dy) {
    state->report_scheduled = false;
    *dx = pmw3610_clamp_report_delta(state->dx);
    *dy = pmw3610_clamp_report_delta(state->dy);

    if (*dx == 0 && *dy == 0) {
        return false;
    }

    state->dx -= *dx;
    state->dy -= *dy;
    state->last_report_time = now;
    state->have_last_report = true;
    return true;
}
