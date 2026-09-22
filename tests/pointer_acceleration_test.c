/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <zen/pointer_acceleration.h>

static const struct zen_pointer_accel_curve curve = {
    .base_multiplier_milli = 1000,
    .takeoff_speed = 20,
    .full_speed = 112,
    .max_multiplier_milli = 2000,
    .attack_smoothing_milli = 500,
    .release_smoothing_milli = 750,
    .reference_interval_ms = 15,
    .idle_reset_ms = 60,
};

static int64_t process_x(struct zen_pointer_accel_state *state, int64_t value, bool sync,
                         int64_t now_ms) {
    return zen_pointer_accel_process_axis(&curve, state, ZEN_POINTER_ACCEL_AXIS_X, value, sync,
                                          now_ms);
}

static int64_t process_y(struct zen_pointer_accel_state *state, int64_t value, bool sync,
                         int64_t now_ms) {
    return zen_pointer_accel_process_axis(&curve, state, ZEN_POINTER_ACCEL_AXIS_Y, value, sync,
                                          now_ms);
}

static void test_vector_speed(void) {
    assert(zen_pointer_accel_vector_speed(0, 0) == 0);
    assert(zen_pointer_accel_vector_speed(3, 4) == 5);
    assert(zen_pointer_accel_vector_speed(-3, -4) == 5);
    assert(zen_pointer_accel_vector_speed(30, 40) == 50);
}

static void test_curve(void) {
    assert(zen_pointer_accel_multiplier(&curve, 0) == 1000);
    assert(zen_pointer_accel_multiplier(&curve, 20) == 1000);
    assert(zen_pointer_accel_multiplier(&curve, 112) == 2000);
    assert(zen_pointer_accel_multiplier(&curve, 1000) == 2000);

    uint16_t previous = 1000;
    for (uint32_t speed = 21; speed <= 112; speed++) {
        const uint16_t current = zen_pointer_accel_multiplier(&curve, speed);
        assert(current >= previous);
        previous = current;
    }

    assert(zen_pointer_accel_multiplier(&curve, 32) >= 1040);
    assert(zen_pointer_accel_multiplier(&curve, 32) <= 1050);
    assert(zen_pointer_accel_multiplier(&curve, 64) >= 1460);
    assert(zen_pointer_accel_multiplier(&curve, 64) <= 1480);
}

static void test_low_speed_is_exact(void) {
    struct zen_pointer_accel_axis_state state = {.extra_milli = 875};

    assert(zen_pointer_accel_scale_axis(&state, 1, 1000) == 1);
    assert(state.extra_milli == 0);
    assert(zen_pointer_accel_scale_axis(&state, -1, 1000) == -1);
    assert(state.extra_milli == 0);
}

static void test_fractional_output_is_preserved(void) {
    struct zen_pointer_accel_axis_state state;
    zen_pointer_accel_axis_reset(&state);

    assert(zen_pointer_accel_scale_axis(&state, 1, 1250) == 1);
    assert(zen_pointer_accel_scale_axis(&state, 1, 1250) == 1);
    assert(zen_pointer_accel_scale_axis(&state, 1, 1250) == 1);
    assert(zen_pointer_accel_scale_axis(&state, 1, 1250) == 2);
    assert(state.extra_milli == 0);

    assert(zen_pointer_accel_scale_axis(&state, -1, 750) == 0);
    assert(zen_pointer_accel_scale_axis(&state, -1, 750) == -1);
    assert(zen_pointer_accel_scale_axis(&state, -1, 750) == -1);
    assert(zen_pointer_accel_scale_axis(&state, -1, 750) == -1);
    assert(state.extra_milli == 0);
}

static void test_inline_frame_keeps_original_event_count(void) {
    struct zen_pointer_accel_state state;
    zen_pointer_accel_reset(&state, &curve);

    /* The first frame is unchanged and only selects gain for the next frame. */
    assert(process_x(&state, 30, false, 1000) == 30);
    assert(process_y(&state, 40, true, 1000) == 40);
    assert(state.multiplier_milli > 1000);

    const int64_t x = process_x(&state, 30, false, 1015);
    const int64_t y = process_y(&state, 40, true, 1015);
    assert(x > 30 && y > 40);
    const int64_t direction_error = x * 4 - y * 3;
    assert(direction_error >= -4 && direction_error <= 4);
}

static void test_gain_attack_is_smoothed_and_release_is_prompt(void) {
    struct zen_pointer_accel_state state;
    zen_pointer_accel_reset(&state, &curve);

    assert(process_x(&state, 112, true, 1000) == 112);
    assert(state.multiplier_milli == 1500);
    assert(process_x(&state, 112, true, 1015) == 168);
    assert(state.multiplier_milli == 1750);

    /* One already-started frame uses the prior gain; following frames return to exact 1.0x. */
    assert(process_x(&state, 5, true, 1030) > 5);
    assert(state.multiplier_milli == 1000);
    assert(process_x(&state, 5, true, 1045) == 5);
}

static void test_speed_is_normalized_for_scheduler_delay(void) {
    struct zen_pointer_accel_state regular;
    struct zen_pointer_accel_state delayed;
    zen_pointer_accel_reset(&regular, &curve);
    zen_pointer_accel_reset(&delayed, &curve);

    assert(process_x(&regular, 10, true, 1000) == 10);
    assert(process_x(&delayed, 10, true, 1000) == 10);
    assert(process_x(&regular, 40, true, 1015) == 40);
    assert(process_x(&delayed, 80, true, 1030) == 80);
    assert(regular.multiplier_milli == delayed.multiplier_milli);
}

static void test_idle_resets_gain_before_the_next_event(void) {
    struct zen_pointer_accel_state state;
    zen_pointer_accel_reset(&state, &curve);

    assert(process_x(&state, 112, true, 1000) == 112);
    assert(state.multiplier_milli > 1000);
    assert(process_x(&state, 3, false, 1060) == 3);
    assert(process_y(&state, 4, true, 1060) == 4);
}

static void test_stale_partial_frame_is_discarded(void) {
    struct zen_pointer_accel_state state;
    zen_pointer_accel_reset(&state, &curve);

    assert(process_x(&state, 112, true, 1000) == 112);
    assert(state.multiplier_milli > 1000);
    assert(process_x(&state, 50, false, 1015) > 50);
    assert(state.frame_active);

    /* A missing sync must not contaminate a later frame or preserve old gain. */
    assert(process_x(&state, 3, false, 1075) == 3);
    assert(process_y(&state, 4, true, 1075) == 4);
    assert(state.frame_x == 0 && state.frame_y == 0);
}

static void test_large_and_extreme_values_do_not_overflow(void) {
    struct zen_pointer_accel_axis_state state;
    zen_pointer_accel_axis_reset(&state);

    assert(zen_pointer_accel_scale_axis(&state, 32000, 2000) == 64000);
    assert(state.extra_milli == 0);
    assert(zen_pointer_accel_vector_speed(INT64_C(70000), 0) == UINT32_MAX);

    assert(zen_pointer_accel_scale_axis(&state, INT64_MAX, 2000) == INT64_MAX);
    assert(state.extra_milli >= 0 && state.extra_milli < 1000);

    zen_pointer_accel_axis_reset(&state);
    assert(zen_pointer_accel_scale_axis(&state, INT64_MIN, 2000) == INT64_MIN);
    assert(state.extra_milli <= 0 && state.extra_milli > -1000);
}

int main(void) {
    test_vector_speed();
    test_curve();
    test_low_speed_is_exact();
    test_fractional_output_is_preserved();
    test_inline_frame_keeps_original_event_count();
    test_gain_attack_is_smoothed_and_release_is_prompt();
    test_speed_is_normalized_for_scheduler_delay();
    test_idle_resets_gain_before_the_next_event();
    test_stale_partial_frame_is_discarded();
    test_large_and_extreme_values_do_not_overflow();
    puts("pointer_acceleration_test: PASS");
    return 0;
}
