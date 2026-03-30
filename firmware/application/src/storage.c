#include "storage.h"

#include <errno.h>
#include <ff.h>
#include <stdio.h>
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
    {.type = FILE_TYPE_TIMESYNC, .status = MB_FILE_STATUS_INACTIVE, .ret_last = 0, .file = {}},
    {.type = FILE_TYPE_ACCEL, .status = MB_FILE_STATUS_INACTIVE, .ret_last = 0, .file = {}},
    {.type = FILE_TYPE_GYRO, .status = MB_FILE_STATUS_INACTIVE, .ret_last = 0, .file = {}},
    {.type = FILE_TYPE_MAGNETO, .status = MB_FILE_STATUS_INACTIVE, .ret_last = 0, .file = {}},
    {.type = FILE_TYPE_ROTATION, .status = MB_FILE_STATUS_INACTIVE, .ret_last = 0, .file = {}},
};

#define FILE_COUNT (sizeof(file_info_table) / (sizeof(struct file_info)))
K_MUTEX_DEFINE(storage_mutex);

enum mb_storage_status storage_status = MB_STORAGE_STATUS_UNINIT;

/**
 * @brief Updates the general storage status based on the individual sample file
 * statuses. If there is at least one ongoing sampling task (i.e. a file is
 * active) the led shall remain ON.
 *
 */
static void storage_update_status() {
    if (k_mutex_lock(&storage_mutex, K_FOREVER) == 0) {
        switch (storage_status) {
            case MB_STORAGE_STATUS_INIT_OK_ACTIVE:
            case MB_STORAGE_STATUS_INIT_OK_INACTIVE: {
                bool active = false;
                for (int i = 0; i < FILE_COUNT; i++) {
                    if (file_info_table[i].status != MB_FILE_STATUS_INACTIVE) {
                        active = true;
                        break;
                    }
                }
                storage_status =
                    active ? MB_STORAGE_STATUS_INIT_OK_ACTIVE : MB_STORAGE_STATUS_INIT_OK_INACTIVE;
                led_report_active(active);
            } break;
            default: {
            }
        }
        k_mutex_unlock(&storage_mutex);
    }
}

static void storage_sync_work_handler(struct k_work* work);
static void storage_sync_timer_handler(struct k_timer* timer);

K_WORK_DEFINE(storage_sync_work, storage_sync_work_handler);
K_TIMER_DEFINE(storage_sync_timer, storage_sync_timer_handler, NULL);

static void storage_sync_work_handler(struct k_work* work) {
    (void)work;

    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex, sync work");
        return;
    }

    if ((storage_status != MB_STORAGE_STATUS_INIT_OK_ACTIVE) &&
        (storage_status != MB_STORAGE_STATUS_INIT_OK_INACTIVE)) {
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
    if ((storage_status != MB_STORAGE_STATUS_INIT_OK_ACTIVE) &&
        (storage_status != MB_STORAGE_STATUS_INIT_OK_INACTIVE)) {
        res = fs_mount(&mp);
        if (res == FR_OK) {
            storage_status = MB_STORAGE_STATUS_INIT_OK_INACTIVE;
            LOG_INF("Disk mounted");
        } else {
            storage_status = MB_STORAGE_STATUS_INIT_ERR;
            LOG_ERR("Error mounting disk.");
        }
    } else {
        LOG_INF("storage already initialized");
    }
    k_mutex_unlock(&storage_mutex);

    if ((storage_status == MB_STORAGE_STATUS_INIT_OK_ACTIVE) ||
        (storage_status == MB_STORAGE_STATUS_INIT_OK_INACTIVE)) {
        k_timer_start(&storage_sync_timer, K_MSEC(100), K_MSEC(100));
    }

    if (res == FR_OK) {
        storage_init_experiment(0);
    }
    return res;
}

int storage_deinit_fs() {
    int res = 0;

    if (storage_status == MB_STORAGE_STATUS_INIT_OK_ACTIVE) {
        LOG_ERR("Cannot deinit fs while sampling is ongoing");
        res = -EACCES;
    } else {
        k_timer_stop(&storage_sync_timer);
        struct k_work_sync storage_sync_work_sync;
        (void)k_work_cancel_sync(&storage_sync_work, &storage_sync_work_sync);

        storage_status = MB_STORAGE_STATUS_UNINIT;
        if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
            LOG_ERR("could not acquire storage mutex to deinit fs");
            return -EACCES;
        }

        res = fs_unmount(&mp);
        LOG_INF("Disk unmounted");
        k_mutex_unlock(&storage_mutex);
    }
    return res;
}

int storage_do_per_file_in_sd(per_file_cb_t cb, void* context) {
    int res = 0;
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to do per file op");
        return -EACCES;
    }

    if ((storage_status != MB_STORAGE_STATUS_INIT_OK_INACTIVE)) {
        LOG_ERR("cannot do per file op if fs not initialized and sampling not ongoing");
        res = -EACCES;
    } else {
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
        }
        res = fs_closedir(&base_dir);
        if (res < 0) {
            LOG_ERR("could not close root dir after doing per file op status: %d", res);
        }
    }

    k_mutex_unlock(&storage_mutex);
    return res;
}

uint8_t storage_get_status() { return (uint8_t)storage_status; }

static char active_experiment_dir[MAX_PATH_LEN];
int storage_init_experiment(int id) {
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to init experiment");
        return -EACCES;
    }
    int ret = 0;
    if (storage_status != MB_STORAGE_STATUS_INIT_OK_INACTIVE) {
        LOG_ERR("Cannot init experiment folder while sampling is ongoing");
        ret = -EACCES;
    } else if (snprintf(active_experiment_dir, MAX_PATH_LEN, "/" DISK_NAME ":/%d", id) >=
               MAX_PATH_LEN) {
        ret = -ENAMETOOLONG;
    } else {
        LOG_INF("creating experiment folder %s", active_experiment_dir);
        ret = fs_mkdir(active_experiment_dir);
        if ((ret != 0) && (ret != -EEXIST)) {
            LOG_ERR("Unknown error trying to create folder");
        }
        LOG_INF("Experiment folder created: %d", id);
    }
    k_mutex_unlock(&storage_mutex);
    return ret;
}

int storage_erase(char* path) {
    int res;
    if (storage_status != MB_STORAGE_STATUS_INIT_OK_INACTIVE) {
        return -EACCES;
    }
    if (strcmp(path, mp.mnt_point) == 0) {
        LOG_INF("erasing %s", path);
        res = storage_deinit_fs();
        if (res < 0) {
            LOG_ERR("could not unmount , errno: %d", res);
            return res;
        }
        // check files are closed
        if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
            LOG_ERR("could not acquire storage mutex to erase");
            return -EACCES;
        }

        if (storage_status != MB_STORAGE_STATUS_UNINIT) {
            res = -EACCES;
        } else {
            res = fs_mkfs(FS_FATFS, (uintptr_t)DISK_NAME ":", NULL, 0);
            if (res < 0) {
                LOG_ERR("Error formating persistent storage %d", res);
            } else {
                LOG_INF("Disk formatted");
            }
        }
        k_mutex_unlock(&storage_mutex);

        if (res == 0) {
            res = storage_init_fs();
            if (res < 0) {
                LOG_ERR("failed to init fs after formatting");
            }
        }
    } else {
        // other path provided
        printf("about to delete %s\n", path);
        struct fs_dirent file_stat;
        res = fs_stat(path, &file_stat);
        if (res < 0) {
            LOG_ERR("could not stat file %s to erase, err %d", path, res);
        } else {
            if (file_stat.type == FS_DIR_ENTRY_DIR) {
                LOG_ERR("Trying to erase a folder %s", path);
            }
            res = fs_unlink(path);
            if (res < 0) {
                LOG_ERR("could not erase target %s err %d", path, res);
            }
        }
    }

    return res;
}

//====== Functions to be used by sampling-focused modules ====== ///

// assumes experiment was already initialized
int storage_init_sample_file(enum mb_file_type file_type, int sample_iter) {
    if ((storage_status == MB_STORAGE_STATUS_UNINIT) ||
        (storage_status == MB_STORAGE_STATUS_INIT_ERR)) {
        LOG_ERR("cannot init sample file! fs not initialized!");
        return -EPERM;
    }
    if (file_type > FILE_TYPE_MAX) {
        return -EINVAL;
    }

    if (file_info_table[file_type].status == MB_FILE_STATUS_ACTIVE) {
        return -EINPROGRESS;
    }

    if (file_info_table[file_type].status == MB_FILE_STATUS_ERR) {
        LOG_ERR("file of type %d init after err", file_type);
    }

    int ret;
    memset(&file_info_table[file_type].file, 0, sizeof(struct fs_file_t));
    fs_file_t_init(&file_info_table[file_type].file);
    const char* fmt_prox = "%s/PROX%d";
    const char* fmt_sync = "%s/SYNC%d";
    const char* fmt_mic = "%s/MIC%d";
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
        case (FILE_TYPE_TIMESYNC): {
            fmt = fmt_sync;
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
            return -EINVAL;
        }
    }
    char path[MAX_PATH_LEN];
    snprintf(path, MAX_PATH_LEN, fmt, active_experiment_dir, sample_iter);
    LOG_INF("Trying to open file: %s", path);
    ret = fs_open(&file_info_table[file_type].file, path, FS_O_CREATE | FS_O_WRITE);
    file_info_table[file_type].status = (ret < 0) ? MB_FILE_STATUS_ERR : MB_FILE_STATUS_ACTIVE;
    file_info_table[file_type].ret_last = ret;
    storage_update_status();
    return ret;
}

uint16_t storage_get_active_sensor_bitflags() {
    uint16_t bitflags = 0;
    for (int i = 0; i < FILE_COUNT; i++) {
        if (file_info_table[i].status == MB_FILE_STATUS_ACTIVE) {
            bitflags |= (1 << file_info_table[i].type);
        }
    }
    return bitflags;
}

int storage_write(enum mb_file_type file_type, void* data, size_t size) {
    if (file_info_table[file_type].status == MB_FILE_STATUS_ERR) {
        LOG_ERR("write to file_type %d with err status %d", file_type,
                file_info_table[file_type].ret_last);
    }
    if (file_info_table[file_type].status == MB_FILE_STATUS_INACTIVE) {
        LOG_ERR("write to unopened file_type %d", file_type);
    }
    int ret = fs_write(&file_info_table[file_type].file, data, size);
#ifdef STORAGE_DMA_NOT_ENABLED
    fs_sync(&file_info_table[file_type].file);
#endif
    file_info_table[file_type].status = (ret < 0) ? MB_FILE_STATUS_ERR : MB_FILE_STATUS_ACTIVE;
    file_info_table[file_type].ret_last = ret;
    return ret;
}

int storage_close(enum mb_file_type file_type) {
    if (k_mutex_lock(&storage_mutex, K_FOREVER) != 0) {
        LOG_ERR("could not acquire storage mutex to close file");
        return -EACCES;
    }
    int ret = fs_close(&file_info_table[file_type].file);
    file_info_table[file_type].status = (ret < 0) ? MB_FILE_STATUS_ERR : MB_FILE_STATUS_INACTIVE;
    file_info_table[file_type].ret_last = ret;
    k_mutex_unlock(&storage_mutex);
    storage_update_status();
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

int cmd_get_free_sd_space(uint8_t* data) {
    // struct cmd_get_free_sd_space_request* req_data = (struct cmd_get_free_sd_space_request*)data;
    struct cmd_get_free_sd_space_response* resp_data = (struct cmd_get_free_sd_space_response*)data;
    struct fs_statvfs stat;
    int res = fs_statvfs(mp.mnt_point, &stat);
    if (res < 0) {
        LOG_ERR("could not get free space, err %d", res);
        resp_data->free_bytes = 0;
        return res;
    }
    resp_data->free_bytes = stat.f_bfree * stat.f_frsize;
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
    if (context.found == false) {
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

    int res = fs_open(&file, (char*)req_data->path, FS_O_READ);
    if (res < 0) {
        LOG_ERR("could not open file %s to get crc32, err %d", req_data->path, res);
        resp_data->status_code = res;
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
            return res;
        }
        opened_file_for_download = true;
    }

    if (!opened_file_for_download) {
        LOG_ERR("file not opened for download but got offset %d, path %s", req_data->offset,
                req_data->path);
        resp_data->bytes = -EPERM;  // set to error code
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
    return res;
}
