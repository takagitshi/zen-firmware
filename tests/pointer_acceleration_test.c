/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <zen/pointer_acceleration.h>

static const struct zen_pointer_accel_curve curve = {
    .takeoff_speed = 8,
    .full_speed = 64,
    .max_multiplier_milli = 2250,
};

static void test_vector_speed(void) {
    assert(zen_pointer_accel_vector_speed(0, 0) == 0);
    assert(zen_pointer_accel_vector_speed(3, 4) == 5);
    assert(zen_pointer_accel_vector_speed(-3, -4) == 5);
    assert(zen_pointer_accel_vector_speed(30, 40) == 50);
}

static void test_curve(void) {
    assert(zen_pointer_accel_multiplier(&curve, 0) == 1000);
    assert(zen_pointer_accel_multiplier(&curve, 8) == 1000);
    assert(zen_pointer_accel_multiplier(&curve, 64) == 2250);
    assert(zen_pointer_accel_multiplier(&curve, 1000) == 2250);

    uint16_t previous = 1000;
    for (uint32_t speed = 9; speed <= 64; speed++) {
        const uint16_t current = zen_pointer_accel_multiplier(&curve, speed);
        assert(current >= previous);
        previous = current;
    }

    assert(zen_pointer_accel_multiplier(&curve, 32) >= 1450);
    assert(zen_pointer_accel_multiplier(&curve, 32) <= 1550);
    assert(zen_pointer_accel_multiplier(&curve, 48) >= 1950);
    assert(zen_pointer_accel_multiplier(&curve, 48) <= 2050);
}

static void test_low_speed_is_exact(void) {
    struct zen_pointer_accel_axis_state state = {.extra_milli = 875};

    assert(zen_pointer_accel_scale_axis(&state, 1, 1000) == 1);
    assert(zen_pointer_accel_scale_axis(&state, -1, 1000) == -1);
    assert(state.extra_milli == 875);
}

static void test_fractional_extra_is_preserved(void) {
    struct zen_pointer_accel_axis_state state;
    zen_pointer_accel_axis_reset(&state);

    assert(zen_pointer_accel_scale_axis(&state, 1, 1250) == 1);
    assert(zen_pointer_accel_scale_axis(&state, 1, 1250) == 1);
    assert(zen_pointer_accel_scale_axis(&state, 1, 1250) == 1);
    assert(zen_pointer_accel_scale_axis(&state, 1, 1250) == 2);
    assert(state.extra_milli == 0);

    assert(zen_pointer_accel_scale_axis(&state, -1, 1250) == -1);
    assert(zen_pointer_accel_scale_axis(&state, -1, 1250) == -1);
    assert(zen_pointer_accel_scale_axis(&state, -1, 1250) == -1);
    assert(zen_pointer_accel_scale_axis(&state, -1, 1250) == -2);
    assert(state.extra_milli == 0);
}

static void test_same_gain_preserves_axes(void) {
    struct zen_pointer_accel_axis_state x;
    struct zen_pointer_accel_axis_state y;
    zen_pointer_accel_axis_reset(&x);
    zen_pointer_accel_axis_reset(&y);

    assert(zen_pointer_accel_scale_axis(&x, 30, 2000) == 60);
    assert(zen_pointer_accel_scale_axis(&y, 40, 2000) == 80);
    assert(zen_pointer_accel_scale_axis(&x, -30, 2000) == -60);
    assert(zen_pointer_accel_scale_axis(&y, -40, 2000) == -80);
}

static void test_each_frame_uses_its_own_vector_speed(void) {
    struct zen_pointer_accel_state state;
    int64_t x;
    int64_t y;
    zen_pointer_accel_reset(&state);

    zen_pointer_accel_apply(&curve, &state, 30, 40, &x, &y);
    assert(x > 30 && y > 40);

    /* A slow report immediately after a fast one remains exactly linear. */
    zen_pointer_accel_apply(&curve, &state, 3, 4, &x, &y);
    assert(x == 3 && y == 4);

    /* A fast report immediately after a slow one accelerates immediately. */
    zen_pointer_accel_apply(&curve, &state, 48, 0, &x, &y);
    assert(x >= 95 && y == 0);
}

static void test_large_accelerated_value_is_not_discarded(void) {
    struct zen_pointer_accel_axis_state state;
    zen_pointer_accel_axis_reset(&state);

    assert(zen_pointer_accel_scale_axis(&state, 32000, 2250) == 72000);
    assert(state.extra_milli == 0);

    assert(zen_pointer_accel_vector_speed(INT64_C(70000), 0) == UINT32_MAX);
}

static void test_extreme_values_saturate_without_overflow(void) {
    struct zen_pointer_accel_axis_state state;
    zen_pointer_accel_axis_reset(&state);

    assert(zen_pointer_accel_scale_axis(&state, INT64_MAX, 2250) == INT64_MAX);
    assert(state.extra_milli >= 0 && state.extra_milli < 1000);

    zen_pointer_accel_axis_reset(&state);
    assert(zen_pointer_accel_scale_axis(&state, INT64_MIN, 2250) == INT64_MIN);
    assert(state.extra_milli <= 0 && state.extra_milli > -1000);
}

int main(void) {
    test_vector_speed();
    test_curve();
    test_low_speed_is_exact();
    test_fractional_extra_is_preserved();
    test_same_gain_preserves_axes();
    test_each_frame_uses_its_own_vector_speed();
    test_large_accelerated_value_is_not_discarded();
    test_extreme_values_saturate_without_overflow();
    puts("pointer_acceleration_test: PASS");
    return 0;
}
