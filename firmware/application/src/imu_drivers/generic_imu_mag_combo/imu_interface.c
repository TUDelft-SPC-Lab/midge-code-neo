#include "imu_interface.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "midge_protocol.h"
#include "storage.h"
#include "time_control.h"

LOG_MODULE_REGISTER(imu_interface);

static const struct device* const imu_dev = DEVICE_DT_GET(DT_ALIAS(imu_dev));
static const struct device* const mag_dev = DEVICE_DT_GET_OR_NULL(DT_ALIAS(mag_dev));

#define BUFFERED_SAMPLES 16

enum supported_sensors {
    SENSOR_ACCEL = 0,
    SENSOR_GYRO = 1,
    SENSOR_MAGNETO = 2,
};

#define SENSOR_CHANNELS 3

static struct imu_entry imu_buffer[SENSOR_CHANNELS][2][BUFFERED_SAMPLES];

static struct ImuWriteSamplesWork {
    struct k_work work;
    struct k_sem done;
    int buffer_index;
} wr_samples_ctx;

static void imu_write_samples_work_handler(struct k_work* work);

static struct {
    int buffer_index;
    int current_sample;
} sampling_state;

static struct sensor_trigger trigger;

static int fetch_and_store_sample() {
    struct sensor_value accel[3];
    struct sensor_value gyro[3];
    struct sensor_value magn[3];

    uint64_t timestamp = time_control_get_timestamp();

    // 6 axis IMU
    int rc = sensor_sample_fetch(imu_dev);
    if (rc != 0) {
        LOG_ERR("IMU sample fetch failed: %d", rc);
        return rc;
    }

    rc = sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel);
    if (rc != 0) {
        LOG_ERR("accel channel read failed: %d", rc);
        return rc;
    }

    rc = sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro);
    if (rc != 0) {
        LOG_ERR("gyro channel read failed: %d", rc);
        return rc;
    }

    // magnetometer
    rc = sensor_sample_fetch(mag_dev);
    if (rc != 0) {
        LOG_WRN("Magnetometer sample fetch failed: %d", rc);
        // return rc;
    }

    rc = sensor_channel_get(mag_dev, SENSOR_CHAN_MAGN_XYZ, magn);
    if (rc != 0) {
        LOG_ERR("Magnetometer channel read failed: %d", rc);
        return rc;
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
    float gauss_to_uT = 100.0f;
    struct imu_entry magn_sample = {
        .timestamp = timestamp,
        .axis = {.x = sensor_value_to_float(&magn[0]) * gauss_to_uT,
                 .y = sensor_value_to_float(&magn[1]) * gauss_to_uT,
                 .z = sensor_value_to_float(&magn[2]) * gauss_to_uT},
    };

    imu_buffer[SENSOR_ACCEL][sampling_state.buffer_index][sampling_state.current_sample] =
        accel_sample;
    imu_buffer[SENSOR_GYRO][sampling_state.buffer_index][sampling_state.current_sample] =
        gyro_sample;
    imu_buffer[SENSOR_MAGNETO][sampling_state.buffer_index][sampling_state.current_sample] =
        magn_sample;

    sampling_state.current_sample++;
    if (sampling_state.current_sample >= BUFFERED_SAMPLES) {
        sampling_state.current_sample = 0;

        rc = k_sem_take(&wr_samples_ctx.done, K_NO_WAIT);
        if (rc != 0) {
            LOG_ERR("Previous sample write not completed yet, dropping samples: %d", rc);
        } else {
            k_work_init(&wr_samples_ctx.work, imu_write_samples_work_handler);
            wr_samples_ctx.buffer_index = sampling_state.buffer_index;
            k_work_submit(&wr_samples_ctx.work);
            sampling_state.buffer_index = (sampling_state.buffer_index + 1) % 2;
        }
    }

    return 0;
}

static void imu_write_samples_work_handler(struct k_work* work) {
    struct ImuWriteSamplesWork* imu_work = CONTAINER_OF(work, struct ImuWriteSamplesWork, work);
    int buffer_index = imu_work->buffer_index;
    size_t sz = sizeof(struct imu_entry) * BUFFERED_SAMPLES;

    int ret_accel = storage_write(FILE_TYPE_ACCEL, imu_buffer[SENSOR_ACCEL][buffer_index], sz);
    int ret_gyro = storage_write(FILE_TYPE_GYRO, imu_buffer[SENSOR_GYRO][buffer_index], sz);
    int ret_magn = storage_write(FILE_TYPE_MAGNETO, imu_buffer[SENSOR_MAGNETO][buffer_index], sz);
    if (ret_accel < 0 || ret_gyro < 0 || ret_magn < 0) {
        LOG_ERR("Failed to write imu samples, stopping sampling. accel: %d gyro: %d mag: %d",
                ret_accel, ret_gyro, ret_magn);
        imu_drv_api.stop();
    }

    k_sem_give(&imu_work->done);
}

static void handle_drdy(const struct device* dev, const struct sensor_trigger* trig) {
    int rc = fetch_and_store_sample(dev);
    if (rc != 0) {
        LOG_ERR("cancelling trigger due to failure: %d", rc);
        (void)sensor_trigger_set(dev, trig, NULL);
    }
}

static int init(void) {
    if (!device_is_ready(imu_dev)) {
        LOG_ERR("Device %s is not ready", imu_dev->name);
        return -ENODEV;
    }
    if (!device_is_ready(mag_dev)) {
        LOG_ERR("Device %s is not ready", mag_dev->name);
        return -ENODEV;
    }

    return 0;
}

static int set_config(struct imu_config* config) {
    struct sensor_value accel_fsr;
    sensor_g_to_ms2((int32_t)config->acc_fsr, &accel_fsr);
    struct sensor_value gyro_fsr;
    sensor_degrees_to_rad((int32_t)config->gyr_fsr, &gyro_fsr);
    struct sensor_value datarate = {.val1 = config->datarate, .val2 = 0};

    int rc = sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &accel_fsr);
    if (rc != 0) {
        LOG_WRN("Failed to set accel full scale: %d", rc);
    }

    rc = sensor_attr_set(imu_dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE, &gyro_fsr);
    if (rc != 0) {
        LOG_WRN("Failed to set gyro full scale: %d", rc);
    }

    rc = sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &datarate);
    if (rc != 0) {
        LOG_WRN("Failed to set accel datarate: %d", rc);
    }

    rc = sensor_attr_set(imu_dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &datarate);
    if (rc != 0) {
        LOG_WRN("Failed to set gyro datarate: %d", rc);
    }

    rc = sensor_attr_set(mag_dev, SENSOR_CHAN_MAGN_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &datarate);
    if (rc != 0) {
        LOG_WRN("Failed to set magnetometer datarate: %d", rc);
    }

    return 0;
}

static int start(int sample_iter) {
    memset(&sampling_state, 0, sizeof(sampling_state));

    int accel_ret = storage_init_sample_file(FILE_TYPE_ACCEL, sample_iter);
    int gyro_ret = storage_init_sample_file(FILE_TYPE_GYRO, sample_iter);
    int magneto_ret = storage_init_sample_file(FILE_TYPE_MAGNETO, sample_iter);

    int stat = (accel_ret != 0) || (gyro_ret != 0) || (magneto_ret != 0);
    if (stat) {
        LOG_ERR("Error initializing IMU sample files acc: %d gyro: %d mag: %d", accel_ret, gyro_ret,
                magneto_ret);
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

    k_sem_init(&wr_samples_ctx.done, 1, 1);
    k_work_init(&wr_samples_ctx.work, imu_write_samples_work_handler);

    trigger = (struct sensor_trigger){
        .type = SENSOR_TRIG_DATA_READY,
        .chan = SENSOR_CHAN_ACCEL_XYZ,  // odr is the same as gyr
    };

    int ret = sensor_trigger_set(imu_dev, &trigger, handle_drdy);
    if (ret != 0) {
        LOG_ERR("Failed to set trigger: %d", ret);
        (void)storage_close(FILE_TYPE_ACCEL);
        (void)storage_close(FILE_TYPE_GYRO);
        (void)storage_close(FILE_TYPE_MAGNETO);
    }

    return ret;
}

static int stop(void) {
    int ret = sensor_trigger_set(imu_dev, &trigger, NULL);
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
