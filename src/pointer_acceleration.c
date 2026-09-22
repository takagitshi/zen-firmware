/*
 * Copyright (c) 2026 Takashi Imai
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <limits.h>

#include <zen/pointer_acceleration.h>

static uint32_t integer_sqrt(uint64_t value) {
    uint64_t remainder = value;
    uint64_t root = 0;
    uint64_t bit = UINT64_C(1) << 62;

    while (bit > remainder) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (remainder >= root + bit) {
            remainder -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }

    return root > UINT32_MAX ? UINT32_MAX : (uint32_t)root;
}

static uint64_t magnitude_i64(int64_t value) {
    return value < 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
}

static int64_t saturating_add_i64(int64_t lhs, int64_t rhs) {
    if (rhs > 0 && lhs > INT64_MAX - rhs) {
        return INT64_MAX;
    }
    if (rhs < 0 && lhs < INT64_MIN - rhs) {
        return INT64_MIN;
    }
    return lhs + rhs;
}

static int64_t saturating_multiply_i64_u16(int64_t value, uint16_t factor) {
    if (factor == 0 || value == 0) {
        return 0;
    }
    if (value > 0 && value > INT64_MAX / factor) {
        return INT64_MAX;
    }
    if (value < 0 && value < INT64_MIN / factor) {
        return INT64_MIN;
    }
    return value * factor;
}

uint32_t zen_pointer_accel_vector_speed(int64_t x, int64_t y) {
    const uint64_t abs_x = magnitude_i64(x);
    const uint64_t abs_y = magnitude_i64(y);
    if (abs_x > UINT16_MAX || abs_y > UINT16_MAX) {
        return UINT32_MAX;
    }
    return integer_sqrt((abs_x * abs_x) + (abs_y * abs_y));
}

uint16_t zen_pointer_accel_multiplier(const struct zen_pointer_accel_curve *curve,
                                      uint32_t speed) {
    if (speed <= curve->takeoff_speed) {
        return ZEN_POINTER_ACCEL_MULTIPLIER_ONE;
    }
    if (speed >= curve->full_speed) {
        return curve->max_multiplier_milli;
    }

    const uint32_t span = curve->full_speed - curve->takeoff_speed;
    const uint32_t offset = speed - curve->takeoff_speed;
    const uint32_t t_milli = (offset * ZEN_POINTER_ACCEL_MULTIPLIER_ONE) / span;
    const uint64_t smooth_milli =
        ((uint64_t)t_milli * t_milli * (3000U - (2U * t_milli)) + 500000U) /
        1000000U;
    const uint32_t gain_span =
        curve->max_multiplier_milli - ZEN_POINTER_ACCEL_MULTIPLIER_ONE;

    return (uint16_t)(ZEN_POINTER_ACCEL_MULTIPLIER_ONE +
                      ((gain_span * smooth_milli + 500U) / 1000U));
}

void zen_pointer_accel_axis_reset(struct zen_pointer_accel_axis_state *state) {
    state->extra_milli = 0;
}

int64_t zen_pointer_accel_scale_axis(struct zen_pointer_accel_axis_state *state,
                                     int64_t value, uint16_t multiplier_milli) {
    if (multiplier_milli <= ZEN_POINTER_ACCEL_MULTIPLIER_ONE) {
        return value;
    }

    const uint16_t extra_multiplier =
        multiplier_milli - ZEN_POINTER_ACCEL_MULTIPLIER_ONE;
    state->extra_milli = saturating_add_i64(
        state->extra_milli, saturating_multiply_i64_u16(value, extra_multiplier));
    const int64_t extra = state->extra_milli / ZEN_POINTER_ACCEL_MULTIPLIER_ONE;
    state->extra_milli %= ZEN_POINTER_ACCEL_MULTIPLIER_ONE;

    return saturating_add_i64(value, extra);
}

void zen_pointer_accel_reset(struct zen_pointer_accel_state *state) {
    zen_pointer_accel_axis_reset(&state->x);
    zen_pointer_accel_axis_reset(&state->y);
}

void zen_pointer_accel_apply(const struct zen_pointer_accel_curve *curve,
                             struct zen_pointer_accel_state *state, int64_t x, int64_t y,
                             int64_t *out_x, int64_t *out_y) {
    const uint32_t speed = zen_pointer_accel_vector_speed(x, y);
    const uint16_t multiplier_milli = zen_pointer_accel_multiplier(curve, speed);

    *out_x = zen_pointer_accel_scale_axis(&state->x, x, multiplier_milli);
    *out_y = zen_pointer_accel_scale_axis(&state->y, y, multiplier_milli);
}
