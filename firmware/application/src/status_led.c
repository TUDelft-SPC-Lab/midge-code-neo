#include "status_led.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(status_led);

#include <zephyr/drivers/led.h>
#define LED_NODE DT_ALIAS(led0)
static const struct device* status_led = DEVICE_DT_GET(DT_PARENT(LED_NODE));

#define DELAY_MS_OK 250
#define DELAY_MS_ERROR 750
#define DELAY_MS_IDENTIFY 2
#define DELAY_MS_ACTIVE 1

static bool led_active = false;

static int led_pulse(const struct device* dev, uint32_t led, uint32_t count, uint32_t delay) {
    int ret = 0;
    for (uint32_t i = 0; i < count; i++) {
        for (int i = 0; i <= LED_BRIGHTNESS_MAX; i++) {
            ret = led_set_brightness(dev, led, i);
            if (ret < 0) {
                LOG_ERR("Failed to set LED brightness: %d", ret);
                return ret;
            }
            k_msleep(delay);
        }
        for (int i = LED_BRIGHTNESS_MAX; i >= 0; i--) {
            ret = led_set_brightness(dev, led, i);
            if (ret < 0) {
                LOG_ERR("Failed to set LED brightness: %d", ret);
                return ret;
            }
            k_msleep(delay);
        }
    }
    return ret;
}

static int led_toggle(const struct device* dev, uint32_t led, uint32_t count, uint32_t delay) {
    int ret = 0;
    for (uint32_t i = 0; i < count; i++) {
        ret = led_on(dev, led);
        if (ret < 0) {
            LOG_ERR("Failed to turn on LED: %d", ret);
            return ret;
        }
        k_msleep(delay);
        ret = led_off(dev, led);
        if (ret < 0) {
            LOG_ERR("Failed to turn off LED: %d", ret);
            return ret;
        }
        k_msleep(delay);
    }
    return ret;
}

int led_init(void) {
    int ret = device_is_ready(status_led);
    if (!ret) {
        LOG_ERR("PWM device %s is not ready", status_led->name);
        return -ENODEV;
    }
    return 0;
}

int led_report_status(int status_code) {
    if (status_code == 0) {
        if (led_toggle(status_led, 0, 10, DELAY_MS_OK)) {
            LOG_ERR("Failed to report OK status");
        }
        return 0;
    }

    LOG_ERR("Reporting error status: %d", status_code);

    if (status_code < 0) {
        status_code = -status_code;
    }

    if (led_toggle(status_led, 0, status_code, DELAY_MS_ERROR)) {
        LOG_ERR("Failed to report error status: %d", status_code);
    }

    return 0;
}

int led_report_active(bool active) {
    led_active = active;
    if (led_pulse(status_led, 0, 5, DELAY_MS_ACTIVE)) {
        LOG_ERR("Failed to report active status");
    }
    if (active) {
        return led_set_brightness(status_led, 0, LED_BRIGHTNESS_MAX);
    } else {
        return led_set_brightness(status_led, 0, 2);
    }
}

int led_identify(void) {
    // Blink the LED rapidly for identification
    if (led_pulse(status_led, 0, 15, DELAY_MS_IDENTIFY)) {
        LOG_ERR("Failed to identify LED");
    }
    if (led_report_active(led_active)) {
        LOG_ERR("Failed to restore LED state");
    }
    return 0;
}
