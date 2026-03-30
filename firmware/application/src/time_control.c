#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/timeutil.h>

LOG_MODULE_REGISTER(time_control);

struct timeutil_sync_state sync_state = {};

const struct timeutil_sync_config time_config = {
    .ref_Hz = 1000,  // 1ms
    .local_Hz = 1000,
};

static enum { TIME_NOT_SYNCED, TIME_SYNCED } status = TIME_NOT_SYNCED;

int time_control_init(uint64_t ref_ms) {
    sync_state.cfg = &time_config;
    struct timeutil_sync_instant instant = {.local = k_uptime_get(), .ref = ref_ms};
    int ret = timeutil_sync_state_update(&sync_state, &instant);
    if (ret < 0) {
        LOG_ERR("Reference clock failed to be updated");
    } else {
        status = TIME_SYNCED;
    }
    return ret;
}

int time_control_update(uint64_t ref_ms) {
    if (status == TIME_NOT_SYNCED) {
        return time_control_init(ref_ms);
    } else {
        struct timeutil_sync_instant instant = {.local = k_uptime_get(), .ref = ref_ms};
        return timeutil_sync_state_update(&sync_state, &instant);
    }
}

uint64_t time_control_get_timestamp() {
    uint64_t ref;
    if (status == TIME_NOT_SYNCED) {
        LOG_ERR("Need to initialized the time reference first");
    }
    int status = timeutil_sync_ref_from_local(&sync_state, k_uptime_get(), &ref);

    if (status < 0) {
        LOG_ERR("error timestamp %d", status);
        return 0;
    } else {
        LOG_DBG("got timestamp %llu", ref);
        return ref;
    }
}
