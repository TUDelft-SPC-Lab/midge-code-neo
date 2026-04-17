#ifndef MB_TIME_CONTROL_H
#define MB_TIME_CONTROL_H

#include <inttypes.h>

int time_control_init(uint64_t ref_ms);

int time_control_reset();

int time_control_update(uint64_t ref_ms);

uint64_t time_control_get_timestamp();

int time_control_sync(uint64_t ref_ms, int64_t* error_ms);

uint8_t time_control_get_status();

#endif
