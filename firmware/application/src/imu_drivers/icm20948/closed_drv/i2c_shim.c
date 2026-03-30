#include <inttypes.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ICM20948_driver_interface.h"

static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(imu));
static const struct gpio_dt_spec imu_int = GPIO_DT_SPEC_GET(DT_ALIAS(imu_int), gpios);

LOG_MODULE_REGISTER(i2c_shim);

static struct gpio_callback imu_int_callback_data;

static void imu_int_work_handler(struct k_work* work) {
    (void)work;
    icm20948_service_isr();
}

K_WORK_DEFINE(imu_int_work, imu_int_work_handler);

int imu_interrupt_disable() { return gpio_pin_interrupt_configure_dt(&imu_int, GPIO_INT_DISABLE); }

int imu_interrupt_enable() {
    int ret = gpio_pin_interrupt_configure_dt(&imu_int, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        LOG_ERR("Failed to configure GPIO interrupt for IMU");
    }
    return ret;
}

void imu_int_callback_handler(const struct device* dev, struct gpio_callback* cb, uint32_t pins) {
    (void)dev;
    (void)cb;
    (void)pins;

    (void)k_work_submit_to_queue(&k_sys_work_q, &imu_int_work);
}

int imu_i2c_init() {
    bool ready = i2c_is_ready_dt(&imu_i2c);
    if (!ready) {
        LOG_ERR("I2C bus is not ready");
        return -ENODEV;
    }
    ready = gpio_is_ready_dt(&imu_int);
    if (!ready) {
        LOG_ERR("GPIO for IMU interrupt is not ready");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&imu_int, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Failed to configure GPIO pin for IMU interrupt");
        return ret;
    }
    gpio_init_callback(&imu_int_callback_data, imu_int_callback_handler, BIT(imu_int.pin));
    ret = gpio_add_callback(imu_int.port, &imu_int_callback_data);
    if (ret != 0) {
        LOG_ERR("Failed to add GPIO callback for IMU interrupt");
        return ret;
    }
    return 0;
}

int imu_read_reg(void* context, uint8_t reg, uint8_t* buf, uint32_t len) {
    return i2c_burst_read_dt(&imu_i2c, reg, buf, len);
}

int imu_write_reg(void* context, uint8_t reg, const uint8_t* buf, uint32_t len) {
    return i2c_burst_write_dt(&imu_i2c, reg, buf, len);
}

int imu_sleep_us(int us) { return k_msleep(us / 1000); }
