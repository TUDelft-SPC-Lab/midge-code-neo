
#include "privacy_switch.h"

#include <inttypes.h>
#include <stdbool.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "audio.h"

LOG_MODULE_REGISTER(privacy_switch);

static const struct gpio_dt_spec sw_off = GPIO_DT_SPEC_GET(DT_ALIAS(sw_audio_off), gpios);
static const struct gpio_dt_spec sw_low = GPIO_DT_SPEC_GET(DT_ALIAS(sw_audio_low), gpios);
static const struct gpio_dt_spec sw_high = GPIO_DT_SPEC_GET(DT_ALIAS(sw_audio_high), gpios);

static struct gpio_callback sw_callback_data;

K_MUTEX_DEFINE(switch_mutex);

bool debouncing = false;
enum privacy_sw_pos sw_pos = PRIVACY_SWITCH_POS_OFF;

void update_privacy_sw_pos() {
    int off = gpio_pin_get(sw_off.port, sw_off.pin);
    int low = gpio_pin_get(sw_low.port, sw_low.pin);
    int high = gpio_pin_get(sw_high.port, sw_high.pin);
    enum audio_sensor_state state = audio_sensor_get_status();
    if (state == AUDIO_SENSOR_STATE_ACTIVE) {
        audio_sensor_stop();
    }
    LOG_INF("current switch state: off:%d low:%d hi:%d", off, low, high);

    if (off == 1) {
        sw_pos = PRIVACY_SWITCH_POS_OFF;
    } else if (low == 1) {
        sw_pos = PRIVACY_SWITCH_POS_LOW;
    } else if (high == 1) {
        sw_pos = PRIVACY_SWITCH_POS_HIGH;
    } else {
        // no change but weird, log it
        LOG_INF("no position change!");
    }
}

struct k_work_delayable switch_read_work;
void switch_read_work_handler(struct k_work* work) {
    if (k_mutex_lock(&switch_mutex, K_NO_WAIT) == 0) {
        update_privacy_sw_pos();
        debouncing = false;
        k_mutex_unlock(&switch_mutex);
    }
}

void sw_pos_changed(const struct device* dev, struct gpio_callback* cb, uint32_t pins) {
    if (k_mutex_lock(&switch_mutex, K_NO_WAIT) == 0) {
        if (!debouncing) {
            debouncing = true;
        } else {
            // already debouncing, ignore this interrupt
            k_mutex_unlock(&switch_mutex);
            return;
        }
        memset(&switch_read_work, 0, sizeof(switch_read_work));
        k_work_init_delayable(&switch_read_work, switch_read_work_handler);
        k_work_schedule(&switch_read_work, K_MSEC(200));
        k_mutex_unlock(&switch_mutex);
    }
}

int switch_sensor_init() {
    bool gpios_ready =
        gpio_is_ready_dt(&sw_off) || gpio_is_ready_dt(&sw_low) || gpio_is_ready_dt(&sw_high);

    bool gpios_config_done = (gpio_pin_configure_dt(&sw_off, GPIO_INPUT) == 0) &&
                             (gpio_pin_configure_dt(&sw_low, GPIO_INPUT) == 0) &&
                             (gpio_pin_configure_dt(&sw_high, GPIO_INPUT) == 0);

    bool gpios_irq_config_success =
        (gpio_pin_interrupt_configure_dt(&sw_off, GPIO_INT_EDGE_TO_ACTIVE) == 0) &&
        (gpio_pin_interrupt_configure_dt(&sw_low, GPIO_INT_EDGE_TO_ACTIVE) == 0) &&
        (gpio_pin_interrupt_configure_dt(&sw_high, GPIO_INT_EDGE_TO_ACTIVE) == 0);

    gpio_init_callback(&sw_callback_data, sw_pos_changed,
                       BIT(sw_off.pin) | BIT(sw_low.pin) | BIT(sw_high.pin));
    int add_callback_success =
        gpio_add_callback(sw_off.port, &sw_callback_data);  // assume the same port

    if (!gpios_ready || !gpios_config_done || !gpios_irq_config_success ||
        (add_callback_success != 0)) {
        LOG_ERR("Error: Failed during GPIO initialization");
        return -ENXIO;
    }
    update_privacy_sw_pos();
    return 0;
}

enum privacy_sw_pos switch_sensor_position() { return sw_pos; }
