#include "imu_interface.h"

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "icm20948_util_func.h"
#include "storage.h"
#include "time_control.h"

LOG_MODULE_REGISTER(imu_interface);

const struct device* const imu_icm20948 = DEVICE_DT_GET_ONE(invensense_icm20948);

#define BUFFERED_SAMPLES 16

enum supported_sensors {
    SENSOR_ACCEL = 0,
    SENSOR_GYRO = 1,
    SENSOR_MAGNETO = 2,
};

#define SENSOR_CHANNELS 3  // accel, gyro, mag

static struct imu_entry imu_buffer[SENSOR_CHANNELS][2][BUFFERED_SAMPLES];

static struct ImuWriteSamplesWork {
    struct k_work work;
    struct k_sem done;
    int buffer_index;
} wr_samples_ctx;

static void imu_write_samples_work_handler(struct k_work* work) {
    struct ImuWriteSamplesWork* imu_work = CONTAINER_OF(work, struct ImuWriteSamplesWork, work);
    int buffer_index = imu_work->buffer_index;
    size_t sz = sizeof(struct imu_entry) * BUFFERED_SAMPLES;
    // write samples from imu_buffer[0][buffer_index], imu_buffer[1][buffer_index],
    // imu_buffer[2][buffer_index] to storage
    int ret_accel = storage_write(FILE_TYPE_ACCEL, imu_buffer[SENSOR_ACCEL][buffer_index], sz);
    int ret_gyro = storage_write(FILE_TYPE_GYRO, imu_buffer[SENSOR_GYRO][buffer_index], sz);
    int ret_magn = storage_write(FILE_TYPE_MAGNETO, imu_buffer[SENSOR_MAGNETO][buffer_index], sz);
    if (ret_accel < 0 || ret_gyro < 0 || ret_magn < 0) {
        LOG_ERR(
            "Failed to write imu samples, stopping sampling. accel status: %d, gyro status: %d, "
            "magn status: %d",
            ret_accel, ret_gyro, ret_magn);
        imu_drv_api.stop();

    } else {
        LOG_DBG("Successfully wrote imu samples, buffer index: %d", buffer_index);
    }
    k_sem_give(&imu_work->done);
}

struct {
    int buffer_index;
    int current_sample;
} sampling_state;

static int process_icm20948(const struct device* dev) {
    struct sensor_value accel[3];
    struct sensor_value gyro[3];
    struct sensor_value magn[3];
    uint64_t timestamp = time_control_get_timestamp();

    int rc = sensor_sample_fetch(dev);
    if (rc != 0) {
        LOG_ERR("sample fetch failed: %d", rc);
        return rc;
    }
    rc = sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, accel);
    if (rc != 0) {
        LOG_ERR("  accel <error: %d>\n", rc);
    }
    rc = sensor_channel_get(dev, SENSOR_CHAN_GYRO_XYZ, gyro);
    if (rc != 0) {
        LOG_ERR("  gyro <error: %d>\n", rc);
    }

    rc = sensor_channel_get(dev, SENSOR_CHAN_MAGN_XYZ, magn);
    if (rc != 0) {
        LOG_ERR("  magn  <error: %d>\n", rc);
    }

    if (rc != 0) {
        LOG_ERR("sample fetch/get failed: %d", rc);
    }

    struct imu_entry accel_sample = {
        .timestamp = timestamp,
        .axis = {.x = sensor_value_to_float(&accel[0]),
                 .y = sensor_value_to_float(&accel[1]),
                 .z = sensor_value_to_float(&accel[2])},
    };
    struct imu_entry gyro_sample = {
        .timestamp = timestamp,
        .axis = {.x = sensor_value_to_float(&gyro[0]),
                 .y = sensor_value_to_float(&gyro[1]),
                 .z = sensor_value_to_float(&gyro[2])},
    };
    struct imu_entry magn_sample = {
        .timestamp = timestamp,
        .axis = {.x = sensor_value_to_float(&magn[0]),
                 .y = sensor_value_to_float(&magn[1]),
                 .z = sensor_value_to_float(&magn[2])},
    };

    imu_buffer[SENSOR_ACCEL][sampling_state.buffer_index][sampling_state.current_sample] =
        accel_sample;
    imu_buffer[SENSOR_GYRO][sampling_state.buffer_index][sampling_state.current_sample] =
        gyro_sample;
    imu_buffer[SENSOR_MAGNETO][sampling_state.buffer_index][sampling_state.current_sample] =
        magn_sample;

    // select next place to write in buffer
    sampling_state.current_sample++;
    if (sampling_state.current_sample >= BUFFERED_SAMPLES) {
        // reset counter
        sampling_state.current_sample = 0;
        // check buffer write is done before submitting new work.
        rc = k_sem_take(&wr_samples_ctx.done, K_NO_WAIT);
        if (rc != 0) {
            LOG_ERR("Previous sample write not completed yet, dropping samples, error: %d", rc);
        } else {
            // buffer is full, submit work to write it and switch buffer index
            k_work_init(&wr_samples_ctx.work, imu_write_samples_work_handler);
            k_sem_init(&wr_samples_ctx.done, 0, 1);
            wr_samples_ctx.buffer_index = sampling_state.buffer_index;
            k_work_submit(&wr_samples_ctx.work);

            // switch buffer index
            sampling_state.buffer_index = (sampling_state.buffer_index + 1) % 2;
        }
    }

    return rc;
}

static struct sensor_trigger trigger;
static void handle_icm20948_drdy(const struct device* dev, const struct sensor_trigger* trig) {
    int rc = process_icm20948(dev);

    if (rc != 0) {
        LOG_ERR("cancelling trigger due to failure: %d", rc);
        (void)sensor_trigger_set(dev, trig, NULL);
        return;
    }
}

static int init(void) {
    if (!device_is_ready(imu_icm20948)) {
        LOG_ERR("Device %s is not ready", imu_icm20948->name);
        return -ENODEV;
    }
    return 0;
}

static int set_config(struct imu_config* config) {
    int rc = 0;
    // magnetometer data rate cannot be changed in runtime in this driver yet.
    /* Not yet supported. Remember this use SI units
    rc = sensor_attr_set(imu_icm20948, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE,
    &config->acc_fsr); if (rc != 0) { LOG_ERR("Failed to set accel fsr: %d", rc); return rc;
    }
    rc = sensor_attr_set(imu_icm20948, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE,
    &config->gyr_fsr); if (rc != 0) { LOG_ERR("Failed to set gyro fsr: %d", rc); return rc;
    }
    */
    if (check_cfg_matches(imu_icm20948, config)) {
        LOG_INF("Config matches existing settings, no changes made");
    } else {
        LOG_ERR(
            "Config does not match static settings (Driver does not support these runtime "
            "changes)");
        return -EINVAL;
    }
    struct sensor_value datarate_val = {
        .val1 = config->datarate,
        .val2 = 0,
    };

    rc = sensor_attr_set(imu_icm20948, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
                         &datarate_val);
    if (rc != 0) {
        LOG_ERR("Failed to set accel datarate: %d", rc);
        return rc;
    }
    rc = sensor_attr_set(imu_icm20948, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
                         &datarate_val);
    if (rc != 0) {
        LOG_ERR("Failed to set gyro datarate: %d", rc);
    }
    return rc;
}

static int start(int sample_iter) {
    // clear sampling state
    memset(&sampling_state, 0, sizeof(sampling_state));
    // start imu files
    int accel_ret = storage_init_sample_file(FILE_TYPE_ACCEL, sample_iter);
    int gyro_ret = storage_init_sample_file(FILE_TYPE_GYRO, sample_iter);
    int magneto_ret = storage_init_sample_file(FILE_TYPE_MAGNETO, sample_iter);

    int stat = (accel_ret != 0) || (gyro_ret != 0) || (magneto_ret != 0);
    if (stat) {
        LOG_ERR("Error initializing IMU sample files acc: %d - gyro: %d - magneto: %d", accel_ret,
                gyro_ret, magneto_ret);
        if (accel_ret == 0) {
            (void)storage_close(FILE_TYPE_ACCEL);
        }
        if (gyro_ret == 0) {
            (void)storage_close(FILE_TYPE_GYRO);
        }
        if (magneto_ret == 0) {
            (void)storage_close(FILE_TYPE_MAGNETO);
        }
        return -EAGAIN;
    }

    // required for check when switching buffers in process_icm20948
    k_sem_init(&wr_samples_ctx.done, 1, 1);

    // start sampling
    trigger = (struct sensor_trigger){
        .type = SENSOR_TRIG_DATA_READY,
        .chan = SENSOR_CHAN_ALL,
    };

    int ret = sensor_trigger_set(imu_icm20948, &trigger, handle_icm20948_drdy);
    if (ret != 0) {
        LOG_ERR("Failed to set trigger: %d", ret);
        (void)storage_close(FILE_TYPE_ACCEL);
        (void)storage_close(FILE_TYPE_GYRO);
        (void)storage_close(FILE_TYPE_MAGNETO);
    }
    return ret;
}

static int stop() {
    // stop sensors
    int ret = sensor_trigger_set(imu_icm20948, &trigger, NULL);  // icm20948_disable_sensors();
    // close imu files
    if (ret == 0) {
        (void)storage_close(FILE_TYPE_ACCEL);
        (void)storage_close(FILE_TYPE_GYRO);
        (void)storage_close(FILE_TYPE_MAGNETO);
    }
    return ret;
}

struct imu_driver_interface imu_drv_api = {
    .init = init,
    .set_config = set_config,
    .start = start,
    .stop = stop,
};
