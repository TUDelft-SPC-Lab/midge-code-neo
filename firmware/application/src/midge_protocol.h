#ifndef MB_PROTOCOL_H
#define MB_PROTOCOL_H

#include <inttypes.h>
#include <stddef.h>

#define INTERFACE_CMD_DATA_SZ ((size_t)512U + 4U)
#define INTERFACE_CMD_SZ (INTERFACE_CMD_DATA_SZ + 3)  // SOT+CMD_ID+DATA+EOT
#define INTERFACE_MAX_FILE_NAME ((size_t)12U * 3U)    // 8.3 Filename, 3 lvl depth

// ==== Miscellaneous data types ===== //
union __attribute__((packed)) badge_assignment {
    struct __attribute__((packed)) {
        uint16_t group : 4;
        uint16_t badge : 12;
    } badge_id;  // little endian assumed
    uint16_t u16_all;
};

struct __attribute__((packed)) custom_advertisement_data {
    uint16_t battery_mv;
    uint16_t active_sensor_bitflags;
    union badge_assignment badge_assignment;
};

enum CmdID {
    CMD_ID_SETUP_EXPERIMENT = 'A',
    CMD_ID_STATUS = 'B',
    CMD_ID_GET_FW_VERSION = 'C',
    CMD_ID_START_MIC = 'D',
    CMD_ID_STOP_MIC = 'E',
    CMD_ID_START_SCAN = 'F',
    CMD_ID_STOP_SCAN = 'G',
    CMD_ID_START_IMU = 'H',
    CMD_ID_STOP_IMU = 'I',
    CMD_ID_ERASE_SD = 'J',
    CMD_ID_ERASE_FILE = 'K',
    CMD_ID_GET_FREE_SD_SPACE = 'L',
    CMD_ID_GET_FILE_INDEX_INFO = 'M',
    CMD_ID_GET_FILE_CRC32 = 'N',
    CMD_ID_DOWNLOAD_FILE_CHUNK = 'O'
};

// ==== Protocol messages =========== //
struct __attribute__((packed)) cmd_setup_experiment_request {
    union badge_assignment badge_assignment;
    uint16_t experiment_id;
};

struct __attribute__((packed)) cmd_setup_experiment_response {
    int32_t status_code;
};

// uint16_t configured_datarate

struct __attribute__((packed)) cmd_status_request {
    uint64_t millis_since_epoch;
};
struct __attribute__((packed)) cmd_status_response {
    uint8_t sync_status;
    uint8_t storage_init_status;
    uint8_t audio_init_status;
    uint8_t proximity_init_status;
    int16_t battery_millivolts;
    union badge_assignment badge_assignment;
    int64_t sync_error_ms;  // ref - interp
};

struct __attribute__((packed)) cmd_get_fw_version_request {
    uint16_t reserved;
};

struct __attribute__((packed)) cmd_get_fw_version_response {
    uint8_t version_str[32];
};

struct __attribute__((packed)) cmd_start_mic_request {
    uint16_t sample_id;
    uint16_t high_sample_rate;
    uint16_t low_sample_rate_decimation;
    uint8_t mode;  // See @ref audio.h for mode definitions
};

struct __attribute__((packed)) cmd_start_mic_response {
    int32_t status_code;
};

struct __attribute__((packed)) cmd_stop_mic_request {
    uint16_t reserved;
};

struct __attribute__((packed)) cmd_stop_mic_response {
    int32_t status_code;
};

struct __attribute__((packed)) cmd_start_scan_request {
    uint16_t sample_id;
    uint16_t window;
    uint16_t interval;
    uint16_t reserved;
};

struct __attribute__((packed)) cmd_start_scan_response {
    int32_t status_code;
};

struct __attribute__((packed)) cmd_stop_scan_request {
    uint16_t reserved;
};

struct __attribute__((packed)) cmd_stop_scan_response {
    int32_t status_code;
};

struct __attribute__((packed)) cmd_start_imu_request {
    uint16_t sample_id;
    uint16_t acc_fsr;
    uint16_t gyr_fsr;
    uint16_t datarate;
};

struct __attribute__((packed)) cmd_start_imu_response {
    int32_t status_code;
};

struct __attribute__((packed)) cmd_stop_imu_request {
    uint16_t reserved;
};

struct __attribute__((packed)) cmd_stop_imu_response {
    int32_t status_code;
};

struct __attribute__((packed)) cmd_erase_sd_request {
    uint16_t reserved;
};

struct __attribute__((packed)) cmd_erase_sd_response {
    int32_t status_code;
};

struct __attribute__((packed)) cmd_erase_file_request {
    uint8_t path[INTERFACE_MAX_FILE_NAME];
};

struct __attribute__((packed)) cmd_erase_file_response {
    int32_t status_code;
};

struct __attribute__((packed)) cmd_get_free_sd_space_request {
    uint16_t reserved;
};

struct __attribute__((packed)) cmd_get_free_sd_space_response {
    uint32_t free_bytes;
};

/// ==== File transfer related messages, not yet implemented ====
struct __attribute__((packed)) cmd_get_file_index_info_request {
    int16_t index;
};

struct __attribute__((packed)) cmd_get_file_index_info_response {
    uint32_t size_bytes;
    int16_t index;                          // negative index for error code
    uint8_t path[INTERFACE_MAX_FILE_NAME];  // 3 levels of depth, i.e. SD/folder/file, 8.3 names
};

struct __attribute__((packed)) cmd_get_file_crc32_request {
    uint8_t path[INTERFACE_MAX_FILE_NAME];
};

struct __attribute__((packed)) cmd_get_file_crc32_response {
    uint32_t crc32;
    int32_t status_code;
};

struct __attribute__((packed)) cmd_download_file_chunk_request {
    uint8_t path[INTERFACE_MAX_FILE_NAME];
    uint32_t offset;  // opens file if 0
};

struct __attribute__((packed)) cmd_download_file_chunk_response {
    uint8_t data[INTERFACE_CMD_DATA_SZ - 4];
    int16_t bytes;  // 0 means end of file and closes file, negative encodes error code
};

/// ========= File entry formats ==============

struct __attribute__((packed)) timesync_entry {
    uint64_t reference;
    uint64_t interpolated;
};

struct __attribute__((packed)) proximity_sensor_entry {
    union {
        uint8_t u8;
        int8_t i8;
    } rssi;
    uint8_t mac_address[6];
    struct custom_advertisement_data advertised_data;
    uint64_t timestamp;
};

struct __attribute__((packed)) imu_3_axis_sample {
    float x;
    float y;
    float z;
};

struct __attribute__((packed)) imu_quaternion_sample {
    float x;  // x*sin(theta/2)
    float y;  // y*sin(theta/2)
    float z;  // z*sin(theta/2)
    float w;  // cos(theta/2)
};

struct __attribute__((packed)) imu_entry {
    uint64_t timestamp;
    union {
        struct imu_3_axis_sample axis;  // acc, gyro, mag
        float axis_data[3];
        struct imu_quaternion_sample quat;  // rotation vector
        float quat_data[4];
    };
};

#endif
