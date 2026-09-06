/*
 * Copyright (c) 2026 Takashi
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zen_input_processor_runtime_scaler

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <drivers/input_processor.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>
#include <zmk/studio/custom.h>

#include "../drivers/pmw3610_alt/src/pmw3610.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define ZEN_PMW3610_SETTINGS_SUBSYSTEM_ID "zen__pmw3610_settings"
#define ZEN_PMW3610_DEFAULT_CPI 600
#define ZEN_PMW3610_DEFAULT_SCROLL_DIVISOR 30
#define ZEN_PMW3610_SETTINGS_RETRY_DELAY K_MSEC(250)

static struct zmk_rpc_custom_subsystem_meta zen_pmw3610_settings_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS(),
    .security = ZMK_STUDIO_RPC_HANDLER_SECURED,
};

static bool zen_pmw3610_settings_handle_request(const zmk_custom_CallRequest *request,
                                                pb_callback_t *response) {
    ARG_UNUSED(request);
    ARG_UNUSED(response);

    return false;
}

ZMK_RPC_CUSTOM_SUBSYSTEM(zen__pmw3610_settings, &zen_pmw3610_settings_meta,
                         zen_pmw3610_settings_handle_request);

static const struct zmk_custom_setting_value zen_pmw3610_cpi_values[] = {
    ZMK_CUSTOM_SETTING_VALUE_INT32(200),  ZMK_CUSTOM_SETTING_VALUE_INT32(400),
    ZMK_CUSTOM_SETTING_VALUE_INT32(600),  ZMK_CUSTOM_SETTING_VALUE_INT32(800),
    ZMK_CUSTOM_SETTING_VALUE_INT32(1200), ZMK_CUSTOM_SETTING_VALUE_INT32(1600),
    ZMK_CUSTOM_SETTING_VALUE_INT32(2400), ZMK_CUSTOM_SETTING_VALUE_INT32(3200),
};

#define ZEN_PMW3610_CPI_OPTIONS                                                                  \
    ((struct zmk_custom_setting_constraint){                                                     \
        .type = ZMK_CUSTOM_SETTING_CONSTRAINT_OPTIONS,                                           \
        .options = {.values = zen_pmw3610_cpi_values,                                            \
                    .labels = NULL,                                                              \
                    .count = ARRAY_SIZE(zen_pmw3610_cpi_values)}})

ZMK_CUSTOM_SETTING_DEFINE(
    zen_pmw3610_cpi, ZEN_PMW3610_SETTINGS_SUBSYSTEM_ID, "cpi@trackball",
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, ZMK_CUSTOM_SETTING_VALUE_INT32(ZEN_PMW3610_DEFAULT_CPI),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZEN_PMW3610_CPI_OPTIONS);

ZMK_CUSTOM_SETTING_DEFINE(
    zen_pmw3610_scroll_divisor, ZEN_PMW3610_SETTINGS_SUBSYSTEM_ID,
    "scroll_divisor@trackball", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(ZEN_PMW3610_DEFAULT_SCROLL_DIVISOR),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(1, 100));

static atomic_t scroll_divisor = ATOMIC_INIT(ZEN_PMW3610_DEFAULT_SCROLL_DIVISOR);
static struct k_work_delayable apply_cpi_work;

static int read_int32(const struct zmk_custom_setting *setting, int32_t fallback) {
    struct zmk_custom_setting_value value;

    if (zmk_custom_setting_read(setting, &value) != 0 ||
        value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32) {
        return fallback;
    }

    return value.int32_value;
}

static void apply_cpi_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    const struct device *sensor = DEVICE_DT_GET(DT_NODELABEL(trackball));
    struct sensor_value value = {
        .val1 = read_int32(&zen_pmw3610_cpi, ZEN_PMW3610_DEFAULT_CPI),
    };
    int err = sensor_attr_set(sensor, SENSOR_CHAN_ALL, PMW3610_ALT_ATTR_CPI, &value);

    if (err == -EBUSY || err == -EAGAIN) {
        k_work_reschedule(&apply_cpi_work, ZEN_PMW3610_SETTINGS_RETRY_DELAY);
    } else if (err != 0) {
        LOG_ERR("Failed to apply PMW3610 CPI: %d", err);
    }
}

static void apply_settings(void) {
    atomic_set(&scroll_divisor,
               read_int32(&zen_pmw3610_scroll_divisor,
                          ZEN_PMW3610_DEFAULT_SCROLL_DIVISOR));
    k_work_reschedule(&apply_cpi_work, K_NO_WAIT);
}

static int zen_pmw3610_settings_event_listener(const zmk_event_t *event) {
    if (as_zmk_custom_settings_initialized(event) != NULL) {
        apply_settings();
        return 0;
    }

    const struct zmk_custom_setting_changed *changed = as_zmk_custom_setting_changed(event);
    if (changed == NULL) {
        return 0;
    }

    if (changed->setting == &zen_pmw3610_scroll_divisor) {
        atomic_set(&scroll_divisor,
                   read_int32(&zen_pmw3610_scroll_divisor,
                              ZEN_PMW3610_DEFAULT_SCROLL_DIVISOR));
    } else if (changed->setting == &zen_pmw3610_cpi) {
        k_work_reschedule(&apply_cpi_work, K_NO_WAIT);
    }

    return 0;
}

ZMK_LISTENER(zen_pmw3610_settings, zen_pmw3610_settings_event_listener);
ZMK_SUBSCRIPTION(zen_pmw3610_settings, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(zen_pmw3610_settings, zmk_custom_settings_initialized);

struct runtime_scaler_config {
    uint8_t type;
    size_t codes_len;
    const uint16_t *codes;
};

static int runtime_scaler_handle_event(const struct device *dev, struct input_event *event,
                                       uint32_t param1, uint32_t param2,
                                       struct zmk_input_processor_state *state) {
    const struct runtime_scaler_config *config = dev->config;

    ARG_UNUSED(param1);
    ARG_UNUSED(param2);

    if (event->type != config->type) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    for (size_t i = 0; i < config->codes_len; i++) {
        if (config->codes[i] != event->code) {
            continue;
        }

        int32_t value = event->value;
        if (state != NULL && state->remainder != NULL) {
            value += *state->remainder;
        }

        const int32_t divisor = atomic_get(&scroll_divisor);
        event->value = value / divisor;
        if (state != NULL && state->remainder != NULL) {
            *state->remainder = value - (event->value * divisor);
        }

        return 0;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api runtime_scaler_driver_api = {
    .handle_event = runtime_scaler_handle_event,
};

static int zen_pmw3610_settings_init(void) {
    k_work_init_delayable(&apply_cpi_work, apply_cpi_work_handler);
    return 0;
}

SYS_INIT(zen_pmw3610_settings_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#define RUNTIME_SCALER_INST(n)                                                                    \
    static const uint16_t runtime_scaler_codes_##n[] = DT_INST_PROP(n, codes);                    \
    static const struct runtime_scaler_config runtime_scaler_config_##n = {                       \
        .type = DT_INST_PROP(n, type),                                                            \
        .codes_len = ARRAY_SIZE(runtime_scaler_codes_##n),                                        \
        .codes = runtime_scaler_codes_##n,                                                        \
    };                                                                                            \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &runtime_scaler_config_##n, POST_KERNEL,           \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &runtime_scaler_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_SCALER_INST)
