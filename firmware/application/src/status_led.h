#ifndef STATUS_LED_H_
#define STATUS_LED_H_

#include <stdbool.h>

int led_init(void);

int led_report_status(int status_code);

int led_report_active(bool active);

int led_identify(void);

#endif
