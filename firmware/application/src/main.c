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
        {battery_charge_init,  "battery charge sensor"},
        {audio_sensor_init,  "audio sensor"},
        {switch_sensor_init,  "switch sensor"},
        {proximity_sensor_init,  "proximity sensor"},
        {imu_sensor_init,  "IMU sensor"},
        {cmd_processor_init,  "command processor"},
    };

    ret = run_inits(inits, ARRAY_SIZE(inits));

    /*audio_sensor_start(2, 1);
    k_sleep(K_SECONDS(60*3));
    audio_sensor_stop();*/
    led_report_active(false);
    return ret;
}
