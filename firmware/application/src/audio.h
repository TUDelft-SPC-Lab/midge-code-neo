#ifndef MB_AUDIO_H
#define MB_AUDIO_H

#include <inttypes.h>

uint8_t audio_sensor_get_status();

int audio_sensor_init();

enum audio_mode {
    AUDIO_MODE_MONO = 1,
    AUDIO_MODE_STEREO = 2,
};

struct audio_meta_data {
    uint64_t timestamp_ms;
    int status_code;
    enum {
        /// Multiple trigger start entries would mean samples were dropped, the timestamp provides
        /// an approximate reference to the timing of the dropped sample(s) although it is not exact
        AUDIO_EVENT_TYPE_TRIGGER_START = 0,
        AUDIO_EVENT_TYPE_TRIGGER_STOP = 1
    } event_type;
    uint16_t frequency_hz;  // only used for trigger start events to indicate the sampling frequency
                            // of the recorded samples
    uint8_t num_channels;
    uint8_t decimation;
};

// int proximity_sensor_change_config(uint16_t interval, uint16_t window);

int audio_sensor_start(int sample_iter, uint16_t high_sample_rate,
                       uint8_t low_sample_rate_decimation, int mode);

int audio_sensor_stop();

int cmd_mic_start(uint8_t* data);
int cmd_mic_stop(uint8_t* data);

#endif
