#ifndef MBFW_STORAGE_H
#define MBFW_STORAGE_H

#include <inttypes.h>
#include <stddef.h>

#define DISK_NAME "SD"
#define DISK_MOUNT_POINT "/" DISK_NAME ":"
// static const char* disk_pdrv = DISK_NAME;              // for IOCTL operations

enum mb_storage_status {
    MB_STORAGE_STATUS_UNINIT = 0,
    MB_STORAGE_STATUS_INIT_OK_INACTIVE = 1,
    MB_STORAGE_STATUS_INIT_OK_ACTIVE = 2,
    MB_STORAGE_STATUS_INIT_OK_MISC_OP_ACTIVE = 3,
    MB_STORAGE_STATUS_INIT_ERR = 4,
};

enum mb_file_type {
    FILE_TYPE_PROXIMITY = 0,
    FILE_TYPE_AUDIO = 1,
    FILE_TYPE_AUDIO_METADATA = 2,  //?< Timestamps per sample, or just register dropped samples?
    FILE_TYPE_ACCEL = 3,
    FILE_TYPE_GYRO = 4,
    FILE_TYPE_MAGNETO = 5,
    FILE_TYPE_ROTATION = 6,
    FILE_TYPE_TIMESYNC = 7,

    // update prj.conf if this is changed to increase CONFIG_FS_FATFS_NUM_FILES
    FILE_TYPE_MAX = 7,
};

int storage_init_fs();

int storage_deinit_fs();

uint8_t storage_get_status();

int storage_init_experiment(int id);

typedef int (*per_file_cb_t)(char* file_path, int16_t index, void* context);

int storage_do_per_file_in_sd(per_file_cb_t cb, void* context);

    /**
 * @brief
 *
 * @param path path to a single folder or file. In case the path matches the
 * base dir for the SD card (mount point name) then this will perform a deletion
 * of up to 2 levels of depth, i.e. SD/folder/file. Subfolders at depth level 2
 * i.e. SD/folder/subfolder will only be deleted if empty. This is done to
 * avoid recursion which is undesirable in embedded systems
 * @return int
 */
int storage_erase(char* path);

/**
 * @brief
 *
 * @param file_type
 * @param sample_iter
 * @param name_data Data defined by the caller to control naming of the file, e.g. audio mono/stereo
 * naming info
 * @return int
 */
int storage_init_sample_file(enum mb_file_type file_type, int sample_iter);

uint16_t storage_get_active_sensor_bitflags();

int storage_write(enum mb_file_type file_type, void* data, size_t size);

int storage_close(enum mb_file_type file_type);

int cmd_erase_sd(uint8_t* data);

int cmd_get_free_sd_space(uint8_t* data);

int cmd_get_file_index_info(uint8_t* data);

int cmd_get_file_crc32(uint8_t* data);

int cmd_download_file_chunk(uint8_t* data);

#endif
