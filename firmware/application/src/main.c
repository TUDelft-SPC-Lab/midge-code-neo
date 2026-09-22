#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "audio.h"
#include "battery_charge.h"
#include "cmd_processor.h"
#include "imu.h"
#include "midge_protocol.h"
#include "privacy_switch.h"
#include "proximity.h"
#include "status_led.h"
#include "storage.h"
#include "time_control.h"

LOG_MODULE_REGISTER(main_module);
struct init_item {
    int (*fn)(void);
    const char* name;
};

static int run_inits(const struct init_item* items, size_t n) {
    int ret = 0;
    for (size_t i = 0; i < n; i++) {
        ret = items[i].fn();
        if (ret < 0) {
            LOG_ERR("Failed to initialize %s", items[i].name);
            break;
        }
    }
    led_report_status(ret);
    return ret;
}

int main() {
    int ret = led_init();
    if (ret < 0) {
        LOG_ERR("Failed to initialize LED");
        return ret;
    }

    const struct init_item inits[] = {
        {storage_init_fs, "storage"},
        {battery_charge_init, "battery charge sensor"},
        {audio_sensor_init, "audio sensor"},
        {switch_sensor_init, "switch sensor"},
        {proximity_sensor_init, "proximity sensor"},
        {imu_sensor_init, "IMU sensor"},
        {cmd_processor_init, "command processor"},
    };

    ret = run_inits(inits, ARRAY_SIZE(inits));

    /*audio_sensor_start(2, 1);
    k_sleep(K_SECONDS(60*3));
    audio_sensor_stop();*/
    led_report_active(false);
    return ret;
}

/** HACKS and WORKAROUNDS */

#ifdef CONFIG_BOARD_MIDGE_BADGE_V2

union pin_cfg {
    struct {
        uint32_t dir : 1;
        uint32_t input : 1;
        uint32_t pull : 2;
        uint32_t _reserved1 : 4;
        uint32_t drive : 3;
        uint32_t _reserved2 : 5;
        uint32_t sense : 2;
    };
    uint32_t raw;
};

static int lsm6dso_int_pin_hack(void) {
    // force the int pin as a output 0 during init so the imu can enter i2c mode
    // an be recognized during driver init

    // This code manually controls the GPIO block described in
    // https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/gpio.html
    uint32_t addr = 0x50000000;
    //*(volatile uint32_t *)(addr | 0x518) = (1<<30);
    //*(volatile uint32_t *)(addr | 0x508) = (1<<30);
    union pin_cfg cfg = {
        .dir = 1,  // output
        .input = 0,
        .pull = 1,
    };
    *(volatile uint32_t*)(addr | 0x778) = cfg.raw;

    return 0;
}

// Hook into PRE_KERNEL_1 with priority 50
SYS_INIT(lsm6dso_int_pin_hack, PRE_KERNEL_1, 50);

#endif
