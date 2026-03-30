#ifndef MB_UTILITY_H
#define MB_UTILITY_H

#include <zephyr/kernel.h>

struct simple_work_ctx {
    struct k_work work;
    struct k_sem done;
    int ret;
};

#endif
