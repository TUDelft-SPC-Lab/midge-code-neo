#ifndef MB_PROTOCOL_H
#define MB_PROTOCOL_H

#include <inttypes.h>
#include <stddef.h>

#define INTERFACE_CMD_DATA_SZ ((size_t)125U)
#define INTERFACE_CMD_SZ (INTERFACE_CMD_DATA_SZ + 3) // SOT+CMD_ID+DATA+EOT
#define INTERFACE_MAX_FILE_NAME ((size_t)12U*3U) // 8.3 Filename, 3 lvl depth

// ==== Miscellaneous data types ===== //
union __attribute__((packed)) BadgeAssignment {
    struct __attribute__((packed)) {
        uint16_t group : 4;
        uint16_t badge : 12;
    } badge_id; // little endian assumed
    uint16_t u16_all;
};

struct __attribute__((packed)) CustomAdvertisementData {
    uint16_t battery_mv;
    uint16_t active_sensor_bitflags;
    union BadgeAssignment badge_assignment;
};

// ==== Protocol messages =========== //
struct __attribute__((packed)) CmdSetupExperimentRequest{
  union BadgeAssignment badge_assignment;
  uint16_t experiment_id;
};

struct __attribute__((packed)) CmdSetupExperimentResponse{
  uint8_t status_code;
};

// uint16_t configured_datarate

struct __attribute__((packed)) CmdStatusRequest{
  uint64_t  millis_since_epoch;
};
struct __attribute__((packed)) CmdStatusResponse{
  uint8_t sync_status;
  uint8_t storage_init_status;
  uint8_t audio_init_status;
  uint8_t proximity_init_status;
  int16_t battery_millivolts;
  union BadgeAssignment badge_assignment;
  uint64_t sync_delta_ms;
};

struct __attribute__((packed)) CmdGetFWVersionRequest{
  uint16_t  reserved;
};

struct __attribute__((packed)) CmdGetFWVersionResponse{
  uint8_t version_str[32];
};

struct __attribute__((packed)) CmdStartMicRequest{
  uint16_t sample_id;
  uint8_t mode; // See @ref audio.h for mode definitions
};

struct __attribute__((packed)) CmdStartMicResponse{
  int32_t status_code;
};

struct __attribute__((packed)) CmdStopMicRequest{
  uint16_t  reserved;
};

struct __attribute__((packed)) CmdStopMicResponse{
  int32_t status_code;
};

struct __attribute__((packed)) CmdStartScanRequest{
  uint16_t sample_id;
  uint16_t window;
  uint16_t interval;
  uint16_t reserved;
};

struct __attribute__((packed)) CmdStartScanResponse{
  int32_t status_code;
};

struct __attribute__((packed)) CmdStopScanRequest{
  uint16_t  reserved;
};

struct __attribute__((packed)) CmdStopScanResponse{
  int32_t status_code;
};

struct __attribute__((packed)) CmdEraseSDRequest{
  uint16_t  reserved;
};

struct __attribute__((packed)) CmdEraseSDResponse{
  int32_t status_code;
};

struct __attribute__((packed)) CmdStartIMURequest{
  uint16_t sample_id;
  uint16_t acc_fsr;
  uint16_t gyr_fsr;
  uint16_t datarate;
};

struct __attribute__((packed)) CmdStartIMUResponse{
  int32_t status_code;
};

struct __attribute__((packed)) CmdStopIMURequest{
  uint16_t reserved;
};

struct __attribute__((packed)) CmdStopIMUResponse{
  int32_t status_code;
};


/// ==== File transfer related messages, not yet implemented ====
struct __attribute__((packed)) CmdGetFileIndexInfoRequest{
  int16_t index;
};

struct __attribute__((packed)) CmdGetFileIndexInfoResponse{
  int16_t index; // negative index for error code
  uint32_t size_bytes;
  uint8_t path[INTERFACE_MAX_FILE_NAME]; // 3 levels of depth, i.e. SD/folder/file, 8.3 names
};

struct __attribute__((packed)) CmdGetFileCRC32Request{
  uint8_t path[INTERFACE_MAX_FILE_NAME];
};

struct __attribute__((packed)) CmdGetFileCRC32Response{
  uint32_t crc32;
  int32_t status_code;
};

struct __attribute__((packed)) CmdDownloadFileChunkRequest{
  uint8_t path[INTERFACE_MAX_FILE_NAME];
  uint16_t offset;
};

struct __attribute__((packed)) CmdDownloadFileChunkResponse{
  uint8_t data[INTERFACE_CMD_DATA_SZ - 4];
  int16_t bytes; // 0 means end of file, negative encodes error code
};


#endif
