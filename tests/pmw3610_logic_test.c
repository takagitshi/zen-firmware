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

static void test_accelerated_output_is_chunked_without_losing_distance(void) {
    struct pmw3610_frame_chunk chunk;

    chunk = pmw3610_frame_chunk_from_pending(32000, -16000);
    assert(chunk.x == 32000 && chunk.y == -16000);

    int32_t pending_x = 49150;
    int32_t pending_y = 24575;
    int32_t total_x = 0;
    int32_t total_y = 0;
    int chunks = 0;
    while (pending_x != 0 || pending_y != 0) {
        chunk = pmw3610_frame_chunk_from_pending(pending_x, pending_y);
        assert(chunk.x <= INT16_MAX && chunk.x >= INT16_MIN);
        assert(chunk.y <= INT16_MAX && chunk.y >= INT16_MIN);
        assert(chunk.x * 24575 - chunk.y * 49150 >= -49150);
        assert(chunk.x * 24575 - chunk.y * 49150 <= 49150);
        pending_x -= chunk.x;
        pending_y -= chunk.y;
        total_x += chunk.x;
        total_y += chunk.y;
        chunks++;
        assert(chunks <= 3);
    }
    assert(chunks == 2);
    assert(total_x == 49150 && total_y == 24575);

    pending_x = -49152;
    pending_y = 16384;
    total_x = 0;
    total_y = 0;
    while (pending_x != 0 || pending_y != 0) {
        chunk = pmw3610_frame_chunk_from_pending(pending_x, pending_y);
        pending_x -= chunk.x;
        pending_y -= chunk.y;
        total_x += chunk.x;
        total_y += chunk.y;
    }
    assert(total_x == -49152 && total_y == 16384);
}

static void test_y_retry_completes_sync_before_the_next_overflow_chunk(void) {
    struct pmw3610_output_state state;
    pmw3610_output_init(&state);
    pmw3610_output_queue(&state, 49150, 24575, true);

    struct pmw3610_output_frame first = pmw3610_output_take_next(&state);
    assert(!first.retrying);
    assert(first.x == 32767 && first.y == 16383);

    /* X reached the listener, but Y failed before the sync event. */
    struct pmw3610_frame_retry retry =
        pmw3610_frame_retry_result(first.x, first.y, 0, -1);
    pmw3610_output_complete(&state, retry, false);

    struct pmw3610_output_frame second = pmw3610_output_take_next(&state);
    assert(second.retrying);
    assert(second.x == 0 && second.y == 16383);
    pmw3610_output_complete(&state,
                            pmw3610_frame_retry_result(second.x, second.y, 0, 0), false);

    /* Only after Y syncs may the remaining X/Y chunk enter the listener. */
    struct pmw3610_output_frame third = pmw3610_output_take_next(&state);
    assert(!third.retrying);
    assert(third.x == 16383 && third.y == 8192);
    pmw3610_output_complete(&state,
                            pmw3610_frame_retry_result(third.x, third.y, 0, 0), false);
    assert(!pmw3610_output_has_pending(&state));

    assert((int32_t)first.x + second.x + third.x == 49150);
    assert((int32_t)second.y + third.y == 24575);
}

static void test_zero_sync_failure_is_retried(void) {
    struct pmw3610_output_state state;
    pmw3610_output_init(&state);
    pmw3610_output_queue(&state, 0, 0, true);

    struct pmw3610_output_frame first = pmw3610_output_take_next(&state);
    assert(first.x == 0 && first.y == 0 && first.force_sync);
    pmw3610_output_complete(&state, (struct pmw3610_frame_retry){0}, true);
    assert(pmw3610_output_has_pending(&state));

    struct pmw3610_output_frame second = pmw3610_output_take_next(&state);
    assert(second.retrying);
    assert(second.x == 0 && second.y == 0 && second.force_sync);
    pmw3610_output_complete(&state, (struct pmw3610_frame_retry){0}, false);
    assert(!pmw3610_output_has_pending(&state));
}

static void test_fixed_deadline_and_tail_flush(void) {
    struct pmw3610_report_accumulator state;
    int64_t delay;
    int64_t collection_elapsed;
    int16_t x;
    int16_t y;

    pmw3610_report_accumulator_init(&state);
    pmw3610_report_accumulate(&state, 1000, 10, -5);
    assert(pmw3610_report_prepare_schedule(&state, 1000, 15, &delay));
    assert(delay == 0);
    assert(pmw3610_report_take(&state, 1000, &x, &y, &collection_elapsed));
    assert(x == 10 && y == -5);
    assert(collection_elapsed == 0);

    pmw3610_report_accumulate(&state, 1008, 2, 3);
    assert(pmw3610_report_prepare_schedule(&state, 1008, 15, &delay));
    assert(delay == 7);

    pmw3610_report_accumulate(&state, 1012, 4, -1);
    assert(!pmw3610_report_prepare_schedule(&state, 1012, 15, &delay));

    /* The first deadline remains 1015 even when motion continues. */
    assert(pmw3610_report_take(&state, 1015, &x, &y, &collection_elapsed));
    assert(x == 6 && y == 2);
    assert(collection_elapsed == 7);

    /* A final sample is flushed by the scheduled work without another IRQ. */
    pmw3610_report_accumulate(&state, 1020, 1, 0);
    assert(pmw3610_report_prepare_schedule(&state, 1020, 15, &delay));
    assert(delay == 10);
    assert(pmw3610_report_take(&state, 1030, &x, &y, &collection_elapsed));
    assert(x == 1 && y == 0);
    assert(collection_elapsed == 10);

    /* A new sample after an idle gap is reported immediately, never purged. */
    pmw3610_report_accumulate(&state, 1050, -1, 1);
    assert(pmw3610_report_prepare_schedule(&state, 1050, 15, &delay));
    assert(delay == 0);
    assert(pmw3610_report_take(&state, 1050, &x, &y, &collection_elapsed));
    assert(x == -1 && y == 1);
    assert(collection_elapsed == 0);
}

static void test_clamped_reports_preserve_remainder(void) {
    struct pmw3610_report_accumulator state;
    int64_t delay;
    int64_t collection_elapsed;
    int16_t x;
    int16_t y;

    pmw3610_report_accumulator_init(&state);
    pmw3610_report_accumulate(&state, 1000, INT16_MAX + 10, INT16_MIN - 10);
    assert(pmw3610_report_prepare_schedule(&state, 1000, 15, &delay));
    assert(pmw3610_report_take(&state, 1000, &x, &y, &collection_elapsed));
    assert(x == INT16_MAX && y == INT16_MIN);
    assert(state.dx == 10 && state.dy == -10);

    assert(pmw3610_report_prepare_schedule(&state, 1000, 15, &delay));
    assert(delay == 15);
    assert(pmw3610_report_take(&state, 1015, &x, &y, &collection_elapsed));
    assert(x == 10 && y == -10);
    assert(collection_elapsed == 15);
}

static void test_cancelled_motion_does_not_leave_stale_collection_time(void) {
    struct pmw3610_report_accumulator state;
    int64_t delay;
    int64_t collection_elapsed;
    int16_t x;
    int16_t y;

    pmw3610_report_accumulator_init(&state);
    pmw3610_report_accumulate(&state, 1000, 5, -3);
    pmw3610_report_accumulate(&state, 1005, -5, 3);
    assert(!state.have_accumulation_start);
    assert(!pmw3610_report_prepare_schedule(&state, 1015, 15, &delay));

    pmw3610_report_accumulate(&state, 2000, 320, 0);
    assert(pmw3610_report_take(&state, 2000, &x, &y, &collection_elapsed));
    assert(x == 320 && y == 0 && collection_elapsed == 0);
}

int main(void) {
    test_delta12_boundaries();
    test_bounded_retry_backoff();
    test_frame_retry_semantics();
    test_accelerated_output_is_chunked_without_losing_distance();
    test_y_retry_completes_sync_before_the_next_overflow_chunk();
    test_zero_sync_failure_is_retried();
    test_fixed_deadline_and_tail_flush();
    test_clamped_reports_preserve_remainder();
    test_cancelled_motion_does_not_leave_stale_collection_time();
    puts("pmw3610_logic_test: PASS");
    return 0;
}
