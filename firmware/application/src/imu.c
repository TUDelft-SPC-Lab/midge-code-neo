#include "imu.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "imu_interface.h"
#include "midge_protocol.h"
#include "storage.h"
#include "utility.h"

LOG_MODULE_REGISTER(imu);

static uint8_t imu_sensor_state = SENSOR_STATE_DISABLED;

uint8_t imu_sensor_get_status() { return imu_sensor_state; }

int imu_sensor_init() {
    int ret = imu_drv_api.init();
    if (ret != 0) {
        imu_sensor_state = SENSOR_STATE_ERR;
        LOG_ERR("Failed to initialize IMU driver: %d", ret);
    } else {
        imu_sensor_state = SENSOR_STATE_STOP;
        LOG_INF("IMU driver initialized successfully");
    }
    return ret;
}

int imu_sensor_start(int sample_iter, uint16_t acc_fsr, uint16_t gyr_fsr, uint16_t datarate) {
    struct imu_config config = {
        .acc_fsr = acc_fsr,
        .gyr_fsr = gyr_fsr,
        .datarate = datarate,
    };
    // set config
    int ret = imu_drv_api.set_config(&config);
    if (ret != 0) {
        LOG_ERR("Failed to set IMU config: %d", ret);
        return ret;
    }

    // start sampling
    ret = imu_drv_api.start(sample_iter);
    if (ret != 0) {
        LOG_ERR("Failed to start IMU sampling: %d", ret);
        imu_sensor_state = SENSOR_STATE_ERR;
    } else {
        LOG_INF("IMU sampling started successfully");
        imu_sensor_state = SENSOR_STATE_ACTIVE;
    }
    return ret;
}

int imu_sensor_stop() {
    int ret = imu_drv_api.stop();
    if (ret != 0) {
        LOG_ERR("Failed to stop IMU sampling: %d", ret);
        imu_sensor_state = SENSOR_STATE_ERR;
    } else {
        LOG_INF("IMU sampling stopped successfully");
        imu_sensor_state = SENSOR_STATE_STOP;
    }
    return ret;
}

struct start_imu_work_ctx {
    struct k_work work;
    struct k_sem done;
    uint16_t sample_id;
    uint16_t acc_fsr;
    uint16_t gyr_fsr;
    uint16_t datarate;
    int ret;
};

static void start_imu_work_handler(struct k_work* work) {
    struct start_imu_work_ctx* ctx = CONTAINER_OF(work, struct start_imu_work_ctx, work);
    ctx->ret = imu_sensor_start(ctx->sample_id, ctx->acc_fsr, ctx->gyr_fsr, ctx->datarate);
    k_sem_give(&ctx->done);
}

int cmd_start_imu(uint8_t* data) {
    struct cmd_start_imu_request* req_data = (struct cmd_start_imu_request*)data;
    struct start_imu_work_ctx ctx = {.sample_id = req_data->sample_id,
                                     .acc_fsr = req_data->acc_fsr,
                                     .gyr_fsr = req_data->gyr_fsr,
                                     .datarate = req_data->datarate};
    k_sem_init(&ctx.done, 0, 1);
    k_work_init(&ctx.work, start_imu_work_handler);
    int ret = k_work_submit(&ctx.work);
    if (ret < 0) {
        LOG_ERR("Failed to submit start imu work: %d", ret);
    } else {
        ret = k_sem_take(&ctx.done, K_FOREVER);
        if (ret != 0) {
            LOG_ERR("Failed to take semaphore for start imu work: %d", ret);
        } else {
            ret = ctx.ret;
        }
    }

    struct cmd_start_imu_response* resp_data = (struct cmd_start_imu_response*)data;
    memset(resp_data, 0, sizeof(struct cmd_start_imu_response));
    resp_data->status_code = ret;
    return ret;
}

static void stop_imu_work_handler(struct k_work* work) {
    struct simple_work_ctx* ctx = CONTAINER_OF(work, struct simple_work_ctx, work);
    ctx->ret = imu_sensor_stop();
    k_sem_give(&ctx->done);
}

int cmd_stop_imu(uint8_t* data) {
    // struct cmd_stop_imu_request* req_data = (struct cmd_stop_imu_request*)data;
    struct cmd_stop_imu_response* resp_data = (struct cmd_stop_imu_response*)data;
    struct simple_work_ctx ctx;
    k_work_init(&ctx.work, stop_imu_work_handler);
    k_sem_init(&ctx.done, 0, 1);

    int ret;
    int submit_ret = k_work_submit(&ctx.work);
    if (submit_ret < 0) {
        ret = submit_ret;
    } else {
        k_sem_take(&ctx.done, K_FOREVER);
        ret = ctx.ret;
    }
    resp_data->status_code = ret;
    return ret;
}
