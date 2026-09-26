/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <zen/pointer_acceleration.h>

static const struct zen_pointer_accel_curve curve = {
    .base_gain_milli = 750,
    .takeoff_speed = 32,
    .full_speed = 160,
    .max_gain_milli = 1500,
    .reference_interval_ms = 15,
    .idle_reset_ms = 60,
};

static void apply(struct zen_pointer_accel_state *state, int32_t x, int32_t y,
                  int64_t now_ms, int32_t *out_x, int32_t *out_y) {
    zen_pointer_accel_apply_frame(&curve, state, x, y, now_ms, 15, out_x, out_y);
}

static void apply_collected_over(struct zen_pointer_accel_state *state, int32_t x, int32_t y,
                                 int64_t now_ms, int64_t collection_elapsed_ms,
                                 int32_t *out_x, int32_t *out_y) {
    zen_pointer_accel_apply_frame(&curve, state, x, y, now_ms, collection_elapsed_ms,
                                  out_x, out_y);
}

static void test_vector_speed(void) {
    assert(zen_pointer_accel_vector_speed(0, 0) == 0);
    assert(zen_pointer_accel_vector_speed(3, 4) == 5);
    assert(zen_pointer_accel_vector_speed(-30, -40) == 50);
    assert(zen_pointer_accel_vector_speed(INT32_MIN, INT32_MIN) == 3037000499U);
}

static void test_speed_normalization_never_inflates_short_frames(void) {
    assert(zen_pointer_accel_normalize_speed(40, 15, 1) == 40);
    assert(zen_pointer_accel_normalize_speed(40, 15, 15) == 40);
    assert(zen_pointer_accel_normalize_speed(80, 15, 30) == 40);
    assert(zen_pointer_accel_normalize_speed(120, 15, 45) == 40);
}

static void test_curve_is_monotonic_and_bounded(void) {
    assert(zen_pointer_accel_multiplier(&curve, 0) == 750000);
    assert(zen_pointer_accel_multiplier(&curve, 32) == 750000);
    assert(zen_pointer_accel_multiplier(&curve, 160) == 1050000);
    assert(zen_pointer_accel_multiplier(&curve, 320) == 1275000);

    uint32_t previous = (uint32_t)curve.base_gain_milli * 1000U;
    for (uint32_t speed = 1; speed <= 100000; speed++) {
        const uint32_t current = zen_pointer_accel_multiplier(&curve, speed);
        assert(current >= previous);
        assert(current <= (uint32_t)curve.max_gain_milli * 1000U);
        previous = current;
    }
    assert(previous >= 1499000);
}

static uint64_t output_speed_milli(uint32_t speed) {
    return (uint64_t)speed * zen_pointer_accel_multiplier(&curve, speed);
}

static void test_output_gain_has_no_warping_spike(void) {
    uint64_t previous = output_speed_milli(0);

    /* The output-speed slope is capped near 1.5x, unlike sensitivity smoothstep. */
    for (uint32_t speed = 1; speed <= 4096; speed++) {
        const uint64_t current = output_speed_milli(speed);
        const uint64_t step = current - previous;
        assert(current >= previous);
        assert(step <= 1600000);
        previous = current;
    }
}

static void test_low_speed_remainder_preserves_motion(void) {
    struct zen_pointer_accel_state state;
    int32_t x;
    int32_t y;

    zen_pointer_accel_reset(&state);
    apply(&state, 1, 0, 1000, &x, &y);
    assert(x == 0 && y == 0);
    apply(&state, 1, 0, 1015, &x, &y);
    assert(x == 1 && y == 0);
    apply(&state, 1, 0, 1030, &x, &y);
    assert(x == 1 && y == 0);
    apply(&state, 1, 0, 1045, &x, &y);
    assert(x == 1 && y == 0);
    assert(state.x.remainder == 0);

    /* A direction reversal discards opposite-signed carry instead of bumping. */
    apply(&state, 1, 0, 1060, &x, &y);
    assert(x == 0);
    apply(&state, -1, 0, 1075, &x, &y);
    assert(x == 0);
    assert(state.x.remainder == -750000);
}

static void test_same_frame_gain_does_not_leak_to_next_frame(void) {
    struct zen_pointer_accel_state state;
    int32_t x;
    int32_t y;

    zen_pointer_accel_reset(&state);
    apply(&state, 320, 0, 1000, &x, &y);
    assert(x == 408 && y == 0);

    apply(&state, 4, 0, 1015, &x, &y);
    assert(x == 3 && y == 0);
}

static void test_vector_uses_one_gain_for_both_axes(void) {
    struct zen_pointer_accel_state state;
    int32_t x;
    int32_t y;

    zen_pointer_accel_reset(&state);
    apply(&state, 60, 80, 1000, &x, &y);
    assert(x > 0 && y > 0);
    const int32_t direction_error = x * 4 - y * 3;
    assert(direction_error >= -4 && direction_error <= 4);
}

static void test_delayed_frame_has_same_speed_class(void) {
    struct zen_pointer_accel_state regular;
    struct zen_pointer_accel_state delayed;
    int32_t regular_x;
    int32_t delayed_x;
    int32_t y;

    zen_pointer_accel_reset(&regular);
    zen_pointer_accel_reset(&delayed);
    apply(&regular, 1, 0, 1000, &regular_x, &y);
    apply(&delayed, 1, 0, 1000, &delayed_x, &y);
    apply(&regular, 80, 0, 1015, &regular_x, &y);
    apply_collected_over(&delayed, 160, 0, 1030, 30, &delayed_x, &y);

    assert(regular_x == 64);
    assert(delayed_x == 128);
}

static void test_idle_clears_fractional_state(void) {
    struct zen_pointer_accel_state state;
    int32_t x;
    int32_t y;

    zen_pointer_accel_reset(&state);
    apply(&state, 1, 0, 1000, &x, &y);
    assert(state.x.remainder == 750000);
    apply(&state, 1, 0, 1060, &x, &y);
    assert(x == 0);
    assert(state.x.remainder == 750000);
}

static void test_long_delayed_backlog_is_not_treated_as_a_fast_frame(void) {
    struct zen_pointer_accel_state state;
    int32_t x;
    int32_t y;

    zen_pointer_accel_reset(&state);
    apply(&state, 4, 0, 1000, &x, &y);
    assert(x == 3);

    /* 320 counts accumulated over 75ms normalize to 64 counts/15ms. */
    apply_collected_over(&state, 320, 0, 1075, 75, &x, &y);
    assert(x == 246);
    assert(x < 320);
}

static void test_fast_first_frame_after_idle_keeps_its_raw_speed(void) {
    struct zen_pointer_accel_state state;
    int32_t x;
    int32_t y;

    zen_pointer_accel_reset(&state);
    apply(&state, 4, 0, 1000, &x, &y);
    assert(x == 3);

    /* The frame itself was collected now, despite a long gap since motion. */
    apply_collected_over(&state, 320, 0, 2000, 0, &x, &y);
    assert(x == 408);
}

static void test_extreme_values_are_saturated_by_the_driver_boundary(void) {
    struct zen_pointer_accel_state state;
    int32_t x;
    int32_t y;

    zen_pointer_accel_reset(&state);
    apply(&state, INT32_MAX, INT32_MIN, 1000, &x, &y);
    assert(x == INT32_MAX);
    assert(y == INT32_MIN);
}

int main(void) {
    test_vector_speed();
    test_speed_normalization_never_inflates_short_frames();
    test_curve_is_monotonic_and_bounded();
    test_output_gain_has_no_warping_spike();
    test_low_speed_remainder_preserves_motion();
    test_same_frame_gain_does_not_leak_to_next_frame();
    test_vector_uses_one_gain_for_both_axes();
    test_delayed_frame_has_same_speed_class();
    test_idle_clears_fractional_state();
    test_long_delayed_backlog_is_not_treated_as_a_fast_frame();
    test_fast_first_frame_after_idle_keeps_its_raw_speed();
    test_extreme_values_are_saturated_by_the_driver_boundary();
    puts("pointer_acceleration_test: PASS");
    return 0;
}
