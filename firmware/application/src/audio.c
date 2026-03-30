#include "audio.h"

#include <stdbool.h>
#include <string.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "midge_protocol.h"
#include "privacy_switch.h"
#include "storage.h"
#include "time_control.h"

LOG_MODULE_REGISTER(mb_audio);

#define MAX_SAMPLE_RATE 16000

#define SAMPLE_BIT_WIDTH 16
#define BYTES_PER_SAMPLE sizeof(int16_t)

/* Size of a block for 100 ms of audio data. */
#define BLOCK_SIZE(_sample_rate, _number_of_channels) \
    (BYTES_PER_SAMPLE * (_sample_rate / 10) * _number_of_channels)

/* Driver will allocate blocks from this slab to receive audio data into them.
 * Application, after getting a given block from the driver and processing its
 * data, needs to free that block.
 */
#define MAX_BLOCK_SIZE 4096  //(BLOCK_SIZE(MAX_SAMPLE_RATE, 2)) / 2
#define BLOCK_COUNT 5

struct k_mem_slab mem_slab;
char mem_slab_buffer[BLOCK_COUNT * MAX_BLOCK_SIZE] __aligned(4);

// K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 4);

#define HIGH_SAMPLE_RATE MAX_SAMPLE_RATE
#define LOW_SAMPLE_RATE_DECIMATION 16
#define LOW_SAMPLE_RATE (20000 / LOW_SAMPLE_RATE_DECIMATION)

const struct device* const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
/**
 * @brief Microphone configuration. Depends on hardware. Might be useful to
 * have the config defined as part of the device tree
 * For now, assumes usage of PDM Mic ST MP34DT05TR-A
 */
struct pdm_io_cfg microphone_cfg = {
    .min_pdm_clk_freq = 1200000,
    .max_pdm_clk_freq = 3250000,
    // usual value in datasheets: 40% min, 60% max, 50% typical
    .min_pdm_clk_dc = 40,
    .max_pdm_clk_dc = 60,
};

struct pcm_stream_cfg stream = {
    .pcm_width = SAMPLE_BIT_WIDTH,
    .mem_slab = &mem_slab,
};

static struct {
    enum audio_sensor_state state;
    struct dmic_cfg audio_config;
    uint16_t sample_iter;
} sensor_data = {.state = AUDIO_SENSOR_STATE_DISABLED, .sample_iter = 0, .audio_config = {}};

static int write_metadata(struct audio_meta_data* metadata) {
    char buffer[128];
    int len = snprintf(buffer, sizeof(buffer), "%" PRIu64 ", %d,%d,%d,%d\n", metadata->timestamp_ms,
                       metadata->status_code, metadata->event_type, metadata->frequency_hz,
                       metadata->num_channels);
    if (len < 0) {
        LOG_ERR("Failed to format audio metadata, status %d\n", len);
        return -EFAULT;
    }

    int ret = storage_write(FILE_TYPE_AUDIO_METADATA, buffer, len);
    if (ret < 0) {
        LOG_ERR("Failed to write audio metadata, status %d\n", ret);
        // not aborting on metadata write failure since the sample data will still
        // be written and the metadata is not critical, although it provides useful
        // context for the sample
    }
    return ret;
}

uint8_t audio_sensor_get_status() { return (uint8_t)sensor_data.state; }

struct audio_sampling_work_ctx {
    struct k_work init_work;
    struct k_sem init_done;
    int init_ret;
    struct k_work_delayable process_work;
    struct k_sem stop_done;
    int stop_ret;
    int inter_sample_delay_ms;
} audio_sampling_work_ctx;

static void audio_sample_process_work_handler(struct k_work* work);

static void audio_init_sampling_work_handler(struct k_work* work) {
    struct audio_sampling_work_ctx* ctx =
        CONTAINER_OF(work, struct audio_sampling_work_ctx, init_work);
    int ret = 0;
    do {
        ret = storage_init_sample_file(FILE_TYPE_AUDIO, sensor_data.sample_iter);
        if (ret < 0) {
            LOG_ERR("Failed to open sampling file, status %d", ret);
            sensor_data.state = AUDIO_SENSOR_STATE_ERR;
            break;
        }

        ret = storage_init_sample_file(FILE_TYPE_AUDIO_METADATA, sensor_data.sample_iter);
        char csv_header[] = "timestamp(ms), status, event, freq, channels\n";
        if (ret == 0) {
            ret = storage_write(FILE_TYPE_AUDIO_METADATA, csv_header, sizeof(csv_header));
        }
        if (ret < 0) {
            LOG_ERR("Failed to open metadata file, status %d", ret);
            sensor_data.state = AUDIO_SENSOR_STATE_ERR;
            storage_close(FILE_TYPE_AUDIO);
            break;
        }

        // trigger start
        ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
        if (ret < 0) {
            // error on trigger
            LOG_ERR("Failed to trigger start");
            sensor_data.state = AUDIO_SENSOR_STATE_ERR;
            storage_close(FILE_TYPE_AUDIO);
            storage_close(FILE_TYPE_AUDIO_METADATA);
            break;
        } else {
            struct audio_meta_data metadata = {
                .timestamp_ms = time_control_get_timestamp(),
                .status_code = 0,
                .event_type = AUDIO_EVENT_TYPE_TRIGGER_START,
                .frequency_hz = sensor_data.audio_config.streams->pcm_rate,
                .num_channels = sensor_data.audio_config.channel.act_num_chan,
            };
            write_metadata(&metadata);
        }

        // calculate the expected time between samples.
        ctx->inter_sample_delay_ms =
            (sensor_data.audio_config.streams->block_size * 1000) /
            (sensor_data.audio_config.streams->pcm_rate *
             sensor_data.audio_config.channel.act_num_chan * BYTES_PER_SAMPLE);

        // submit audio sample process to system queue
        LOG_INF("starting audio sampling");
        k_work_init_delayable(&ctx->process_work, audio_sample_process_work_handler);
        int ret = k_work_schedule(&ctx->process_work, K_MSEC(ctx->inter_sample_delay_ms));

        if (ret < 0) {
            LOG_ERR("Failed to schedule audio sample processing work, status %d", ret);
            sensor_data.state = AUDIO_SENSOR_STATE_ERR;
            dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);
            storage_close(FILE_TYPE_AUDIO);
            storage_close(FILE_TYPE_AUDIO_METADATA);
        } else {
            sensor_data.state = AUDIO_SENSOR_STATE_ACTIVE;
        }
    } while (0);
    if (ret < 0) {
    }

    ctx->init_ret = ret;
    k_sem_give(&ctx->init_done);
}

static void audio_sample_process_work_handler(struct k_work* work) {
    struct k_work_delayable* dwork = k_work_delayable_from_work(work);
    struct audio_sampling_work_ctx* ctx =
        CONTAINER_OF(dwork, struct audio_sampling_work_ctx, process_work);

    void* audio_buffer;
    uint32_t audio_buffer_size;
    int ret;
    if (sensor_data.state == AUDIO_SENSOR_STATE_ACTIVE) {
        // read buffer
        ret = dmic_read(dmic_dev, 0, &audio_buffer, &audio_buffer_size, ctx->inter_sample_delay_ms);
        if (ret < 0) {
            // error on trigger
            if (ret == -EBUSY || ret == -ENOMSG || ret == -EAGAIN) {
                LOG_INF("Sample read timeout reached, sample most likely dropped, retriggering\n");
                struct audio_meta_data metadata = {
                    .timestamp_ms = time_control_get_timestamp(),
                    .status_code = ret,
                    .event_type = AUDIO_EVENT_TYPE_TRIGGER_START,
                    .frequency_hz = sensor_data.audio_config.streams->pcm_rate,
                    .num_channels = sensor_data.audio_config.channel.act_num_chan,
                };
                // drop all buffer data and re-init sampling
                k_mem_slab_init(&mem_slab, mem_slab_buffer, MAX_BLOCK_SIZE, BLOCK_COUNT);
                int retry_status = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
                if (retry_status < 0) {
                    LOG_ERR(
                        "Failed to retrigger start after sample read timeout, status %d, "
                        "aborting\n",
                        retry_status);
                    sensor_data.state = AUDIO_SENSOR_STATE_ERR;
                } else {
                    ret = write_metadata(&metadata);
                }
                // timestamp should be added to metadata file to keep track of dropped samples
            } else {
                LOG_ERR("unknown read err %d\n, sample probably dropped, aborting", ret);
                sensor_data.state = AUDIO_SENSOR_STATE_ERR;
            }
        } else {
            ret = storage_write(FILE_TYPE_AUDIO, audio_buffer, audio_buffer_size);
            k_mem_slab_free(&mem_slab, audio_buffer);
            if (ret < 0) {
                LOG_ERR("write sample failed \n");
                // sampling will stop, file system could be compromised.
                sensor_data.state = AUDIO_SENSOR_STATE_ERR;
            }
        }
    }

    if (sensor_data.state == AUDIO_SENSOR_STATE_ACTIVE) {
        k_work_reschedule(&ctx->process_work, K_NO_WAIT);
    } else {
        // trigger stop
        ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);
        if (ret < 0) {
            // error on trigger
            LOG_ERR("trigger stop failed, critical error\n");
            sensor_data.state = AUDIO_SENSOR_STATE_ERR;
        } else {
            struct audio_meta_data metadata = {
                .timestamp_ms = time_control_get_timestamp(),
                .status_code = ret,
                .event_type = AUDIO_EVENT_TYPE_TRIGGER_STOP,
                .frequency_hz = sensor_data.audio_config.streams->pcm_rate,
                .num_channels = sensor_data.audio_config.channel.act_num_chan,
            };
            ret = write_metadata(&metadata);

            sensor_data.state = AUDIO_SENSOR_STATE_STOP;
            LOG_INF("finished sampling round");
            ret = storage_close(FILE_TYPE_AUDIO);
            int ret2 = storage_close(FILE_TYPE_AUDIO_METADATA);
            if ((ret < 0) || (ret2 < 0)) {
                LOG_ERR("Failed to close files, status samples: %d  metadata: %d", ret, ret2);
                sensor_data.state = AUDIO_SENSOR_STATE_ERR;
                ret = (ret < 0) ? ret : ret2;  // return the error code of the first failure, if any
            }
        }
        ctx->stop_ret = ret;
        k_sem_give(&ctx->stop_done);
    }
}

int audio_sensor_start(int sample_iter, int mode) {
    // Get sampling freq based on switch position
    enum privacy_sw_pos switch_pos = switch_sensor_position();

    // configurar canales
    memset(&sensor_data.audio_config, 0,
           sizeof(struct dmic_cfg));  // clear configuration

    sensor_data.audio_config.channel.req_num_streams = 1;
    sensor_data.audio_config.io = microphone_cfg;

    sensor_data.audio_config.channel.req_num_chan = mode;
    switch (mode) {
        case AUDIO_MODE_MONO: {
            sensor_data.audio_config.channel.req_chan_map_lo =
                dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
        } break;
        case AUDIO_MODE_STEREO: {
            sensor_data.audio_config.channel.req_chan_map_lo =
                dmic_build_channel_map(0, 0, PDM_CHAN_LEFT) |
                dmic_build_channel_map(1, 0, PDM_CHAN_RIGHT);
        } break;
        default: {
            sensor_data.state = AUDIO_SENSOR_STATE_ERR;
            return -EINVAL;
        }
    }

    switch (switch_pos) {
        case PRIVACY_SWITCH_POS_LOW: {
            stream.pcm_rate = LOW_SAMPLE_RATE;
            stream.block_size = MAX_BLOCK_SIZE;  // BLOCK_SIZE(LOW_SAMPLE_RATE, mode);
        } break;
        case PRIVACY_SWITCH_POS_HIGH: {
            stream.pcm_rate = HIGH_SAMPLE_RATE;
            stream.block_size = MAX_BLOCK_SIZE;  // BLOCK_SIZE(HIGH_SAMPLE_RATE, 1);
        } break;
        case PRIVACY_SWITCH_POS_OFF:
        default: {
            sensor_data.state = AUDIO_SENSOR_STATE_DISABLED;
            return -EACCES;
        }
    }
    sensor_data.audio_config.streams = &stream;
    int ret = dmic_configure(dmic_dev, &sensor_data.audio_config);
    if (ret < 0) {
        LOG_ERR("DMIC configuration failed");
        return ret;
    }

    sensor_data.sample_iter = sample_iter;

    memset(&audio_sampling_work_ctx, 0, sizeof(audio_sampling_work_ctx));
    k_sem_init(&audio_sampling_work_ctx.init_done, 0, 1);
    k_sem_init(&audio_sampling_work_ctx.stop_done, 0, 1);

    k_work_init(&audio_sampling_work_ctx.init_work, audio_init_sampling_work_handler);
    int submit_ret = k_work_submit(&audio_sampling_work_ctx.init_work);

    if (submit_ret < 0) {
        LOG_ERR("Failed to submit audio sampling init work, status %d", submit_ret);
        ret = submit_ret;
    } else {
        k_sem_take(&audio_sampling_work_ctx.init_done, K_FOREVER);
        ret = audio_sampling_work_ctx.init_ret;
    }
    return ret;
}

int audio_sensor_stop() {
    int ret = 0;
    if (sensor_data.state != AUDIO_SENSOR_STATE_ACTIVE) {
        LOG_ERR("Invalid state to stop audio sampling, state %d", sensor_data.state);
        ret = -EPERM;
    } else {
        sensor_data.state = AUDIO_SENSOR_STATE_STOP;
        int ret = k_sem_take(&audio_sampling_work_ctx.stop_done, K_SECONDS(1));
        if (ret < 0) {
            LOG_ERR("Failed to take stop done semaphore, status %d, forcing work abort", ret);
            ret = k_work_cancel_delayable(&audio_sampling_work_ctx.process_work);
            sensor_data.state = AUDIO_SENSOR_STATE_ERR;
            if (ret < 0) {
                LOG_ERR("Failed to cancel audio processing work, status %d", ret);
            }
        } else {
            ret = audio_sampling_work_ctx.stop_ret;
        }
    }
    return ret;
}

int audio_sensor_init(void) {
    if (!device_is_ready(dmic_dev)) {
        LOG_ERR("%s is not ready", dmic_dev->name);
        sensor_data.state = AUDIO_SENSOR_STATE_ERR;
        return -1;
    }
    if (k_mem_slab_init(&mem_slab, mem_slab_buffer, MAX_BLOCK_SIZE, BLOCK_COUNT)) {
        LOG_ERR("Failed to initialize memory slab for audio samples");
        sensor_data.state = AUDIO_SENSOR_STATE_ERR;
        return -1;
    }
    sensor_data.state = AUDIO_SENSOR_STATE_STOP;
    LOG_INF("init ok");
    return 0;
}

int cmd_mic_start(uint8_t* data) {
    struct cmd_start_mic_request* req_data = (struct cmd_start_mic_request*)data;
    int ret = audio_sensor_start(req_data->sample_id, req_data->mode);
    memset(data, 0, sizeof(struct cmd_start_mic_response));
    struct cmd_start_mic_response* resp_data = (struct cmd_start_mic_response*)data;
    resp_data->status_code = ret;
    return ret;
}

int cmd_mic_stop(uint8_t* data) {
    // struct cmd_stop_mic_request* req_data = (struct cmd_stop_mic_request*)data;
    struct cmd_stop_mic_response* resp_data = (struct cmd_stop_mic_response*)data;
    int ret = audio_sensor_stop();
    resp_data->status_code = ret;
    return ret;
}
