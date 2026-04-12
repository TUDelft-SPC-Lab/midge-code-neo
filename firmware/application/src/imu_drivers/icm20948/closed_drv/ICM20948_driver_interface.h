#ifndef ICM20948_DRIVER_INTERFACE_H_
#define ICM20948_DRIVER_INTERFACE_H_

#include <inttypes.h>

#include "imu_interface.h"

#define ICM_20948_ADDR 0x68
#define AK0991x_DEFAULT_I2C_ADDR 0x0C   /* The default I2C address for AK0991x Magnetometers */
#define AK0991x_SECONDARY_I2C_ADDR 0x0E /* The secondary I2C address for AK0991x Magnetometers */

#define DEF_ST_ACCEL_FS 2       // self testrange g
#define DEF_ST_GYRO_FS_DPS 250  // rself test ange 250 dps
#define DEF_ST_SCALE 32768      // self test data width
#define DEF_SELFTEST_GYRO_SENS (DEF_ST_SCALE / DEF_ST_GYRO_FS_DPS)
#define DEF_ST_ACCEL_FS_MG 2000  // range in micro teslas
#define INV20948_ABS(x) (((x) < 0) ? -(x) : (x))

typedef enum {
    ACCEL = 0,
    //	ACCEL_RAW,
    GYRO = 1,
    //	GYRO_RAW,
    MAG = 2,
    //	MAG_RAW,
    ROTATION_VECTOR = 3,
    //	GAME_ROTATION_VECTOR,
    //	GEOMAGNETIC_ROTATION_VECTOR,
} imu_source_t;

#define MAX_IMU_SOURCES 4

#define IMU_BUFFER_SIZE 16

typedef struct imu_entry imu_sample_t;
// sd_chunk is 24bytes(struct size) * IMU_BUFFER_SIZE, and we want this to be multiple of 512
// (sdcard block size)

extern struct imu_entry imu_buffer[MAX_IMU_SOURCES][2][IMU_BUFFER_SIZE];

extern const char* imu_sensor_name[MAX_IMU_SOURCES];

int icm20948_init(void);
int icm20948_set_fsr(uint32_t acc_fsr, uint32_t gyr_fsr);
int icm20948_set_datarate(uint8_t datarate);
int icm20948_enable_sensors(void);
int icm20948_disable_sensors(void);
void icm20948_service_isr();
uint8_t inv_icm20948_get_self_test_done(void);
uint16_t get_acc_x(void);
uint16_t get_acc_y(void);
uint16_t get_acc_z(void);
uint16_t get_mag_x(void);
uint16_t get_mag_y(void);
uint16_t get_mag_z(void);
uint16_t get_gyr_x(void);
uint16_t get_gyr_y(void);
uint16_t get_gyr_z(void);
uint16_t get_rot_x(void);
uint16_t get_rot_y(void);
uint16_t get_rot_z(void);
uint16_t get_rot_w(void);
uint32_t get_acc_fsr(void);
uint32_t get_gyr_fsr(void);
uint8_t get_datarate(void);

#endif /* ICM20948_DRIVER_INTERFACE_H_ */
