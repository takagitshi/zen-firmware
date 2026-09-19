#pragma once

/**
 * @file pixart.h
 *
 * @brief Common header file for all optical motion sensor by PIXART
 */

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/atomic.h>

#include "pmw3610_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* device data structure */
struct pixart_data {
    const struct device          *dev;
    struct pmw3610_report_accumulator report;
    bool                         sw_smart_flag; // for pmw3610 smart algorithm

    struct gpio_callback         irq_gpio_cb; // motion pin irq callback
    /* Both jobs run on the system work queue; the GPIO ISR only schedules trigger_work. */
    struct k_work_delayable      trigger_work; // motion read/retry job
#if CONFIG_PMW3610_ALT_REPORT_INTERVAL_MIN > 0
    struct k_work_delayable      report_work; // lossless rate-limited report job
#endif
    uint8_t                      read_retry_delay_ms;
    bool                         read_error_active;
    atomic_t                     motion_work_active;

    struct k_work_delayable      init_work; // the work structure for delayable init steps
    int                          async_init_step;
    int                          init_attempt;

    bool                         ready; // whether init is finished successfully
    int                          err; // error code during async init
};

// device config data structure
struct pixart_config {
	struct spi_dt_spec spi;
    struct gpio_dt_spec irq_gpio;
    uint16_t cpi;
    bool swap_xy;
    bool inv_x;
    bool inv_y;
    uint8_t evt_type;
    uint8_t x_input_code;
    uint8_t y_input_code;
    bool force_awake;
    bool force_awake_4ms_mode;
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */
