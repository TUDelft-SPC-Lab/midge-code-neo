#ifndef MB_BATTERY_CHARGE_H
#define MB_BATTERY_CHARGE_H

#include <inttypes.h>

int battery_charge_init();

int battery_charge_get_mv(int16_t* val_mv);

#endif
