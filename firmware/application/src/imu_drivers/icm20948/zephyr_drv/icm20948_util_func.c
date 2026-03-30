#include "icm20948_util_func.h"

#include <icm20948.h>
#include <icm20948_reg.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(icm20948_sampling);

/* Unused for now
static int accel_fsr_to_enum(int32_t fs) {
    switch (fs) {
        case 2:
            return ICM20948_DT_ACCEL_FS_2;
        case 4:
            return ICM20948_DT_ACCEL_FS_4;
        case 8:
            return ICM20948_DT_ACCEL_FS_8;
        case 16:
            return ICM20948_DT_ACCEL_FS_16;
        default:
            LOG_ERR("Unsupported accel fsr: %d", fs);
            return -ENOTSUP;
    }
}

static int gyro_fsr_to_enum(uint16_t dps) {
    switch (dps) {
        case 250:
            return ICM20948_DT_GYRO_FS_250;
        case 500:
            return ICM20948_DT_GYRO_FS_500;
        case 1000:
            return ICM20948_DT_GYRO_FS_1000;
        case 2000:
            return ICM20948_DT_GYRO_FS_2000;
        default:
            LOG_ERR("Unsupported gyro fsr: %d", dps);
            return -ENOTSUP;
    }
}*/

static uint16_t gyro_enum_to_fsr(uint8_t val) {
    switch (val) {
        case ICM20948_DT_GYRO_FS_250:
            return 250;
        case ICM20948_DT_GYRO_FS_500:
            return 500;
        case ICM20948_DT_GYRO_FS_1000:
            return 1000;
        case ICM20948_DT_GYRO_FS_2000:
            return 2000;
        default:
            return 0;
    }
}

static uint16_t accel_enum_to_fsr(uint8_t val) {
    switch (val) {
        case ICM20948_DT_ACCEL_FS_2:
            return 2;
        case ICM20948_DT_ACCEL_FS_4:
            return 4;
        case ICM20948_DT_ACCEL_FS_8:
            return 8;
        case ICM20948_DT_ACCEL_FS_16:
            return 16;
        default:
            return 0;
    }
}

static uint16_t mag_mode_to_data_rate(uint8_t val) {
    switch (val) {
        case ICM20948_DT_MAG_MODE_DISABLED:
            return 0;
        case ICM20948_DT_MAG_MODE_10HZ:
            return 10;
        case ICM20948_DT_MAG_MODE_20HZ:
            return 20;
        case ICM20948_DT_MAG_MODE_50HZ:
            return 50;
        case ICM20948_DT_MAG_MODE_100HZ:
            return 100;
        default:
            return 0;
    }
}

bool check_cfg_matches(const struct device* dev, struct imu_config* config) {
    const struct icm20948_config* cfg = (const struct icm20948_config*)dev->config;
    uint16_t mag_datarate = mag_mode_to_data_rate(cfg->mag_mode);
    uint16_t gyro_fsr = gyro_enum_to_fsr(cfg->gyro_fs);
    uint16_t accel_fsr = accel_enum_to_fsr(cfg->accel_fs);

    if (mag_datarate != config->datarate) {
        LOG_ERR("Mag datarate mismatch: expected %d, got %d", config->datarate, mag_datarate);
        return false;
    }
    if (gyro_fsr != config->gyr_fsr) {
        LOG_ERR("Gyro fsr mismatch: expected %d, got %d", config->gyr_fsr, gyro_fsr);
        return false;
    }
    if (accel_fsr != config->acc_fsr) {
        LOG_ERR("Accel fsr mismatch: expected %d, got %d", config->acc_fsr, accel_fsr);
        return false;
    }
    return true;
}
