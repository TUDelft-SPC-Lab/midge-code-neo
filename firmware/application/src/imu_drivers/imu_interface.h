/**
 * @brief This file defines the minimum expected functions from any IMU
 * driver. The functions defined here will be used by imu.c, which should not
 * depend on any specific IMU driver implementation.
 */

#ifndef MB_IMU_INTERFACE_H
#define MB_IMU_INTERFACE_H

#include <inttypes.h>

struct __attribute__((packed)) imu_3_axis_sample {
    float x;
    float y;
    float z;
};

struct __attribute__((packed)) imu_quaternion_sample {
    float x;  // x*sin(theta/2)
    float y;  // y*sin(theta/2)
    float z;  // z*sin(theta/2)
    float w;  // cos(theta/2)
};

struct imu_sample {
    uint64_t timestamp;
    union {
        struct imu_3_axis_sample axis;  // acc, gyro, mag
        float axis_data[3];
        struct imu_quaternion_sample quat;  // rotation vector
        float quat_data[4];
    };
};
struct imu_config {
    uint16_t acc_fsr;   // g
    uint16_t gyr_fsr;   // dps
    uint16_t datarate;  // Hz, applies to all sensors
};

struct imu_driver_interface {
    /**
     * @brief Initialize the IMU driver
     *
     * @return int status code, 0 for success, negative for error
     */
    int (*init)(void);

    /**
     * @brief Configure the IMU sampling parameters. Configuration can fail if
     * the given parameters are not supported by the underlying IMU driver.
     *
     * @param config Pointer to imu_config struct containing the desired configuration.
     * @return int status code, 0 for success, negative for error.
     */
    int (*set_config)(struct imu_config* config);

    /**
     * @brief Start IMU sensor sampling. Files are expected to be initialized
     * in this function.
     *
     */
    int (*start)(int sample_iter);

    /**
     * @brief Stop IMU sensor sampling. Files are expected to be closed in
     * this function.
     *
     */
    int (*stop)(void);
};

// Expected to be defined in the IMU driver implementation file
extern struct imu_driver_interface imu_drv_api;

#endif  // MB_IMU_INTERFACE_H
