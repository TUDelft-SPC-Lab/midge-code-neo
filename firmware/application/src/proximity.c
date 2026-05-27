#include "proximity.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

#include "midge_protocol.h"
#include "storage.h"
#include "time_control.h"
#include "utility.h"

LOG_MODULE_REGISTER(mb_proximity);

static struct bt_le_scan_param scan_param = {
    .type = BT_LE_SCAN_TYPE_PASSIVE,
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = 0x0010,
    .window = 0x0010,
};

// Selected to be a bit over 512 bytes of data, to be close to sd card block size
#define BUFFERED_SAMPLES 25

static struct {
    enum sensor_state state;
    int sample_cnt;
    int buffer_index;
    struct proximity_sensor_entry buffered_samples[2][BUFFERED_SAMPLES];
} sensor_data = {
    .state = SENSOR_STATE_DISABLED,
    .sample_cnt = 0,
    .buffer_index = 0,
};

int proximity_sensor_init() {
    // check bt already init
    if (sensor_data.state != SENSOR_STATE_DISABLED) {
        LOG_INF("already initialized");
        return -EPERM;
    }
    sensor_data.state = SENSOR_STATE_STOP;
    return 0;
}

uint8_t proximity_sensor_get_status() { return sensor_data.state; }

int proximity_sensor_change_config(uint16_t interval, uint16_t window) {
    scan_param.interval = interval;
    scan_param.window = window;
    return 0;
}

struct scan_data_parse_out {
    struct custom_advertisement_data* adv_data;
    bool valid_name;
    bool valid_adv;
};

static bool scan_data_parse(struct bt_data* data, void* out) {
    struct scan_data_parse_out* parse_out = (struct scan_data_parse_out*)out;
    if (data->type == BT_DATA_NAME_COMPLETE) {
        if (strncmp((char*)data->data, CONFIG_BT_DEVICE_NAME, data->data_len) == 0) {
            parse_out->valid_name = true;
            return !parse_out->valid_adv;  // if adv ready, stop parsing
        } else {
            parse_out->valid_name = false;
            return false;  // stop parsing, not valid
        }
    } else if (data->type == BT_DATA_MANUFACTURER_DATA) {
        if (data->data_len == sizeof(struct custom_advertisement_data)) {
            parse_out->adv_data = (struct custom_advertisement_data*)(data->data);
            parse_out->valid_adv = true;
            return parse_out->valid_name;  // if name ready, stop parsing
        } else {
            parse_out->valid_adv = false;
            return false;
        }
    }
    return true;  // continue parsing
}

/**
 * @brief Required to keep file management in the system workqueu context
 *
 * @param work
 */
static void scan_write_samples_work_handler(struct k_work* work) {
    struct simple_work_ctx* ctx = CONTAINER_OF(work, struct simple_work_ctx, work);
    int index = (sensor_data.buffer_index + 1) % 2;
    ctx->ret = storage_write(FILE_TYPE_PROXIMITY, sensor_data.buffered_samples[index],
                             sizeof(struct proximity_sensor_entry) * BUFFERED_SAMPLES);

    k_sem_give(&ctx->done);
}

K_MUTEX_DEFINE(proximity_sensor_mutex);
void scan_callback(const bt_addr_le_t* addr, int8_t rssi, uint8_t adv_type,
                   struct net_buf_simple* buf) {
    if (k_mutex_lock(&proximity_sensor_mutex, K_MSEC(50)) == 0) {
        struct proximity_sensor_entry* sample =
            &sensor_data.buffered_samples[sensor_data.buffer_index][sensor_data.sample_cnt];
        sample->rssi.i8 = rssi;
        sample->timestamp = time_control_get_timestamp();
        memcpy(sample->mac_address, addr->a.val, 6);
        // obtain the advertised data
        struct scan_data_parse_out parse_out = {
            .adv_data = &sample->advertised_data,
            .valid_name = false,
            .valid_adv = false,
        };
        bt_data_parse(buf, scan_data_parse, &parse_out);
        if (!parse_out.valid_name || !parse_out.valid_adv) {
            k_mutex_unlock(&proximity_sensor_mutex);
            return;
        }
        sample->advertised_data = *parse_out.adv_data;
        sensor_data.sample_cnt++;
        if (sensor_data.sample_cnt == BUFFERED_SAMPLES) {
            sensor_data.buffer_index = (sensor_data.buffer_index + 1) % 2;
            sensor_data.sample_cnt = 0;
            k_mutex_unlock(&proximity_sensor_mutex);

            struct simple_work_ctx ctx;
            k_work_init(&ctx.work, scan_write_samples_work_handler);
            k_sem_init(&ctx.done, 0, 1);
            k_work_submit(&ctx.work);

            int retw = k_sem_take(&ctx.done, K_FOREVER);
            if (retw != 0) {
                LOG_ERR("failed to take semaphore for scan write work");
            } else if (ctx.ret < 0) {
                LOG_ERR("failed to write proximity samples in work handler");
            } else {
                // LOG_INF("wrote %d proximity samples to storage", BUFFERED_SAMPLES);
            }
        } else {
            k_mutex_unlock(&proximity_sensor_mutex);
        }
    } else {
        LOG_ERR("failed to add sample");
    }
}

struct ProximitySensorStartWorkCtx {
    struct k_work work;
    struct k_sem done;
    int ret;
    uint16_t sample_id;
};

static void proximity_sensor_start_work_handler(struct k_work* work) {
    struct ProximitySensorStartWorkCtx* ctx =
        CONTAINER_OF(work, struct ProximitySensorStartWorkCtx, work);
    int ret = 0;
    do {
        // open new file
        ret = storage_init_sample_file(FILE_TYPE_PROXIMITY, ctx->sample_id);
        if (ret < 0) {
            LOG_ERR("failed to init proximity sample file");
            break;
        }
        // start scan
        ret = bt_le_scan_start(&scan_param, scan_callback);
        if (ret != 0) {
            LOG_ERR("failed to start scan");
            if (storage_close(FILE_TYPE_PROXIMITY) != 0) {
                LOG_ERR("FATAL: failed to close proximity sample file");
            }
        } else {
            sensor_data.state = SENSOR_STATE_ACTIVE;
            sensor_data.sample_cnt = 0;
        }
    } while (0);
    ctx->ret = ret;
    k_sem_give(&ctx->done);
}

void proximity_sensor_stop_work_handler(struct k_work* work) {
    struct simple_work_ctx* ctx = CONTAINER_OF(work, struct simple_work_ctx, work);

    int ret;
    do {
        ret = bt_le_scan_stop();
        if (ret != 0) {
            LOG_ERR("failed to stop scan %d", ret);
            break;
        }

        // write remaining data
        if (sensor_data.sample_cnt != 0) {
            ret = storage_write(FILE_TYPE_PROXIMITY,
                                sensor_data.buffered_samples[sensor_data.buffer_index],
                                sizeof(struct proximity_sensor_entry) * sensor_data.sample_cnt);
            if (ret < 0) {
                int close_ret = storage_close(FILE_TYPE_PROXIMITY);  // ignore return
                LOG_ERR("failed to write remaining proximity samples, write:%d close:%d", ret,
                        close_ret);
                sensor_data.state = SENSOR_STATE_ERR;
                break;
            }
        }
        // close file
        ret = storage_close(FILE_TYPE_PROXIMITY);
        if (ret < 0) {
            LOG_ERR("failed to close proximity sample file");
            sensor_data.state = SENSOR_STATE_ERR;
            break;
        } else {
            sensor_data.state = SENSOR_STATE_STOP;
        }
    } while (0);

    ctx->ret = ret;
    k_sem_give(&ctx->done);
}

int proximity_sensor_start(int sample_iter) {
    struct ProximitySensorStartWorkCtx ctx;
    ctx.sample_id = sample_iter;
    k_work_init(&ctx.work, proximity_sensor_start_work_handler);
    k_sem_init(&ctx.done, 0, 1);
    int ret = k_work_submit(&ctx.work);

    if (ret < 0) {
        LOG_ERR("failed to submit proximity sensor start work");
    } else {
        ret = k_sem_take(&ctx.done, K_FOREVER);
        if (ret != 0) {
            LOG_ERR("failed to take semaphore for proximity sensor start work");
        } else {
            ret = ctx.ret;
        }
    }
    return ret;
}

int proximity_sensor_stop() {
    struct simple_work_ctx ctx;
    k_work_init(&ctx.work, proximity_sensor_stop_work_handler);
    k_sem_init(&ctx.done, 0, 1);
    int ret = k_work_submit(&ctx.work);
    if (ret < 0) {
        LOG_ERR("failed to submit proximity sensor stop work");
    } else {
        ret = k_sem_take(&ctx.done, K_FOREVER);
        if (ret != 0) {
            LOG_ERR("failed to take semaphore for proximity sensor stop work");
        } else {
            ret = ctx.ret;
        }
    }
    return ret;
}

int cmd_scan_start(uint8_t* data) {
    struct cmd_start_scan_request* req_data = (struct cmd_start_scan_request*)data;
    struct cmd_start_scan_response* resp_data = (struct cmd_start_scan_response*)data;
    int ret = proximity_sensor_change_config(req_data->interval, req_data->window);
    if (ret == 0) {
        ret = proximity_sensor_start(req_data->sample_id);
    }
    memset(resp_data, 0, sizeof(struct cmd_start_scan_response));
    resp_data->status_code = ret;
    return ret;
}

int cmd_scan_stop(uint8_t* data) {
    // struct cmd_stop_scan_request* req_data = (struct cmd_stop_scan_request*)data;
    struct cmd_stop_scan_response* resp_data = (struct cmd_stop_scan_response*)data;
    int ret = proximity_sensor_stop();
    resp_data->status_code = ret;
    return ret;
}
