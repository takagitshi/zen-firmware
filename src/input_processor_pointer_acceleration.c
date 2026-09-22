/*
 * Copyright (c) 2026 Takashi Imai
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_pointer_acceleration

#include <limits.h>

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>
#include <zmk/input_listeners.h>

#include <zen/pointer_acceleration.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct pointer_acceleration_config {
    struct zen_pointer_accel_curve curve;
};

struct pointer_acceleration_data;

struct pointer_acceleration_output_state {
    struct k_work_delayable work;
    struct pointer_acceleration_data *data;
    const struct device *dev;
    int64_t pending_x;
    int64_t pending_y;
    int16_t inflight_y;
    bool waiting_for_y;
};

struct pointer_acceleration_listener_state {
    struct zen_pointer_accel_state acceleration;
    struct pointer_acceleration_output_state output;
    int64_t frame_x;
    int64_t frame_y;
};

struct pointer_acceleration_data {
    struct k_mutex lock;
    struct pointer_acceleration_listener_state listeners[ZMK_INPUT_LISTENERS_LEN];
};

static int64_t saturating_add(int64_t lhs, int64_t rhs) {
    if (rhs > 0 && lhs > INT64_MAX - rhs) {
        return INT64_MAX;
    }
    if (rhs < 0 && lhs < INT64_MIN - rhs) {
        return INT64_MIN;
    }
    return lhs + rhs;
}

static uint64_t magnitude_i64(int64_t value) {
    return value < 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
}

static int16_t proportional_chunk(int64_t value, uint64_t largest_magnitude) {
    if (value == 0) {
        return 0;
    }
    if (largest_magnitude <= INT16_MAX) {
        return (int16_t)value;
    }

    uint64_t magnitude = magnitude_i64(value);
    while (largest_magnitude > UINT32_MAX) {
        magnitude = (magnitude + 1U) >> 1;
        largest_magnitude = (largest_magnitude + 1U) >> 1;
    }
    const uint64_t scaled = (magnitude * INT16_MAX) / largest_magnitude;
    const int16_t chunk = (int16_t)(scaled == 0 ? 1 : scaled);
    return value < 0 ? (int16_t)-chunk : chunk;
}

static void pointer_acceleration_report_work(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct pointer_acceleration_output_state *output =
        CONTAINER_OF(delayable, struct pointer_acceleration_output_state, work);
    struct pointer_acceleration_data *data = output->data;
    bool retry = false;
    bool more = false;

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        k_work_reschedule(&output->work, K_MSEC(1));
        return;
    }

    if (output->waiting_for_y) {
        const int ret =
            input_report_rel(output->dev, INPUT_REL_Y, output->inflight_y, true, K_NO_WAIT);
        if (ret < 0) {
            retry = true;
        } else {
            output->pending_y -= output->inflight_y;
            output->inflight_y = 0;
            output->waiting_for_y = false;
        }
    } else {
        const uint64_t largest_magnitude =
            MAX(magnitude_i64(output->pending_x), magnitude_i64(output->pending_y));
        const int16_t x = proportional_chunk(output->pending_x, largest_magnitude);
        const int16_t y = proportional_chunk(output->pending_y, largest_magnitude);

        if (x != 0) {
            const int ret = input_report_rel(output->dev, INPUT_REL_X, x, y == 0, K_NO_WAIT);
            if (ret < 0) {
                retry = true;
            } else {
                output->pending_x -= x;
            }
        }

        if (!retry && y != 0) {
            const int ret = input_report_rel(output->dev, INPUT_REL_Y, y, true, K_NO_WAIT);
            if (ret < 0) {
                retry = true;
                if (x != 0) {
                    output->inflight_y = y;
                    output->waiting_for_y = true;
                }
            } else {
                output->pending_y -= y;
            }
        }
    }

    more = output->waiting_for_y || output->pending_x != 0 || output->pending_y != 0;
    k_mutex_unlock(&data->lock);

    if (retry) {
        k_work_reschedule(&output->work, K_MSEC(1));
    } else if (more) {
        k_work_reschedule(&output->work, K_NO_WAIT);
    }
}

static int pointer_acceleration_handle_event(
    const struct device *dev, struct input_event *event, uint32_t param1, uint32_t param2,
    struct zmk_input_processor_state *processor_state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);

    if (!IS_ENABLED(CONFIG_ZEN_POINTER_ACCELERATION) || event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (processor_state == NULL) {
        LOG_ERR("Pointer acceleration requires input listener state");
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct pointer_acceleration_data *data = dev->data;
    const struct pointer_acceleration_config *config = dev->config;
    const uint8_t listener_index = processor_state->input_device_index;

    if (listener_index >= ARRAY_SIZE(data->listeners)) {
        LOG_ERR("Invalid pointer acceleration listener index: %d", listener_index);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct pointer_acceleration_listener_state *state = &data->listeners[listener_index];

    const int64_t raw_value = event->value;
    if (event->code == INPUT_REL_X) {
        state->frame_x = saturating_add(state->frame_x, raw_value);
    } else {
        state->frame_y = saturating_add(state->frame_y, raw_value);
    }
    event->value = 0;

    if (!event->sync) {
        k_mutex_unlock(&data->lock);
        return ZMK_INPUT_PROC_STOP;
    }

    int64_t accelerated_x;
    int64_t accelerated_y;
    zen_pointer_accel_apply(&config->curve, &state->acceleration, state->frame_x,
                            state->frame_y, &accelerated_x, &accelerated_y);
    state->frame_x = 0;
    state->frame_y = 0;
    state->output.pending_x = saturating_add(state->output.pending_x, accelerated_x);
    state->output.pending_y = saturating_add(state->output.pending_y, accelerated_y);

    k_mutex_unlock(&data->lock);
    k_work_reschedule(&state->output.work, K_NO_WAIT);
    return ZMK_INPUT_PROC_STOP;
}

static const struct zmk_input_processor_driver_api pointer_acceleration_driver_api = {
    .handle_event = pointer_acceleration_handle_event,
};

static int pointer_acceleration_init(const struct device *dev) {
    struct pointer_acceleration_data *data = dev->data;
    k_mutex_init(&data->lock);
    for (size_t i = 0; i < ARRAY_SIZE(data->listeners); i++) {
        zen_pointer_accel_reset(&data->listeners[i].acceleration);
        data->listeners[i].output.data = data;
        data->listeners[i].output.dev = dev;
        k_work_init_delayable(&data->listeners[i].output.work,
                              pointer_acceleration_report_work);
    }
    return 0;
}

#define POINTER_ACCELERATION_INST(n)                                                               \
    BUILD_ASSERT(DT_INST_PROP(n, takeoff_speed) <= UINT16_MAX,                                    \
                 "Pointer acceleration takeoff-speed exceeds uint16");                           \
    BUILD_ASSERT(DT_INST_PROP(n, full_speed) <= UINT16_MAX,                                       \
                 "Pointer acceleration full-speed exceeds uint16");                              \
    BUILD_ASSERT(DT_INST_PROP(n, full_speed) > DT_INST_PROP(n, takeoff_speed),                    \
                 "Pointer acceleration full-speed must exceed takeoff-speed");                   \
    BUILD_ASSERT(DT_INST_PROP(n, max_multiplier_milli) >=                                         \
                     ZEN_POINTER_ACCEL_MULTIPLIER_ONE,                                             \
                 "Pointer acceleration maximum multiplier must be at least 1.0x");               \
    BUILD_ASSERT(DT_INST_PROP(n, max_multiplier_milli) <= 4000,                                   \
                 "Pointer acceleration maximum multiplier must not exceed 4.0x");                \
    static const struct pointer_acceleration_config pointer_acceleration_config_##n = {            \
        .curve =                                                                                   \
            {                                                                                      \
                .takeoff_speed = DT_INST_PROP(n, takeoff_speed),                                  \
                .full_speed = DT_INST_PROP(n, full_speed),                                        \
                .max_multiplier_milli = DT_INST_PROP(n, max_multiplier_milli),                    \
            },                                                                                     \
    };                                                                                             \
    static struct pointer_acceleration_data pointer_acceleration_data_##n;                         \
    DEVICE_DT_INST_DEFINE(n, pointer_acceleration_init, NULL, &pointer_acceleration_data_##n,       \
                          &pointer_acceleration_config_##n, POST_KERNEL,                           \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                    \
                          &pointer_acceleration_driver_api);

DT_INST_FOREACH_STATUS_OKAY(POINTER_ACCELERATION_INST)
