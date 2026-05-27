#include "status_led.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(status_led);

#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define DELAY_MS_OK 250
#define DELAY_MS_ERROR 750
#define DELAY_MS_IDENTIFY 100

int led_init(void) {
    int ret;
    if (!gpio_is_ready_dt(&led)) {
        return 0;
    }
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure LED pin");
    }
    return ret;
}

int led_report_status(int status_code) {
    if (status_code == 0) {
        for (int i = 0; i < 10; i++) {
            int ret = gpio_pin_toggle_dt(&led);
            if (ret < 0) {
                LOG_ERR("Failed to toggle LED");
                return 0;
            }
            k_msleep(DELAY_MS_OK);
        }
        return 0;
    }

    LOG_ERR("Reporting error status: %d", status_code);

    if (status_code < 0) {
        status_code = -status_code;
    }

    for (int i = 0; i < status_code; i++) {
        int ret = gpio_pin_toggle_dt(&led);
        if (ret < 0) {
            return 0;
        }
        k_msleep(DELAY_MS_ERROR);
    }

    return 0;
}

// To be called from module that is aware of the active sensors, for now,
// storage
int led_report_active(bool active) { return gpio_pin_set_dt(&led, active ? 0 : 1); }

int led_identify(void) {
    // get the current led state
    int ret = gpio_pin_get_dt(&led);
    bool led_on;
    if (ret < 0) {
        LOG_ERR("Failed to get LED state");
        led_on = false;  // default to off
    } else {
        led_on = ret;
    }

    for (int i = 0; i < 100; i++) {
        int ret = gpio_pin_toggle_dt(&led);
        if (ret < 0) {
            LOG_ERR("Failed to toggle LED");
            return 0;
        }
        k_msleep(DELAY_MS_IDENTIFY);
    }
    return gpio_pin_set_dt(&led, led_on);  // restore original state
}
