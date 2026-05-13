#include "audio.h"

#include <stdbool.h>
#include <string.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "midge_protocol.h"
#include "privacy_switch.h"
#include "storage.h"
#include "time_control.h"

LOG_MODULE_REGISTER(mb_audio);

#define MIC_PARAMS_NODE DT_ALIAS(mic_params)

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

const struct device* const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));

static const struct pdm_io_cfg microphone_cfg = {
    .min_pdm_clk_freq = DT_PROP(MIC_PARAMS_NODE, min_pdm_clk_freq),
    .max_pdm_clk_freq = DT_PROP(MIC_PARAMS_NODE, max_pdm_clk_freq),
    .min_pdm_clk_dc = DT_PROP(MIC_PARAMS_NODE, min_pdm_clk_dc),
    .max_pdm_clk_dc = DT_PROP(MIC_PARAMS_NODE, max_pdm_clk_dc),
};

struct pcm_stream_cfg stream = {
    .pcm_width = SAMPLE_BIT_WIDTH,
    .mem_slab = &mem_slab,
};

static struct {
    enum sensor_state state;
    struct dmic_cfg audio_config;
    uint16_t sample_iter;
    uint16_t high_sample_rate;
    uint8_t low_sample_rate_decimation;
} sensor_data = {.state = SENSOR_STATE_DISABLED,
                 .audio_config = {},
                 .sample_iter = 0,
                 .high_sample_rate = 20000,
                 .low_sample_rate_decimation = 16};

struct __attribute__((packed)) WavFileHeader {
    const uint8_t file_type_bloc_id[4];  // RIFF
    uint32_t file_size;                  // size of entire file minus 8 bytes

    // format bloc
    const uint8_t file_format_id[4];  // WAVE
    const uint8_t format_bloc_id[4];  // fmt
    const uint32_t format_bloc_size;  // 16 for 16-bit PCM
    const uint16_t audio_format;      // 1 for PCM
    uint16_t num_channels;            // 1 for mono, 2 for stereo
    uint32_t sample_rate;             // pcm_rate
    uint32_t byte_per_sec;            // sample_rate * num_channels * bits_per_sample/8
    uint16_t byte_per_bloc;           // num_channels * bits_per_sample/8
    const uint16_t bits_per_sample;

    // data bloc
    const uint8_t data_bloc_id[4];  // data
    uint32_t data_bloc_size;

    // data follows
} wav_hdr = {
    .file_type_bloc_id = {'R', 'I', 'F', 'F'},
    //.file_size = sizeof(data + header) - 8
    .file_format_id = {'W', 'A', 'V', 'E'},
    .format_bloc_id = {'f', 'm', 't', ' '},
    .format_bloc_size = 16,  // PCM,
    .audio_format = 1,       // PCM
    // .num_channels = 2,
    //.sample_rate = 20000,
    //.byte_per_sec = byte_per_sample * num_channels * sample_rate,
    //.byte_per_bloc = channels * 16 / 8,
    .bits_per_sample = SAMPLE_BIT_WIDTH,
    .data_bloc_id = {'d', 'a', 't', 'a'},
    //.data_bloc_size = sizeof(pcm data)
};

static int write_metadata(struct audio_meta_data* metadata) {
    char buffer[128];
    int len = snprintf(buffer, sizeof(buffer), "%" PRIu64 ", %d,%d,%d,%d, %d\n",
                       metadata->timestamp_ms, metadata->status_code, metadata->event_type,
                       metadata->frequency_hz, metadata->num_channels, metadata->decimation);
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
            sensor_data.state = SENSOR_STATE_ERR;
            break;
        }

        // write header in advance
        ret = storage_write(FILE_TYPE_AUDIO, &wav_hdr, sizeof(wav_hdr));
        if (ret < 0) {
            LOG_ERR("Failed to write wav header, status %d", ret);
            sensor_data.state = SENSOR_STATE_ERR;
            storage_close(FILE_TYPE_AUDIO);
            break;
        }

        ret = storage_init_sample_file(FILE_TYPE_AUDIO_METADATA, sensor_data.sample_iter);
        char csv_header[] = "timestamp(ms), status, event, freq, channels, decimation\n";
        if (ret == 0) {
            // -1 to exclude null terminator
            ret = storage_write(FILE_TYPE_AUDIO_METADATA, csv_header, sizeof(csv_header) - 1);
        }
        if (ret < 0) {
            LOG_ERR("Failed to open metadata file, status %d", ret);
            sensor_data.state = SENSOR_STATE_ERR;
            storage_close(FILE_TYPE_AUDIO);
            break;
        }

        // trigger start
        ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
        if (ret < 0) {
            // error on trigger
            LOG_ERR("Failed to trigger start");
            sensor_data.state = SENSOR_STATE_ERR;
            storage_close(FILE_TYPE_AUDIO);
            storage_close(FILE_TYPE_AUDIO_METADATA);
            break;
        } else {
            bool decimate = switch_sensor_position() == PRIVACY_SWITCH_POS_LOW ? true : false;
            int decimation = decimate ? sensor_data.low_sample_rate_decimation : 1;
            struct audio_meta_data metadata = {
                .timestamp_ms = time_control_get_timestamp(),
                .status_code = 0,
                .event_type = AUDIO_EVENT_TYPE_TRIGGER_START,
                .frequency_hz = sensor_data.audio_config.streams->pcm_rate,
                .num_channels = sensor_data.audio_config.channel.act_num_chan,
                .decimation = decimation,
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
            sensor_data.state = SENSOR_STATE_ERR;
            dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);
            storage_close(FILE_TYPE_AUDIO);
            storage_close(FILE_TYPE_AUDIO_METADATA);
        } else {
            sensor_data.state = SENSOR_STATE_ACTIVE;
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
    bool decimate = switch_sensor_position() == PRIVACY_SWITCH_POS_LOW ? true : false;
    uint8_t decimation = decimate ? sensor_data.low_sample_rate_decimation : 1;
    void* audio_buffer;
    uint32_t audio_buffer_size;
    int ret;
    if (sensor_data.state == SENSOR_STATE_ACTIVE) {
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
                    .decimation = decimation,
                };
                // drop all buffer data and re-init sampling
                k_mem_slab_init(&mem_slab, mem_slab_buffer, MAX_BLOCK_SIZE, BLOCK_COUNT);
                int retry_status = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
                if (retry_status < 0) {
                    LOG_ERR(
                        "Failed to retrigger start after sample read timeout, status %d, "
                        "aborting\n",
                        retry_status);
                    sensor_data.state = SENSOR_STATE_ERR;
                } else {
                    ret = write_metadata(&metadata);
                }
                // timestamp should be added to metadata file to keep track of dropped samples
            } else {
                LOG_ERR("unknown read err %d\n, sample probably dropped, aborting", ret);
                sensor_data.state = SENSOR_STATE_ERR;
            }
        } else {
            if (decimate) {
                int channels = sensor_data.audio_config.channel.act_num_chan;
                int step = BYTES_PER_SAMPLE * channels;
                int decimation = sensor_data.low_sample_rate_decimation;
                int jump = decimation * step;
                size_t decimated_size = audio_buffer_size / decimation;
                // uint8_t subsampled_buffer[decimated_size];

                uint8_t* subsampled_buffer_ptr =
                    (uint8_t*)audio_buffer + jump;  // in place compaction;
                for (int i = jump; i < audio_buffer_size; i += jump) {
                    memcpy(subsampled_buffer_ptr, (uint8_t*)audio_buffer + i, step);
                    subsampled_buffer_ptr += step;
                }

                ret = storage_write(FILE_TYPE_AUDIO, audio_buffer, decimated_size);
                wav_hdr.data_bloc_size += decimated_size;
            } else {
                ret = storage_write(FILE_TYPE_AUDIO, audio_buffer, audio_buffer_size);
                wav_hdr.data_bloc_size += audio_buffer_size;
            }
            k_mem_slab_free(&mem_slab, audio_buffer);
            if (ret < 0) {
                LOG_ERR("write sample failed \n");
                // sampling will stop, file system could be compromised.
                sensor_data.state = SENSOR_STATE_ERR;
            }
        }
    }

    if (sensor_data.state == SENSOR_STATE_ACTIVE) {
        k_work_reschedule(&ctx->process_work, K_NO_WAIT);
    } else {
        // trigger stop
        ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);
        if (ret < 0) {
            // error on trigger
            LOG_ERR("trigger stop failed, critical error\n");
            sensor_data.state = SENSOR_STATE_ERR;
        } else {
            struct audio_meta_data metadata = {
                .timestamp_ms = time_control_get_timestamp(),
                .status_code = ret,
                .event_type = AUDIO_EVENT_TYPE_TRIGGER_STOP,
                .frequency_hz = sensor_data.audio_config.streams->pcm_rate,
                .num_channels = sensor_data.audio_config.channel.act_num_chan,
                .decimation = decimation,
            };
            ret = write_metadata(&metadata);

            sensor_data.state = SENSOR_STATE_STOP;
            LOG_INF("finished sampling round");

            // update the wav header with correct data and total file size info
            ret = storage_seek_start(FILE_TYPE_AUDIO);
            if (ret < 0) {
                LOG_ERR("Failed to seek to start of audio file to update header, status %d", ret);
                sensor_data.state = SENSOR_STATE_ERR;
            } else {
                // add total data size to header
                wav_hdr.file_size = wav_hdr.data_bloc_size + sizeof(wav_hdr) - 8;
                ret = storage_write(FILE_TYPE_AUDIO, &wav_hdr, sizeof(wav_hdr));
                if (ret < 0) {
                    LOG_ERR("Failed to update wav header with file info, status %d", ret);
                    sensor_data.state = SENSOR_STATE_ERR;
                }
            }

            ret = storage_close(FILE_TYPE_AUDIO);
            int ret2 = storage_close(FILE_TYPE_AUDIO_METADATA);
            if ((ret < 0) || (ret2 < 0)) {
                LOG_ERR("Failed to close files, status samples: %d  metadata: %d", ret, ret2);
                sensor_data.state = SENSOR_STATE_ERR;
                ret = (ret < 0) ? ret : ret2;  // return the error code of the first failure, if any
            }
        }
        ctx->stop_ret = ret;
        k_sem_give(&ctx->stop_done);
    }
}

int audio_sensor_start(int sample_iter, uint16_t high_sample_rate,
                       uint8_t low_sample_rate_decimation, int mode) {
    // Validate input parameters
    if (mode != AUDIO_MODE_MONO && mode != AUDIO_MODE_STEREO) {
        LOG_ERR("Invalid audio mode %d", mode);
        return -EINVAL;
    }
    if (low_sample_rate_decimation == 0) {
        LOG_ERR("Low sample rate decimation cannot be zero");
        return -EINVAL;
    }
    if (high_sample_rate % low_sample_rate_decimation != 0) {
        LOG_ERR("High sample rate must be divisible by low sample rate decimation");
        return -EINVAL;
    }
    if (MAX_BLOCK_SIZE % low_sample_rate_decimation != 0) {
        LOG_ERR("Decimation must be a factor of the max block size");
        return -EINVAL;
    }
    if (high_sample_rate / low_sample_rate_decimation > 2000) {
        LOG_ERR(
            "The low sample rate decimation is too low. It must be high enough to produce "
            "privacy-preserving sample rates.");
        return -EINVAL;
    }

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
            sensor_data.state = SENSOR_STATE_STOP;
            return -EINVAL;
        }
    }

    switch (switch_pos) {
        // Both use the same sample freq, the output buffer is manually
        // decimated as the hw may not support low sample rates
        case PRIVACY_SWITCH_POS_LOW:
        case PRIVACY_SWITCH_POS_HIGH: {
            stream.pcm_rate = high_sample_rate;
            stream.block_size = MAX_BLOCK_SIZE;  // BLOCK_SIZE(HIGH_SAMPLE_RATE, 1);
        } break;
        case PRIVACY_SWITCH_POS_OFF:
        default: {
            sensor_data.state = SENSOR_STATE_DISABLED;
            return -EACCES;
        }
    }
    sensor_data.audio_config.streams = &stream;
    int ret = dmic_configure(dmic_dev, &sensor_data.audio_config);
    if (ret < 0) {
        LOG_ERR("DMIC configuration failed");
        return ret;
    }
    sensor_data.high_sample_rate = high_sample_rate;
    sensor_data.low_sample_rate_decimation = low_sample_rate_decimation;

    sensor_data.sample_iter = sample_iter;

    // add file info to header
    wav_hdr.num_channels = sensor_data.audio_config.channel.act_num_chan;
    bool decimate = switch_sensor_position() == PRIVACY_SWITCH_POS_LOW ? true : false;
    uint32_t decimation = decimate ? sensor_data.low_sample_rate_decimation : 1;
    wav_hdr.sample_rate = sensor_data.audio_config.streams->pcm_rate / decimation;
    wav_hdr.byte_per_sec = BYTES_PER_SAMPLE * wav_hdr.num_channels * wav_hdr.sample_rate;
    wav_hdr.byte_per_bloc = BYTES_PER_SAMPLE * wav_hdr.num_channels;
    wav_hdr.data_bloc_size = 0;  // will be updated as samples are written

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
    if (sensor_data.state != SENSOR_STATE_ACTIVE) {
        LOG_ERR("Invalid state to stop audio sampling, state %d", sensor_data.state);
        ret = -EPERM;
    } else {
        sensor_data.state = SENSOR_STATE_STOP;
        int ret = k_sem_take(&audio_sampling_work_ctx.stop_done, K_SECONDS(1));
        if (ret < 0) {
            LOG_ERR("Failed to take stop done semaphore, status %d, forcing work abort", ret);
            ret = k_work_cancel_delayable(&audio_sampling_work_ctx.process_work);
            sensor_data.state = SENSOR_STATE_ERR;
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
        sensor_data.state = SENSOR_STATE_ERR;
        return -1;
    }
    if (k_mem_slab_init(&mem_slab, mem_slab_buffer, MAX_BLOCK_SIZE, BLOCK_COUNT)) {
        LOG_ERR("Failed to initialize memory slab for audio samples");
        sensor_data.state = SENSOR_STATE_ERR;
        return -1;
    }
    sensor_data.state = SENSOR_STATE_STOP;
    LOG_INF("init ok");
    return 0;
}

int cmd_mic_start(uint8_t* data) {
    struct cmd_start_mic_request* req_data = (struct cmd_start_mic_request*)data;
    int ret = audio_sensor_start(req_data->sample_id, req_data->high_sample_rate,
                                 req_data->low_sample_rate_decimation, req_data->mode);
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
