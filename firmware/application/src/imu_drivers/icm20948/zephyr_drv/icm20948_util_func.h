#ifndef ICM20948_UTIL_FUNC_H
#define ICM20948_UTIL_FUNC_H

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include "imu_interface.h"

bool check_cfg_matches(const struct device* dev, struct ImuConfig* config);

#endif
