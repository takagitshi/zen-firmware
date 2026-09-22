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
        return curve->base_multiplier_milli;
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
        curve->max_multiplier_milli - curve->base_multiplier_milli;

    return (uint16_t)(curve->base_multiplier_milli +
                      ((gain_span * smooth_milli + 500U) / 1000U));
}

void zen_pointer_accel_axis_reset(struct zen_pointer_accel_axis_state *state) {
    state->extra_milli = 0;
}

int64_t zen_pointer_accel_scale_axis(struct zen_pointer_accel_axis_state *state,
                                     int64_t value, uint16_t multiplier_milli) {
    if (multiplier_milli == ZEN_POINTER_ACCEL_MULTIPLIER_ONE) {
        state->extra_milli = 0;
        return value;
    }

    const int64_t whole = value / ZEN_POINTER_ACCEL_MULTIPLIER_ONE;
    const int64_t fraction = value % ZEN_POINTER_ACCEL_MULTIPLIER_ONE;
    const int64_t scaled_whole = saturating_multiply_i64_u16(whole, multiplier_milli);
    const int64_t fraction_milli =
        state->extra_milli + fraction * multiplier_milli;
    const int64_t scaled_fraction =
        fraction_milli / ZEN_POINTER_ACCEL_MULTIPLIER_ONE;
    state->extra_milli = fraction_milli % ZEN_POINTER_ACCEL_MULTIPLIER_ONE;

    return saturating_add_i64(scaled_whole, scaled_fraction);
}

static uint32_t normalize_speed(uint32_t speed, uint16_t reference_interval_ms,
                                int64_t elapsed_ms) {
    if (elapsed_ms <= 0) {
        return speed;
    }

    const uint64_t normalized =
        ((uint64_t)speed * reference_interval_ms + (uint64_t)elapsed_ms / 2U) /
        (uint64_t)elapsed_ms;
    return normalized > UINT32_MAX ? UINT32_MAX : (uint32_t)normalized;
}

static uint16_t smooth_multiplier(uint16_t current, uint16_t target,
                                  const struct zen_pointer_accel_curve *curve) {
    if (target <= curve->base_multiplier_milli) {
        return curve->base_multiplier_milli;
    }

    const uint16_t weight = target > current ? curve->attack_smoothing_milli
                                             : curve->release_smoothing_milli;
    const int32_t difference = (int32_t)target - current;
    int32_t step = (difference * weight) / ZEN_POINTER_ACCEL_MULTIPLIER_ONE;
    if (step == 0 && difference != 0) {
        step = difference > 0 ? 1 : -1;
    }
    return (uint16_t)((int32_t)current + step);
}

void zen_pointer_accel_reset(struct zen_pointer_accel_state *state,
                             const struct zen_pointer_accel_curve *curve) {
    zen_pointer_accel_axis_reset(&state->x);
    zen_pointer_accel_axis_reset(&state->y);
    state->frame_x = 0;
    state->frame_y = 0;
    state->frame_start_time_ms = 0;
    state->last_frame_time_ms = 0;
    state->multiplier_milli = curve->base_multiplier_milli;
    state->frame_active = false;
    state->have_frame_time = false;
}

int64_t zen_pointer_accel_process_axis(const struct zen_pointer_accel_curve *curve,
                                       struct zen_pointer_accel_state *state,
                                       enum zen_pointer_accel_axis axis, int64_t value,
                                       bool sync, int64_t now_ms) {
    if (state->frame_active &&
        (now_ms < state->frame_start_time_ms ||
         now_ms - state->frame_start_time_ms >= curve->idle_reset_ms)) {
        state->frame_x = 0;
        state->frame_y = 0;
        state->multiplier_milli = curve->base_multiplier_milli;
        zen_pointer_accel_axis_reset(&state->x);
        zen_pointer_accel_axis_reset(&state->y);
        state->frame_active = false;
    }

    if (!state->frame_active) {
        if (state->have_frame_time &&
            (now_ms < state->last_frame_time_ms ||
             now_ms - state->last_frame_time_ms >= curve->idle_reset_ms)) {
            state->multiplier_milli = curve->base_multiplier_milli;
            zen_pointer_accel_axis_reset(&state->x);
            zen_pointer_accel_axis_reset(&state->y);
        }
        state->frame_start_time_ms = now_ms;
        state->frame_active = true;
    }

    struct zen_pointer_accel_axis_state *axis_state;
    if (axis == ZEN_POINTER_ACCEL_AXIS_X) {
        state->frame_x = saturating_add_i64(state->frame_x, value);
        axis_state = &state->x;
    } else {
        state->frame_y = saturating_add_i64(state->frame_y, value);
        axis_state = &state->y;
    }

    const int64_t scaled =
        zen_pointer_accel_scale_axis(axis_state, value, state->multiplier_milli);

    if (sync) {
        const uint32_t raw_speed =
            zen_pointer_accel_vector_speed(state->frame_x, state->frame_y);
        uint32_t normalized_speed = raw_speed;
        if (state->have_frame_time) {
            const int64_t elapsed_ms = now_ms - state->last_frame_time_ms;
            if (elapsed_ms > 0 && elapsed_ms < curve->idle_reset_ms) {
                normalized_speed =
                    normalize_speed(raw_speed, curve->reference_interval_ms, elapsed_ms);
            }
        }

        const uint16_t target = zen_pointer_accel_multiplier(curve, normalized_speed);
        state->multiplier_milli = smooth_multiplier(state->multiplier_milli, target, curve);
        if (state->multiplier_milli == ZEN_POINTER_ACCEL_MULTIPLIER_ONE) {
            zen_pointer_accel_axis_reset(&state->x);
            zen_pointer_accel_axis_reset(&state->y);
        }
        state->frame_x = 0;
        state->frame_y = 0;
        state->frame_start_time_ms = 0;
        state->last_frame_time_ms = now_ms;
        state->have_frame_time = true;
        state->frame_active = false;
    }

    return scaled;
}
