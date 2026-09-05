/*
 * Copyright (c) 2026 Takashi Imai
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_gesture

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>
#include <zmk/behavior.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/input_listeners.h>
#include <zmk/keymap.h>
#include <zmk/virtual_key_position.h>

#include <zen/gesture_state.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct gesture_processor_config {
    uint8_t index;
    uint8_t layer;
    uint8_t reset_on_layer;
    uint32_t threshold;
    uint32_t cooldown_ms;
    struct zmk_behavior_binding left_binding;
    struct zmk_behavior_binding right_binding;
};

struct gesture_processor_data {
    struct k_mutex lock;
    struct zen_gesture_state states[ZMK_INPUT_LISTENERS_LEN];
};

static void reset_all_states(const struct device *dev) {
    struct gesture_processor_data *data = dev->data;

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return;
    }
    for (size_t i = 0; i < ARRAY_SIZE(data->states); i++) {
        zen_gesture_state_reset(&data->states[i]);
    }
    k_mutex_unlock(&data->lock);
}

static void invoke_action(const struct gesture_processor_config *cfg,
                          const struct zmk_behavior_binding *binding, uint8_t listener_index) {
    struct zmk_behavior_binding_event behavior_event = {
        .layer = cfg->layer,
        .position = ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(listener_index, cfg->index),
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    const int press_ret = zmk_behavior_invoke_binding(binding, behavior_event, true);
    const int release_ret = zmk_behavior_invoke_binding(binding, behavior_event, false);
    if (press_ret < 0 || release_ret < 0) {
        LOG_ERR("Gesture action failed: press=%d release=%d", press_ret, release_ret);
    }
}

static int gesture_processor_handle_event(const struct device *dev, struct input_event *event,
                                          uint32_t param1, uint32_t param2,
                                          struct zmk_input_processor_state *processor_state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);

    const struct gesture_processor_config *cfg = dev->config;
    struct gesture_processor_data *data = dev->data;

    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (!zmk_keymap_layer_active(cfg->layer)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (processor_state->input_device_index >= ARRAY_SIZE(data->states)) {
        LOG_ERR("Invalid input listener index: %d", processor_state->input_device_index);
        event->value = 0;
        return ZMK_INPUT_PROC_STOP;
    }

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        event->value = 0;
        return ZMK_INPUT_PROC_STOP;
    }

    if (!zmk_keymap_layer_active(cfg->layer)) {
        zen_gesture_state_reset(&data->states[processor_state->input_device_index]);
        k_mutex_unlock(&data->lock);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    const enum zen_gesture_axis axis =
        event->code == INPUT_REL_X ? ZEN_GESTURE_AXIS_X : ZEN_GESTURE_AXIS_Y;
    enum zen_gesture_direction direction =
        zen_gesture_state_update(&data->states[processor_state->input_device_index], axis,
                                 event->value, event->sync, k_uptime_get(), cfg->threshold,
                                 cfg->cooldown_ms);
    event->value = 0;
    k_mutex_unlock(&data->lock);

    if (direction == ZEN_GESTURE_LEFT && zmk_keymap_layer_active(cfg->layer)) {
        invoke_action(cfg, &cfg->left_binding, processor_state->input_device_index);
    } else if (direction == ZEN_GESTURE_RIGHT && zmk_keymap_layer_active(cfg->layer)) {
        invoke_action(cfg, &cfg->right_binding, processor_state->input_device_index);
    }

    return ZMK_INPUT_PROC_STOP;
}

static const struct zmk_input_processor_driver_api gesture_processor_driver_api = {
    .handle_event = gesture_processor_handle_event,
};

static int gesture_processor_init(const struct device *dev) {
    struct gesture_processor_data *data = dev->data;
    k_mutex_init(&data->lock);
    reset_all_states(dev);
    return 0;
}

#define GESTURE_PROCESSOR_INST(n)                                                                   \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) == 2, "Gesture processor requires two bindings");  \
    BUILD_ASSERT(DT_INST_PROP(n, threshold) > 0, "Gesture threshold must be greater than zero");   \
    static const struct gesture_processor_config gesture_processor_config_##n = {                  \
        .index = n,                                                                                 \
        .layer = DT_INST_PROP(n, layer),                                                            \
        .reset_on_layer = DT_INST_PROP(n, reset_on_layer),                                          \
        .threshold = DT_INST_PROP(n, threshold),                                                    \
        .cooldown_ms = DT_INST_PROP(n, cooldown_ms),                                                \
        .left_binding = ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n)),                              \
        .right_binding = ZMK_KEYMAP_EXTRACT_BINDING(1, DT_DRV_INST(n)),                             \
    };                                                                                              \
    static struct gesture_processor_data gesture_processor_data_##n;                               \
    DEVICE_DT_INST_DEFINE(n, gesture_processor_init, NULL, &gesture_processor_data_##n,             \
                          &gesture_processor_config_##n, POST_KERNEL,                               \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &gesture_processor_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GESTURE_PROCESSOR_INST)

#define MATCH_CHANGED_LAYER(inst)                                                                  \
    do {                                                                                            \
        const struct device *dev = DEVICE_DT_INST_GET(inst);                                       \
        const struct gesture_processor_config *cfg = dev->config;                                  \
        if (cfg->layer == event->layer || cfg->reset_on_layer == event->layer) {                    \
            gesture_layer_changed = true;                                                           \
        }                                                                                           \
    } while (false);

#define RESET_INSTANCE(inst) reset_all_states(DEVICE_DT_INST_GET(inst));

static int gesture_layer_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *event = as_zmk_layer_state_changed(eh);
    if (event != NULL) {
        bool gesture_layer_changed = false;
        DT_INST_FOREACH_STATUS_OKAY(MATCH_CHANGED_LAYER)
        if (gesture_layer_changed) {
            DT_INST_FOREACH_STATUS_OKAY(RESET_INSTANCE)
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(gesture_layer_state, gesture_layer_state_changed_listener);
ZMK_SUBSCRIPTION(gesture_layer_state, zmk_layer_state_changed);
