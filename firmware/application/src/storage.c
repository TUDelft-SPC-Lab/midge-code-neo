#include "storage.h"

#include <errno.h>
#include <ff.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/crc.h>

#include "midge_protocol.h"
#include "status_led.h"

#define MAX_PATH_LEN INTERFACE_MAX_FILE_NAME

LOG_MODULE_REGISTER(mb_storage);

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {.type = FS_FATFS, .fs_data = &fat_fs, .mnt_point = DISK_MOUNT_POINT};
K_MUTEX_DEFINE(storage_mutex);
union mb_storage_status storage_status = {.all_flags = 0};

static void storage_sync_work_handler(struct k_work* work);
static void storage_sync_timer_handler(struct k_timer* timer);

K_WORK_DEFINE(storage_sync_work, storage_sync_work_handler);
K_TIMER_DEFINE(storage_sync_timer, storage_sync_timer_handler, NULL);

static char active_experiment_dir[MAX_PATH_LEN] = "DUMMY PATH";

enum mb_file_status {
    MB_FILE_STATUS_INACTIVE = 0,
    MB_FILE_STATUS_ACTIVE = 1,
    MB_FILE_STATUS_ERR = 2
};

struct file_info {
    int type;                    // identifier of the file
    enum mb_file_status status;  // file status
    int ret_last;                // last operation's return value
    struct fs_file_t file;
};

struct file_info file_info_table[] = {
    {.type = FILE_TYPE_PROXIMITY, .status = MB_FILE_STATUS_INACTIVE, .ret_last = 0, .file = {}},
    {.type = FILE_TYPE_AUDIO, .status = MB_FILE_STATUS_INACTIVE, .ret_last = 0, .file = {}},
    {.type = FILE_TYPE_AUDIO_METADATA,
     .status = MB_FILE_STATUS_INACTIVE,
     .ret_last = 0,
     .file = {}},
    {.type = FILE_TYPE_ACCEL, .status = MB_FILE_STATUS_INACTIVE, .ret_last = 0, .file = {}},
    {.type = FILE_TYPE_GYRO, .status = MB_FILE_STATUS_INACTIVE, .ret_last = 0, .file = {}},
    {.type = FILE_TYPE_MAGNETO, .status = MB_FILE_STATUS_INACTIVE, .ret_last = 0, .file = {}},
    {.type = FILE_TYPE_ROTATION, .status = MB_FILE_STATUS_INACTIVE, .ret_last = 0, .file = {}},
};

#define FILE_COUNT (sizeof(file_info_table) / (sizeof(struct file_info)))

static bool storage_is_valid_file_type(enum mb_file_type file_type) {
    return (file_type >= FILE_TYPE_PROXIMITY) && (file_type < FILE_TYPE_MAX);
}

static void storage_update_status_locked() {
    if (!storage_status.experiment_initialized) {
        storage_status.sampling_active = false;
        led_report_active(false);
        return;
    }

    bool active = false;
    for (int i = 0; i < FILE_COUNT; i++) {
        if (file_info_table[i].status != MB_FILE_STATUS_INACTIVE) {
            active = true;
            break;
        }
    }
    storage_status.sampling_active = active;
    led_report_active(active);
}

uint8_t storage_get_status() {
    uint8_t status = 0;
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to get status");
        return 0;
    }

    status = storage_status.all_flags;
    k_mutex_unlock(&storage_mutex);
    return status;
}

static void storage_sync_work_handler(struct k_work* work) {
    (void)work;

    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex, sync work");
        return;
    }

    if (!storage_status.sampling_active) {
        k_mutex_unlock(&storage_mutex);
        return;
    }

    for (int i = 0; i < FILE_COUNT; i++) {
        if (file_info_table[i].status == MB_FILE_STATUS_ACTIVE) {
            int ret = fs_sync(&file_info_table[i].file);
            if (ret < 0) {
                file_info_table[i].status = MB_FILE_STATUS_ERR;
                file_info_table[i].ret_last = ret;
            }
        }
    }

    k_mutex_unlock(&storage_mutex);
}

static void storage_sync_timer_handler(struct k_timer* timer) {
    (void)timer;
    (void)k_work_submit(&storage_sync_work);
}

int storage_init_fs() {
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to init fs");
        return -EACCES;
    }

    int res = 0;
    bool start_sync_timer = false;
    if (!storage_status.fs_initialized) {
        res = fs_mount(&mp);
        if (res == FR_OK) {
            storage_status.fs_init_err = false;
            LOG_INF("Disk mounted");
            storage_status.fs_initialized = true;
            start_sync_timer = true;
        } else {
            storage_status.fs_init_err = true;
            LOG_ERR("Error mounting disk.");
        }
    } else {
        LOG_INF("storage already initialized");
    }

    k_mutex_unlock(&storage_mutex);

    if (start_sync_timer) {
        k_timer_start(&storage_sync_timer, K_MSEC(100), K_MSEC(100));
    }
    return res;
}

int storage_deinit_fs() {
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to deinit fs");
        return -EACCES;
    }

    int res = 0;
    if (!storage_status.fs_initialized) {
        LOG_INF("storage already deinitialized");
        k_mutex_unlock(&storage_mutex);
        return 0;
    }

    if (storage_status.sampling_active || storage_status.misc_op_active) {
        LOG_ERR("Cannot deinit fs while an operation is ongoing");
        k_mutex_unlock(&storage_mutex);
        return -EACCES;
    }

    storage_status.misc_op_active = true;
    k_mutex_unlock(&storage_mutex);

    k_timer_stop(&storage_sync_timer);
    struct k_work_sync storage_sync_work_sync;
    (void)k_work_cancel_sync(&storage_sync_work, &storage_sync_work_sync);

    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to finalize deinit fs");
        return -EACCES;
    }

    storage_status.fs_initialized = false;
    storage_status.experiment_initialized = false;
    storage_update_status_locked();

    res = fs_unmount(&mp);
    if (res < 0) {
        LOG_ERR("Error unmounting disk, err %d", res);
    } else {
        LOG_INF("Disk unmounted");
    }
    storage_status.misc_op_active = false;
    k_mutex_unlock(&storage_mutex);

    return res;
}

int storage_do_per_file_in_sd(per_file_cb_t cb, void* context) {
    int res = 0;
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to do per file op");
        return -EACCES;
    }

    if (!storage_status.fs_initialized || storage_status.sampling_active ||
        storage_status.misc_op_active) {
        LOG_ERR("cannot do per file op if fs not initialized or an fs op is ongoing");
        res = -EACCES;
    } else {
        storage_status.misc_op_active = true;
        struct fs_dir_t base_dir;
        fs_dir_t_init(&base_dir);
        res = fs_opendir(&base_dir, mp.mnt_point);
        if (res < 0) {
            LOG_ERR("could not open root dir to do per file op");
        } else {
            struct fs_dirent entry;
            int16_t file_index = 0;
            while ((res = fs_readdir(&base_dir, &entry)) == 0) {
                if (entry.name[0] == 0) {
                    break;
                }
                if (entry.type != FS_DIR_ENTRY_DIR) {
                    // skip files on root, only look into experiment folders
                    continue;
                }
                char path[MAX_PATH_LEN];
                snprintf(path, MAX_PATH_LEN, "%s/%s", mp.mnt_point, entry.name);
                struct fs_dir_t exp_dir;
                fs_dir_t_init(&exp_dir);
                res = fs_opendir(&exp_dir, path);
                if (res < 0) {
                    LOG_ERR("could not open exp dir %s to do per file op status: %d", path, res);
                    continue;
                }
                struct fs_dirent exp_entry;
                while ((res = fs_readdir(&exp_dir, &exp_entry)) == 0) {
                    if (exp_entry.name[0] == 0) {
                        break;
                    }
                    if (exp_entry.type != FS_DIR_ENTRY_FILE) {
                        // skip subdirs, only operate on files in exp folder
                        continue;
                    }
                    size_t max_copy_len = MAX_PATH_LEN + sizeof(exp_entry.name);
                    char file_path[max_copy_len];
                    snprintf(file_path, max_copy_len, "%s/%s", path, exp_entry.name);
                    res = cb(file_path, file_index, context);
                    file_index++;
                    if (res < 0) {
                        LOG_ERR("operation failed on file %s with err %d", file_path, res);
                    }
                }
                res = fs_closedir(&exp_dir);
                if (res < 0) {
                    LOG_ERR("could not close exp dir %s after doing per file op status: %d", path,
                            res);
                }
            }
            res = fs_closedir(&base_dir);
            if (res < 0) {
                LOG_ERR("could not close root dir after doing per file op status: %d", res);
            }
        }
        storage_status.misc_op_active = false;
    }

    k_mutex_unlock(&storage_mutex);
    return res;
}

int storage_init_experiment(struct cmd_setup_experiment_request* experiment_info) {
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to init experiment");
        return -EACCES;
    }
    int ret = 0;
    if (!storage_status.fs_initialized || storage_status.fs_init_err) {
        LOG_ERR(
            "Cannot init experiment folder if the file system is not initialized or init had "
            "errors");
        ret = -EACCES;
    } else if (storage_status.sampling_active || storage_status.misc_op_active) {
        LOG_ERR("Cannot init experiment folder while an operation is ongoing");
        ret = -EACCES;
    } else if (snprintf(active_experiment_dir, MAX_PATH_LEN, "/" DISK_NAME ":/%d",
                        experiment_info->experiment_id) >= MAX_PATH_LEN) {
        ret = -ENAMETOOLONG;
    } else {
        // stat to make sure the dir doesn't already exist
        struct fs_dirent dir_stat;
        ret = fs_stat(active_experiment_dir, &dir_stat);
        if (ret == 0) {
            LOG_ERR("Experiment folder for id %d already exists", experiment_info->experiment_id);
            k_mutex_unlock(&storage_mutex);
            return -EEXIST;
        } else if (ret != -ENOENT) {
            LOG_ERR("Error checking if experiment folder for id %d exists, err %d",
                    experiment_info->experiment_id, ret);
            k_mutex_unlock(&storage_mutex);
            return ret;
        }

        LOG_INF("creating experiment folder %s", active_experiment_dir);
        ret = fs_mkdir(active_experiment_dir);
        if ((ret != 0) && (ret != -EEXIST)) {
            LOG_ERR("Unknown error trying to create folder");
        }
        LOG_INF("Experiment folder created: %d", experiment_info->experiment_id);
        storage_status.experiment_initialized = true;

        // create metadata file for badge assignment info
        char meta_path[MAX_PATH_LEN * 2];
        snprintf(meta_path, sizeof(meta_path), "%s/ID.txt", active_experiment_dir);
        struct fs_file_t meta_file;
        fs_file_t_init(&meta_file);
        ret = fs_open(&meta_file, meta_path, FS_O_CREATE | FS_O_WRITE);
        if (ret < 0) {
            LOG_ERR("failed to create badge assignment metadata file, err %d", ret);
        } else {
            char badge_assignment_str[128];
            snprintf(badge_assignment_str, sizeof(badge_assignment_str),
                     "Badge Assignment: group_id - %u badge_id - %u\n",
                     experiment_info->badge_assignment.badge_id.group,
                     experiment_info->badge_assignment.badge_id.badge);
            ret = fs_write(&meta_file, badge_assignment_str, strlen(badge_assignment_str));
            if (ret < 0) {
                LOG_ERR("failed to write badge assignment info to metadata file, err %d", ret);
            }
            fs_close(&meta_file);
        }
    }
    k_mutex_unlock(&storage_mutex);
    if (ret < 0) {
        return ret;
    } else {
        return 0;
    }
}

int storage_erase(char* path) {
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to erase");
        return -EACCES;
    }

    if (storage_status.sampling_active || storage_status.misc_op_active) {
        k_mutex_unlock(&storage_mutex);
        return -EACCES;
    }

    bool erase_whole_fs = (strcmp(path, mp.mnt_point) == 0);
    if (!erase_whole_fs) {
        storage_status.misc_op_active = true;
    }
    k_mutex_unlock(&storage_mutex);

    int res;

    if (erase_whole_fs) {
        LOG_INF("erasing %s", path);
        res = storage_deinit_fs();
        if (res < 0) {
            LOG_ERR("could not unmount , errno: %d", res);
            return res;
        }

        if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
            LOG_ERR("could not acquire storage mutex to erase");
            return -EACCES;
        }

        res = fs_mkfs(FS_FATFS, (uintptr_t)DISK_NAME ":", NULL, 0);
        if (res < 0) {
            LOG_ERR("Error formating persistent storage %d", res);
        } else {
            LOG_INF("Disk formatted");
        }

        k_mutex_unlock(&storage_mutex);

        if (res == 0) {
            res = storage_init_fs();
            if (res < 0) {
                LOG_ERR("failed to init fs after formatting");
            }
        }

        if (k_mutex_lock(&storage_mutex, K_FOREVER) == 0) {
            storage_status.misc_op_active = false;
            k_mutex_unlock(&storage_mutex);
        }
    } else {
        if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
            LOG_ERR("could not acquire storage mutex to erase path");
            return -EACCES;
        }

        printf("about to delete %s\n", path);
        struct fs_dirent file_stat;
        res = fs_stat(path, &file_stat);

        // check if we are trying to delete the current experiment folder
        int cmp_res = strncmp(path, active_experiment_dir, strlen(active_experiment_dir));
        if (cmp_res == 0) {
            LOG_WRN("Trying to erase active experiment folder %s", path);
            storage_status.experiment_initialized = false;
        }

        if (res < 0) {
            LOG_ERR("could not stat file %s to erase, err %d", path, res);
        } else {
            if (file_stat.type == FS_DIR_ENTRY_DIR) {
                LOG_WRN("Trying to erase a folder %s", path);
            }
            res = fs_unlink(path);
            if (res < 0) {
                LOG_ERR("could not erase target %s err %d", path, res);
            }
        }

        storage_status.misc_op_active = false;
        storage_update_status_locked();
        k_mutex_unlock(&storage_mutex);
    }

    return res;
}

//====== Functions to be used by sampling-focused modules ====== ///

// assumes experiment was already initialized
int storage_init_sample_file(enum mb_file_type file_type, int sample_iter) {
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to init sample file");
        return -EACCES;
    }

    if (!storage_status.fs_initialized || storage_status.fs_init_err) {
        LOG_ERR("cannot init sample file! fs not initialized!");
        k_mutex_unlock(&storage_mutex);
        return -EPERM;
    } else if (storage_status.misc_op_active) {
        LOG_ERR("cannot init sample file! another misc operation is ongoing!");
        k_mutex_unlock(&storage_mutex);
        return -EACCES;
    } else if (!storage_status.experiment_initialized) {
        LOG_ERR("cannot init sample file! experiment not initialized!");
        k_mutex_unlock(&storage_mutex);
        return -EPERM;
    } else if (!storage_is_valid_file_type(file_type)) {
        LOG_ERR("invalid file type %d, cannot init sample file", file_type);
        k_mutex_unlock(&storage_mutex);
        return -EINVAL;
    } else if (file_info_table[file_type].status == MB_FILE_STATUS_ACTIVE) {
        LOG_ERR("file of type %d is already active", file_type);
        k_mutex_unlock(&storage_mutex);
        return -EINPROGRESS;
    }
    storage_status.misc_op_active = true;

    if (file_info_table[file_type].status == MB_FILE_STATUS_ERR) {
        LOG_WRN("file of type %d init after err", file_type);
    }

    int ret;
    memset(&file_info_table[file_type].file, 0, sizeof(struct fs_file_t));
    fs_file_t_init(&file_info_table[file_type].file);
    const char* fmt_prox = "%s/PROX%d";
    const char* fmt_mic = "%s/MIC%d.wav";
    const char* fmt_mic_meta = "%s/MIC%d.m";
    const char* fmt_accel = "%s/ACC%d";
    const char* fmt_gyro = "%s/GYR%d";
    const char* fmt_magneto = "%s/MAG%d";
    const char* fmt_rotation = "%s/ROT%d";

    const char* dummy = "DUMMY";
    const char* fmt = dummy;
    switch (file_type) {
        case (FILE_TYPE_PROXIMITY): {
            fmt = fmt_prox;
        } break;
        case (FILE_TYPE_AUDIO): {
            fmt = fmt_mic;
        } break;
        case (FILE_TYPE_AUDIO_METADATA): {
            fmt = fmt_mic_meta;
        } break;
        case (FILE_TYPE_ACCEL): {
            fmt = fmt_accel;
        } break;
        case (FILE_TYPE_GYRO): {
            fmt = fmt_gyro;
        } break;
        case (FILE_TYPE_MAGNETO): {
            fmt = fmt_magneto;
        } break;
        case (FILE_TYPE_ROTATION): {
            fmt = fmt_rotation;
        } break;
        default: {
            storage_status.misc_op_active = false;
            k_mutex_unlock(&storage_mutex);
            return -EINVAL;
        }
    }
    char path[MAX_PATH_LEN];
    snprintf(path, MAX_PATH_LEN, fmt, active_experiment_dir, sample_iter);

    struct fs_dirent file_stat;
    ret = fs_stat(path, &file_stat);
    if (ret == 0) {
        storage_status.misc_op_active = false;
        k_mutex_unlock(&storage_mutex);
        return -EEXIST;  // file already exists, don't overwrite
    } else if (ret != -ENOENT) {
        LOG_ERR("Error checking if sample file for type %d already exists, err %d", file_type, ret);
        storage_status.misc_op_active = false;
        k_mutex_unlock(&storage_mutex);
        return ret;
    }

    LOG_INF("Trying to open file: %s", path);
    ret = fs_open(&file_info_table[file_type].file, path, FS_O_CREATE | FS_O_WRITE);
    if (ret < 0) {
        LOG_ERR("failed to open file for type %d at path %s, err %d", file_type, path, ret);
    } else {
        LOG_INF("File opened for type %d at path %s", file_type, path);
    }
    file_info_table[file_type].status = (ret < 0) ? MB_FILE_STATUS_ERR : MB_FILE_STATUS_ACTIVE;
    file_info_table[file_type].ret_last = ret;
    storage_status.misc_op_active = false;
    storage_update_status_locked();
    k_mutex_unlock(&storage_mutex);
    return ret;
}

uint16_t storage_get_active_sensor_bitflags() {
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to get active sensor bitflags");
        return 0;
    }

    uint16_t bitflags = 0;
    for (int i = 0; i < FILE_COUNT; i++) {
        if (file_info_table[i].status == MB_FILE_STATUS_ACTIVE) {
            bitflags |= (1 << file_info_table[i].type);
        }
    }

    k_mutex_unlock(&storage_mutex);
    return bitflags;
}

int storage_write(enum mb_file_type file_type, void* data, size_t size) {
    if (!storage_is_valid_file_type(file_type)) {
        LOG_ERR("invalid file type %d for write", file_type);
        return -EINVAL;
    }

    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to write file");
        return -EACCES;
    }

    if (file_info_table[file_type].status == MB_FILE_STATUS_ERR) {
        LOG_ERR("write to file_type %d with err status %d", file_type,
                file_info_table[file_type].ret_last);
    }
    if (file_info_table[file_type].status == MB_FILE_STATUS_INACTIVE) {
        LOG_ERR("write to unopened file_type %d", file_type);
        k_mutex_unlock(&storage_mutex);
        return -EACCES;
    }

    int ret = fs_write(&file_info_table[file_type].file, data, size);
#ifdef STORAGE_DMA_NOT_ENABLED
    fs_sync(&file_info_table[file_type].file);
#endif
    file_info_table[file_type].status = (ret < 0) ? MB_FILE_STATUS_ERR : MB_FILE_STATUS_ACTIVE;
    file_info_table[file_type].ret_last = ret;
    k_mutex_unlock(&storage_mutex);
    return ret;
}

int storage_close(enum mb_file_type file_type) {
    if (!storage_is_valid_file_type(file_type)) {
        LOG_ERR("invalid file type %d for close", file_type);
        return -EINVAL;
    }

    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to close file");
        return -EACCES;
    }
    int ret = fs_close(&file_info_table[file_type].file);
    file_info_table[file_type].status = (ret < 0) ? MB_FILE_STATUS_ERR : MB_FILE_STATUS_INACTIVE;
    file_info_table[file_type].ret_last = ret;
    storage_update_status_locked();
    k_mutex_unlock(&storage_mutex);
    return ret;
}

int storage_seek_start(enum mb_file_type file_type) {
    if (!storage_is_valid_file_type(file_type)) {
        LOG_ERR("invalid file type %d for seek", file_type);
        return -EINVAL;
    }

    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to seek file");
        return -EACCES;
    }

    if (file_info_table[file_type].status != MB_FILE_STATUS_ACTIVE) {
        LOG_ERR("cannot seek in file type %d with status %d", file_type,
                file_info_table[file_type].status);
        k_mutex_unlock(&storage_mutex);
        return -EACCES;
    }

    int ret = fs_seek(&file_info_table[file_type].file, 0, FS_SEEK_SET);
    if (ret < 0) {
        LOG_ERR("failed to seek to start of file type %d, err %d", file_type, ret);
    }

    k_mutex_unlock(&storage_mutex);
    return ret;
}

int storage_write_timesync(struct timesync_entry* entry) {
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to write timesync");
        return -EACCES;
    }

    if (!storage_status.experiment_initialized) {
        LOG_ERR("Experiment storage not initialized");
        k_mutex_unlock(&storage_mutex);
        return -EPERM;
    }

    char path[MAX_PATH_LEN];
    int written = snprintf(path, MAX_PATH_LEN, "%s/SYNC", active_experiment_dir);
    if (written < 0 || written >= MAX_PATH_LEN) {
        LOG_ERR("failed to create timesync file path, err %d", written);
        k_mutex_unlock(&storage_mutex);
        return -ENAMETOOLONG;
    }
    struct fs_file_t timesync_file;
    fs_file_t_init(&timesync_file);
    int ret = fs_open(&timesync_file, path, FS_O_CREATE | FS_O_APPEND | FS_O_WRITE);
    if (ret < 0) {
        LOG_ERR("failed to open timesync file to write timesync event %s, err %d", path, ret);
        k_mutex_unlock(&storage_mutex);
        return ret;
    }
    // uint8_t buff[64];
    //  snprintf((char*)buff, sizeof(buff), "ref: %" PRIu64 ", interp: %" PRIu64 "\n",
    //  reference,
    //           interpolated);
    k_yield();
    // ret = fs_write(&timesync_file, buff, strlen((char*)buff));
    ret = fs_write(&timesync_file, entry, sizeof(struct timesync_entry));
    if (ret < 0) {
        LOG_ERR("failed to write timesync event to file, err %d", ret);
    }
    k_yield();
    int ret_close = fs_close(&timesync_file);
    if (ret_close < 0) {
        ret = ret_close;
        LOG_ERR("failed to close timesync file after writing event, err %d", ret);
    }

    k_mutex_unlock(&storage_mutex);
    return ret;
}

// ======= Cmd Processor-facing API ====== //

int cmd_erase_sd(uint8_t* data) {
    // struct cmd_erase_sd_request* req_data = (struct cmd_erase_sd_request*)data;
    struct cmd_erase_sd_response* resp_data = (struct cmd_erase_sd_response*)data;
    int ret = storage_erase(DISK_MOUNT_POINT);
    resp_data->status_code = ret;
    return ret;
}

int cmd_erase_file(uint8_t* data) {
    struct cmd_erase_file_request* req_data = (struct cmd_erase_file_request*)data;
    struct cmd_erase_file_response* resp_data = (struct cmd_erase_file_response*)data;
    req_data->path[INTERFACE_MAX_FILE_NAME - 1] = '\0';  // ensure null termination
    int ret = storage_erase((char*)req_data->path);
    resp_data->status_code = ret;
    return ret;
}

int cmd_get_free_sd_space(uint8_t* data) {
    // struct cmd_get_free_sd_space_request* req_data = (struct
    // cmd_get_free_sd_space_request*)data;
    struct cmd_get_free_sd_space_response* resp_data = (struct cmd_get_free_sd_space_response*)data;
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to get free sd space");
        resp_data->free_bytes = 0;
        return -EACCES;
    }

    struct fs_statvfs stat;
    int res = fs_statvfs(mp.mnt_point, &stat);
    if (res < 0) {
        LOG_ERR("could not get free space, err %d", res);
        resp_data->free_bytes = 0;
        k_mutex_unlock(&storage_mutex);
        return res;
    }
    resp_data->free_bytes = stat.f_bfree * stat.f_frsize;

    k_mutex_unlock(&storage_mutex);
    return 0;
}

struct GetFileNameFromIndexContext {
    struct cmd_get_file_index_info_response* resp_data;
    bool found;
};

static int get_file_name_from_index(char* path, int16_t index, void* context) {
    struct GetFileNameFromIndexContext* ctx = (struct GetFileNameFromIndexContext*)context;
    struct cmd_get_file_index_info_response* resp_data = ctx->resp_data;

    if (index == resp_data->index) {
        ctx->found = true;
        // populate response with file info
        struct fs_dirent file_stat;
        int res = fs_stat(path, &file_stat);
        if (res < 0) {
            LOG_ERR("could not stat file %s to get file name from index, err %d", path, res);
            return res;
        } else if (file_stat.type != FS_DIR_ENTRY_FILE) {
            LOG_ERR("can only get info for files, not dirs, stat type %d", file_stat.type);
            return -EACCES;
        } else {
            resp_data->size_bytes = file_stat.size;
            strncpy((char*)resp_data->path, path, INTERFACE_MAX_FILE_NAME);
            return 0;
        }
    }
    return 0;  // keep looking for the file with the right index
}

int cmd_get_file_index_info(uint8_t* data) {
    struct cmd_get_file_index_info_request* req_data =
        (struct cmd_get_file_index_info_request*)data;
    struct cmd_get_file_index_info_response* resp_data =
        (struct cmd_get_file_index_info_response*)data;
    int16_t index = req_data->index;
    memset(resp_data, 0, sizeof(struct cmd_get_file_index_info_response));
    resp_data->index = index;
    struct GetFileNameFromIndexContext context = {.resp_data = resp_data, .found = false};

    int res = storage_do_per_file_in_sd(get_file_name_from_index, &context);
    if (res < 0) {
        LOG_ERR("error occurred while looking for file with index %d, err %d", index, res);
    } else if (context.found == false) {
        LOG_INF("no file found for index %d, err %d", index, res);
        res = -ENOENT;
    }

    resp_data->index = (context.found)
                           ? resp_data->index
                           : res;  // set to -1 to indicate error, valid index is non-negative
    return resp_data->index;
}

int cmd_get_file_crc32(uint8_t* data) {
    struct cmd_get_file_crc32_request* req_data = (struct cmd_get_file_crc32_request*)data;
    struct cmd_get_file_crc32_response* resp_data = (struct cmd_get_file_crc32_response*)data;
    req_data->path[INTERFACE_MAX_FILE_NAME - 1] = '\0';  // ensure null termination
    struct fs_file_t file;
    fs_file_t_init(&file);
    uint8_t buffer[512] __aligned(32);
    uint32_t checksum = 0;

    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to compute crc32");
        resp_data->status_code = -EACCES;
        return -EACCES;
    }

    int res = fs_open(&file, (char*)req_data->path, FS_O_READ);
    if (res < 0) {
        LOG_ERR("could not open file %s to get crc32, err %d", req_data->path, res);
        resp_data->status_code = res;
        k_mutex_unlock(&storage_mutex);
        return res;
    }

    do {
        res = fs_read(&file, buffer, sizeof(buffer));
        if (res < 0) {
            LOG_ERR("error reading file %s to get crc32, err %d", req_data->path, res);
            resp_data->status_code = res;
            int off = fs_tell(&file);
            LOG_ERR("error occurred at offset %d in file %s", off, req_data->path);
            break;
        }
        checksum = crc32_ieee_update(checksum, buffer, res);

    } while (res > 0);
    if (fs_close(&file) < 0) {
        LOG_ERR("error closing file %s after getting crc32, err %d", req_data->path, res);
    }
    resp_data->crc32 = checksum;
    resp_data->status_code = (res < 0) ? res : 0;
    res = resp_data->status_code;
    k_mutex_unlock(&storage_mutex);
    return res;
}

// add guard
static struct fs_file_t file_for_chunk_download;
int cmd_download_file_chunk(uint8_t* data) {
    struct cmd_download_file_chunk_request* req_data =
        (struct cmd_download_file_chunk_request*)data;
    struct cmd_download_file_chunk_response* resp_data =
        (struct cmd_download_file_chunk_response*)data;
    req_data->path[INTERFACE_MAX_FILE_NAME - 1] = '\0';  // ensure null termination
    static bool opened_file_for_download = false;

    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to download chunk");
        resp_data->bytes = -EACCES;
        return -EACCES;
    }

    int res = 0;
    if (req_data->offset == 0) {
        // close prev file if opened for some reason
        if (opened_file_for_download) {
            fs_close(&file_for_chunk_download);
        }
        // new download, open file
        fs_file_t_init(&file_for_chunk_download);
        res = fs_open(&file_for_chunk_download, (char*)req_data->path, FS_O_READ);
        if (res < 0) {
            LOG_ERR("could not open file %s to download chunk, err %d", req_data->path, res);
            resp_data->bytes = res;  // set to error code
            k_mutex_unlock(&storage_mutex);
            return res;
        }
        opened_file_for_download = true;
    }

    if (!opened_file_for_download) {
        LOG_ERR("file not opened for download but got offset %d, path %s", req_data->offset,
                req_data->path);
        resp_data->bytes = -EPERM;  // set to error code
        k_mutex_unlock(&storage_mutex);
        return -EPERM;
    }

    off_t offset = req_data->offset;
    LOG_DBG("opened file %s to download chunk at offset %ld", req_data->path, offset);

    // no need to preserve the path
    // memset(resp_data->data, 0, sizeof(resp_data->data));
    do {
        // seek to offset
        res = fs_seek(&file_for_chunk_download, offset, FS_SEEK_SET);
        if (res < 0) {
            LOG_ERR("could not seek in file %s to download chunk, err %d", req_data->path, res);
            break;
        }
        // read chunk
        res = fs_read(&file_for_chunk_download, resp_data->data, sizeof(resp_data->data));
        if (res < 0) {
            LOG_ERR("error reading file %s to download chunk, err %d", req_data->path, res);
        }
        LOG_DBG("read chunk from file %s at offset %ld, bytes read %d", req_data->path, offset,
                res);
    } while (0);

    if (res <= 0) {
        if (fs_close(&file_for_chunk_download) < 0) {
            LOG_ERR("error closing file %s, err %d", req_data->path, res);
        }
        opened_file_for_download = false;
    }

    resp_data->bytes = res;  // set to 0 to indicate end of file
    k_mutex_unlock(&storage_mutex);
    return res;
}
