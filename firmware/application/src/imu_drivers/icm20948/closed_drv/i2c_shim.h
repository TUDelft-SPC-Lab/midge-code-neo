#ifndef I2C_SHIM_H_
#define I2C_SHIM_H_

#include <inttypes.h>

int imu_interrupt_disable(void);
int imu_interrupt_enable(void);
int imu_i2c_init(void);
int imu_read_reg(void* context, uint8_t reg, uint8_t* buf, uint32_t len);
int imu_write_reg(void* context, uint8_t reg, const uint8_t* buf, uint32_t len);
int imu_sleep_us(int us);

#endif /* I2C_SHIM_H_ */
