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

struct pointer_acceleration_data {
    struct zen_pointer_accel_state listeners[ZMK_INPUT_LISTENERS_LEN];
};

static int32_t clamp_i32(int64_t value) {
    if (value > INT32_MAX) {
        return INT32_MAX;
    }
    if (value < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)value;
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

    const enum zen_pointer_accel_axis axis = event->code == INPUT_REL_X
                                                   ? ZEN_POINTER_ACCEL_AXIS_X
                                                   : ZEN_POINTER_ACCEL_AXIS_Y;
    event->value = clamp_i32(zen_pointer_accel_process_axis(
        &config->curve, &data->listeners[listener_index], axis, event->value, event->sync,
        k_uptime_get()));

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api pointer_acceleration_driver_api = {
    .handle_event = pointer_acceleration_handle_event,
};

static int pointer_acceleration_init(const struct device *dev) {
    struct pointer_acceleration_data *data = dev->data;
    const struct pointer_acceleration_config *config = dev->config;
    for (size_t i = 0; i < ARRAY_SIZE(data->listeners); i++) {
        zen_pointer_accel_reset(&data->listeners[i], &config->curve);
    }
    return 0;
}

#define POINTER_ACCELERATION_INST(n)                                                               \
    BUILD_ASSERT(DT_INST_PROP(n, base_multiplier_milli) >= 500,                                  \
                 "Pointer acceleration base multiplier must be at least 0.5x");                 \
    BUILD_ASSERT(DT_INST_PROP(n, base_multiplier_milli) <=                                       \
                     ZEN_POINTER_ACCEL_MULTIPLIER_ONE,                                            \
                 "Pointer acceleration base multiplier must not exceed 1.0x");                  \
    BUILD_ASSERT(DT_INST_PROP(n, takeoff_speed) <= UINT16_MAX,                                   \
                 "Pointer acceleration takeoff-speed exceeds uint16");                          \
    BUILD_ASSERT(DT_INST_PROP(n, full_speed) <= UINT16_MAX,                                      \
                 "Pointer acceleration full-speed exceeds uint16");                             \
    BUILD_ASSERT(DT_INST_PROP(n, full_speed) > DT_INST_PROP(n, takeoff_speed),                    \
                 "Pointer acceleration full-speed must exceed takeoff-speed");                  \
    BUILD_ASSERT(DT_INST_PROP(n, max_multiplier_milli) >=                                        \
                     DT_INST_PROP(n, base_multiplier_milli),                                      \
                 "Pointer acceleration maximum multiplier must exceed the base");               \
    BUILD_ASSERT(DT_INST_PROP(n, max_multiplier_milli) <= 4000,                                  \
                 "Pointer acceleration maximum multiplier must not exceed 4.0x");               \
    BUILD_ASSERT(DT_INST_PROP(n, attack_smoothing_milli) <= 1000,                                \
                 "Pointer acceleration attack smoothing must not exceed 1.0");                  \
    BUILD_ASSERT(DT_INST_PROP(n, attack_smoothing_milli) > 0,                                    \
                 "Pointer acceleration attack smoothing must be positive");                    \
    BUILD_ASSERT(DT_INST_PROP(n, release_smoothing_milli) <= 1000,                               \
                 "Pointer acceleration release smoothing must not exceed 1.0");                 \
    BUILD_ASSERT(DT_INST_PROP(n, release_smoothing_milli) > 0,                                   \
                 "Pointer acceleration release smoothing must be positive");                   \
    BUILD_ASSERT(DT_INST_PROP(n, reference_interval_ms) > 0,                                     \
                 "Pointer acceleration reference interval must be positive");                   \
    BUILD_ASSERT(DT_INST_PROP(n, reference_interval_ms) <= UINT16_MAX,                            \
                 "Pointer acceleration reference interval exceeds uint16");                    \
    BUILD_ASSERT(DT_INST_PROP(n, idle_reset_ms) <= UINT16_MAX,                                   \
                 "Pointer acceleration idle reset exceeds uint16");                            \
    BUILD_ASSERT(DT_INST_PROP(n, idle_reset_ms) > DT_INST_PROP(n, reference_interval_ms),         \
                 "Pointer acceleration idle reset must exceed the reference interval");         \
    static const struct pointer_acceleration_config pointer_acceleration_config_##n = {            \
        .curve =                                                                                   \
            {                                                                                      \
                .base_multiplier_milli = DT_INST_PROP(n, base_multiplier_milli),                  \
                .takeoff_speed = DT_INST_PROP(n, takeoff_speed),                                  \
                .full_speed = DT_INST_PROP(n, full_speed),                                        \
                .max_multiplier_milli = DT_INST_PROP(n, max_multiplier_milli),                    \
                .attack_smoothing_milli = DT_INST_PROP(n, attack_smoothing_milli),                \
                .release_smoothing_milli = DT_INST_PROP(n, release_smoothing_milli),              \
                .reference_interval_ms = DT_INST_PROP(n, reference_interval_ms),                  \
                .idle_reset_ms = DT_INST_PROP(n, idle_reset_ms),                                  \
            },                                                                                     \
    };                                                                                             \
    static struct pointer_acceleration_data pointer_acceleration_data_##n;                         \
    DEVICE_DT_INST_DEFINE(n, pointer_acceleration_init, NULL, &pointer_acceleration_data_##n,       \
                          &pointer_acceleration_config_##n, POST_KERNEL,                           \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                    \
                          &pointer_acceleration_driver_api);

DT_INST_FOREACH_STATUS_OKAY(POINTER_ACCELERATION_INST)
