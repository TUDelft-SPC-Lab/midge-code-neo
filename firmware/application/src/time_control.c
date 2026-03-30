#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/timeutil.h>

LOG_MODULE_REGISTER(time_control);

struct timeutil_sync_state sync_state = {};

const struct timeutil_sync_config time_config = {
    .ref_Hz = 1000,  // 1ms
    .local_Hz = 1000,
};

static enum { TIME_NOT_SYNCED, TIME_SYNCED, TIME_ERR } status = TIME_NOT_SYNCED;

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
    switch (status) {
        case TIME_NOT_SYNCED: {
            LOG_DBG("Time reference not initialized, call time_control_init first");
            return time_control_init(ref_ms);
        } break;
        case TIME_SYNCED: {
            LOG_DBG("Updating time reference");
            struct timeutil_sync_instant instant = {.local = k_uptime_get(), .ref = ref_ms};
            return timeutil_sync_state_update(&sync_state, &instant);
        } break;
        case TIME_ERR:
        default:
            LOG_ERR("Time reference in error or undefined state, call time_control_init to reset");
            return -ENODEV;  // device not available
            break;
    }
}

uint64_t time_control_get_timestamp() {
    uint64_t ref;
    if (status == TIME_NOT_SYNCED) {
        LOG_ERR("Need to initialize the time reference first");
        return 0;  // return erratic timestamp
    }
    int rc = timeutil_sync_ref_from_local(&sync_state, k_uptime_get(), &ref);

    if (rc < 0) {
        LOG_ERR("error timestamp %d", rc);
        status = TIME_ERR;
        return 0;
    } else {
        LOG_DBG("got timestamp %llu", ref);
        return ref;
    }
}
