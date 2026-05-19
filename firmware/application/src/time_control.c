#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/timeutil.h>

#include "storage.h"

LOG_MODULE_REGISTER(time_control);

// minimum delta to do a time-sync
// TODO This needs a more detailed analysis. BLE message delay can be up to
// 100ms or more in some cases, so time syncs should probably only be performed
// after time significant enough for the internal Mingle Midge clock to drift
// more than the BLE message delay, to ensure that the sync error value is
// Actually meaningful
#define BLE_LATENCY_THRESHOLD_MS 200
// Assume scenario where all the sampling tasks are active, latency is
// expected to be BLE latency + thread scheduling delay.
// the 90 ms value is based on empirical measurements of the current implementation, but it could be
// further refined with more detailed analysis and testing of the BLE latency and thread scheduling
// delay under different conditions. Most likely, to be defined in the board cmake file as
// it depends on time to complete other tasks, i.e. it depends on processing power
#define BLE_LATENCY_AVG_RX_MS 90

struct timeutil_sync_state sync_state = {};

const struct timeutil_sync_config time_config = {
    .ref_Hz = 1000,  // 1ms
    .local_Hz = 1000,
};

static enum {
    TIME_NOT_SYNCED = 0,
    TIME_SYNCED = 1,
    TIME_SYNCED_NO_CHANGE = 2,
    TIME_ERR = 3,
} status = TIME_NOT_SYNCED;

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

int time_control_reset() {
    status = TIME_NOT_SYNCED;
    return 0;
}

int time_control_update(uint64_t ref_ms) {
    switch (status) {
        case TIME_ERR:
            LOG_WRN("There was a previous error before this time control init");
            // fall through is intentional
        case TIME_NOT_SYNCED: {
            LOG_DBG("Time reference not initialized, call time_control_init first");
            return time_control_init(ref_ms);
        } break;
        case TIME_SYNCED: {
            LOG_DBG("Updating time reference");
            struct timeutil_sync_instant instant = {.local = k_uptime_get(), .ref = ref_ms};
            return timeutil_sync_state_update(&sync_state, &instant);
        } break;
        default:
            LOG_ERR("Time reference in undefined state, call time_control_init to reset");
            return -ENODEV;  // device not available
            break;
    }
}

uint64_t time_control_get_timestamp() {
    return k_uptime_get();  // return local time in ms
}

static uint64_t time_control_get_timestamp_interp() {
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

int time_control_sync(uint64_t ref_ms, int64_t* error_ms) {
    uint64_t current_interpolation = time_control_get_timestamp_interp();
    uint64_t current_internal = time_control_get_timestamp();

    struct timesync_entry entry = {
        .reference = ref_ms,
        .interpolated = current_interpolation,
        .internal = current_internal,
    };

    int64_t error;  // ref - interp
    int64_t delta;
    if (current_interpolation < (FW_BUILD_TIMESTAMP * 1000)) {
        // time has not been synced
        error = (int64_t)ref_ms;
        delta = error;
        LOG_WRN("Time has not been synced since boot, error value is intentionally inaccurate");
    } else {
        // Time was initialized, we can reasonably assume drift will not cause
        // a difference large enough to make the error value take less than 64
        // bits
        if (current_interpolation < ref_ms) {
            error = (int64_t)(ref_ms - current_interpolation);
            delta = error;
        } else {
            error = -(int64_t)(current_interpolation - ref_ms);
            delta = -error;
        }
    }
    *error_ms = error;
    int ret = 0;
#ifdef HEURISTIC_TIME_SYNC
    if (delta > BLE_LATENCY_THRESHOLD_MS) {
        LOG_INF("Performing time sync, error: %" PRId64 " ms, assumed latency: %d ms", error,
                BLE_LATENCY_AVG_RX_MS);
        ret = storage_write_timesync(&entry);
        if (ret < 0) {
            LOG_ERR("Failed to write time sync info to storage, status %d", ret);
            status = TIME_ERR;
        }
        ret = time_control_update(ref_ms + BLE_LATENCY_AVG_RX_MS);
        if (ret < 0) {
            LOG_ERR("Failed to perform the time sync");
            status = TIME_ERR;
        } else {
            status = TIME_SYNCED;
        }

    } else {
        status = TIME_SYNCED_NO_CHANGE;
        LOG_INF("Not performing time sync, error not significant enough");
    }
#else
    ret = storage_write_timesync(&entry);
    if (ret < 0) {
        LOG_ERR("Failed to write time sync info to storage, status %d", ret);
        status = TIME_ERR;
    }
    ret = time_control_update(ref_ms);
    if (ret < 0) {
        LOG_ERR("Failed to perform the time sync");
        status = TIME_ERR;
    } else {
        status = TIME_SYNCED;
    }
#endif
    return ret;
}

uint8_t time_control_get_status() { return (uint8_t)status; }
