/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include <zen/gesture_state.h>

#define THRESHOLD 30
#define COOLDOWN_MS 150

static enum zen_gesture_direction update(struct zen_gesture_state *state, enum zen_gesture_axis axis,
                                         int32_t value, bool sync, int64_t now_ms) {
    return zen_gesture_state_update(state, axis, value, sync, now_ms, THRESHOLD, COOLDOWN_MS);
}

int main(void) {
    struct zen_gesture_state state;

    zen_gesture_state_reset(&state);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 20, false, 1000) == ZEN_GESTURE_NONE);
    assert(update(&state, ZEN_GESTURE_AXIS_Y, 15, true, 1000) == ZEN_GESTURE_RIGHT);

    zen_gesture_state_reset(&state);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 29, true, 1000) == ZEN_GESTURE_NONE);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 1, true, 1015) == ZEN_GESTURE_RIGHT);
    assert(state.x == 0 && state.y == 0);

    zen_gesture_state_reset(&state);
    assert(update(&state, ZEN_GESTURE_AXIS_X, -30, true, 1000) == ZEN_GESTURE_LEFT);

    zen_gesture_state_reset(&state);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 15, false, 1000) == ZEN_GESTURE_NONE);
    assert(update(&state, ZEN_GESTURE_AXIS_Y, 15, true, 1000) == ZEN_GESTURE_RIGHT);

    zen_gesture_state_reset(&state);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 14, false, 1000) == ZEN_GESTURE_NONE);
    assert(update(&state, ZEN_GESTURE_AXIS_Y, -16, true, 1000) == ZEN_GESTURE_UP);
    assert(state.x == 0 && state.y == 0);

    zen_gesture_state_reset(&state);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 30, true, 1000) == ZEN_GESTURE_RIGHT);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 100, true, 1100) == ZEN_GESTURE_NONE);
    assert(state.x == 0 && state.y == 0);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 29, true, 1150) == ZEN_GESTURE_NONE);
    assert(state.x == 0 && state.y == 0);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 29, true, 1165) == ZEN_GESTURE_NONE);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 1, true, 1180) == ZEN_GESTURE_RIGHT);

    zen_gesture_state_reset(&state);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 30, true, 1000) == ZEN_GESTURE_RIGHT);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 20, false, 1149) == ZEN_GESTURE_NONE);
    assert(update(&state, ZEN_GESTURE_AXIS_Y, 15, true, 1150) == ZEN_GESTURE_NONE);
    assert(state.x == 0 && state.y == 0);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 30, true, 1165) == ZEN_GESTURE_RIGHT);

    zen_gesture_state_reset(&state);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 20, true, 1000) == ZEN_GESTURE_NONE);
    assert(update(&state, ZEN_GESTURE_AXIS_X, -20, true, 1015) == ZEN_GESTURE_NONE);
    assert(state.x == 0);

    zen_gesture_state_reset(&state);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 29, true, 1000) == ZEN_GESTURE_NONE);
    zen_gesture_state_reset(&state);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 1, true, 1015) == ZEN_GESTURE_NONE);

    zen_gesture_state_reset(&state);
    assert(update(&state, ZEN_GESTURE_AXIS_X, INT32_MAX, false, 1000) == ZEN_GESTURE_NONE);
    assert(update(&state, ZEN_GESTURE_AXIS_X, 1, false, 1000) == ZEN_GESTURE_NONE);
    assert(state.x == INT32_MAX);

    puts("gesture_state_test: PASS");
    return 0;
}
