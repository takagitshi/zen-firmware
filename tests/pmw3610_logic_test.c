/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../drivers/pmw3610_alt/src/pmw3610_logic.h"

static void test_delta12_boundaries(void) {
    assert(pmw3610_decode_delta12(0x00, 0x0) == 0);
    assert(pmw3610_decode_delta12(0x01, 0x0) == 1);
    assert(pmw3610_decode_delta12(0x7f, 0x0) == 127);
    assert(pmw3610_decode_delta12(0x80, 0x0) == 128);
    assert(pmw3610_decode_delta12(0xff, 0x7) == 2047);
    assert(pmw3610_decode_delta12(0x00, 0x8) == -2048);
    assert(pmw3610_decode_delta12(0x80, 0xf) == -128);
    assert(pmw3610_decode_delta12(0x81, 0xf) == -127);
    assert(pmw3610_decode_delta12(0xff, 0xf) == -1);
}

static void test_bounded_retry_backoff(void) {
    uint8_t delay = PMW3610_READ_RETRY_MIN_MS;

    assert(delay == 1);
    delay = pmw3610_next_retry_delay(delay);
    assert(delay == 2);
    delay = pmw3610_next_retry_delay(delay);
    assert(delay == 4);
    delay = pmw3610_next_retry_delay(delay);
    assert(delay == 8);
    delay = pmw3610_next_retry_delay(delay);
    assert(delay == 16);
    delay = pmw3610_next_retry_delay(delay);
    assert(delay == 32);
    delay = pmw3610_next_retry_delay(delay);
    assert(delay == 64);
    assert(pmw3610_next_retry_delay(delay) == 64);
}

static void test_frame_retry_semantics(void) {
    struct pmw3610_frame_retry retry;

    retry = pmw3610_frame_retry_result(10, 20, -1, 0);
    assert(!retry.send_y && retry.x == 10 && retry.y == 20);

    retry = pmw3610_frame_retry_result(10, 20, 0, -1);
    assert(retry.send_y && retry.x == 0 && retry.y == 20);

    retry = pmw3610_frame_retry_result(10, 20, 0, 0);
    assert(retry.send_y && retry.x == 0 && retry.y == 0);

    retry = pmw3610_frame_retry_result(10, 0, -1, 0);
    assert(!retry.send_y && retry.x == 10 && retry.y == 0);

    retry = pmw3610_frame_retry_result(0, 20, 0, -1);
    assert(retry.send_y && retry.x == 0 && retry.y == 20);

    retry = pmw3610_frame_retry_result(0, 20, 0, 0);
    assert(retry.send_y && retry.x == 0 && retry.y == 0);

    retry = pmw3610_frame_retry_result(0, 0, 0, 0);
    assert(!retry.send_y && retry.x == 0 && retry.y == 0);
}

static void test_fixed_deadline_and_tail_flush(void) {
    struct pmw3610_report_accumulator state;
    int64_t delay;
    int16_t x;
    int16_t y;

    pmw3610_report_accumulator_init(&state);
    pmw3610_report_accumulate(&state, 10, -5);
    assert(pmw3610_report_prepare_schedule(&state, 1000, 15, &delay));
    assert(delay == 0);
    assert(pmw3610_report_take(&state, 1000, &x, &y));
    assert(x == 10 && y == -5);

    pmw3610_report_accumulate(&state, 2, 3);
    assert(pmw3610_report_prepare_schedule(&state, 1008, 15, &delay));
    assert(delay == 7);

    pmw3610_report_accumulate(&state, 4, -1);
    assert(!pmw3610_report_prepare_schedule(&state, 1012, 15, &delay));

    /* The first deadline remains 1015 even when motion continues. */
    assert(pmw3610_report_take(&state, 1015, &x, &y));
    assert(x == 6 && y == 2);

    /* A final sample is flushed by the scheduled work without another IRQ. */
    pmw3610_report_accumulate(&state, 1, 0);
    assert(pmw3610_report_prepare_schedule(&state, 1020, 15, &delay));
    assert(delay == 10);
    assert(pmw3610_report_take(&state, 1030, &x, &y));
    assert(x == 1 && y == 0);

    /* A new sample after an idle gap is reported immediately, never purged. */
    pmw3610_report_accumulate(&state, -1, 1);
    assert(pmw3610_report_prepare_schedule(&state, 1050, 15, &delay));
    assert(delay == 0);
    assert(pmw3610_report_take(&state, 1050, &x, &y));
    assert(x == -1 && y == 1);
}

static void test_clamped_reports_preserve_remainder(void) {
    struct pmw3610_report_accumulator state;
    int64_t delay;
    int16_t x;
    int16_t y;

    pmw3610_report_accumulator_init(&state);
    pmw3610_report_accumulate(&state, INT16_MAX + 10, INT16_MIN - 10);
    assert(pmw3610_report_prepare_schedule(&state, 1000, 15, &delay));
    assert(pmw3610_report_take(&state, 1000, &x, &y));
    assert(x == INT16_MAX && y == INT16_MIN);
    assert(state.dx == 10 && state.dy == -10);

    assert(pmw3610_report_prepare_schedule(&state, 1000, 15, &delay));
    assert(delay == 15);
    assert(pmw3610_report_take(&state, 1015, &x, &y));
    assert(x == 10 && y == -10);
}

int main(void) {
    test_delta12_boundaries();
    test_bounded_retry_backoff();
    test_frame_retry_semantics();
    test_fixed_deadline_and_tail_flush();
    test_clamped_reports_preserve_remainder();
    puts("pmw3610_logic_test: PASS");
    return 0;
}
