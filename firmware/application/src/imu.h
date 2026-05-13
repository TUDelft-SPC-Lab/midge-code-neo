#ifndef MBFW_IMU_H
#define MBFW_IMU_H

#include <inttypes.h>

uint8_t imu_sensor_get_status();

int imu_sensor_init();

int imu_sensor_start(int sample_iter, uint16_t acc_fsr, uint16_t gyr_fsr, uint16_t datarate);

int imu_sensor_stop();

int cmd_start_imu(uint8_t* data);

int cmd_stop_imu(uint8_t* data);

#endif  // MBFW_IMU_H
