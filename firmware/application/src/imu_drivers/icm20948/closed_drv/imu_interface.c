#include "imu_interface.h"

#include <zephyr/logging/log.h>

#include "ICM20948_driver_interface.h"
#include "errno.h"
#include "storage.h"

LOG_MODULE_REGISTER(imu_interface);

struct imu_sample imu_buffer[MAX_IMU_SOURCES][2][IMU_BUFFER_SIZE];

int imu_driver_init() { return icm20948_init(); }

int imu_driver_set_config(struct imu_config* config) {
    int ret = 0;
    ret |= icm20948_set_fsr(config->acc_fsr, config->gyr_fsr);
    ret |= icm20948_set_datarate(config->datarate);
    return ret;
}

int imu_driver_start(int sample_iter) {
    // start imu files
    int accel_ret = storage_init_sample_file(FILE_TYPE_ACCEL, sample_iter);
    int gyro_ret = storage_init_sample_file(FILE_TYPE_GYRO, sample_iter);
    int magneto_ret = storage_init_sample_file(FILE_TYPE_MAGNETO, sample_iter);
    int rot_ret = storage_init_sample_file(FILE_TYPE_ROTATION, sample_iter);
    int stat = (accel_ret != 0) || (gyro_ret != 0) || (magneto_ret != 0) || (rot_ret != 0);
    if (stat) {
        LOG_ERR("Error initializing IMU sample files acc: %d - gyro: %d - magneto: %d - rot: %d",
                accel_ret, gyro_ret, magneto_ret, rot_ret);
        if (accel_ret == 0) {
            storage_close(FILE_TYPE_ACCEL);
        }
        if (gyro_ret == 0) {
            storage_close(FILE_TYPE_GYRO);
        }
        if (magneto_ret == 0) {
            storage_close(FILE_TYPE_MAGNETO);
        }
        if (rot_ret == 0) {
            storage_close(FILE_TYPE_ROTATION);
        }
        return -EAGAIN;
    }
    return icm20948_enable_sensors();
}

int imu_driver_stop() {
    // stop sensors
    int ret = icm20948_disable_sensors();
    // close imu files
    if (ret == 0) {
        storage_close(FILE_TYPE_ACCEL);
        storage_close(FILE_TYPE_GYRO);
        storage_close(FILE_TYPE_MAGNETO);
        storage_close(FILE_TYPE_ROTATION);
    }
    return ret;
}

struct imu_driver_interface imu_drv_api = {
    .init = imu_driver_init,
    .set_config = imu_driver_set_config,
    .start = imu_driver_start,
    .stop = imu_driver_stop,
};
