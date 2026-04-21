#include "cmd_processor.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "audio.h"
#include "battery_charge.h"
#include "imu.h"
#include "midge_protocol.h"
#include "proximity.h"
#include "storage.h"
#include "time_control.h"

LOG_MODULE_REGISTER(cmd_processor);

struct custom_advertisement_data advertised_data = {
    .battery_mv = 0, .active_sensor_bitflags = 0, .badge_assignment = {.u16_all = 0xFFFF}};

// ======== Aggregate functionality command implementations ======== //

int cmd_setup_experiment(uint8_t* data) {
    struct cmd_setup_experiment_request* req_data = (struct cmd_setup_experiment_request*)data;
    struct cmd_setup_experiment_response* resp_data = (struct cmd_setup_experiment_response*)data;
    advertised_data.badge_assignment = req_data->badge_assignment;
    int ret = storage_init_experiment(req_data);
    if (ret < 0) {
        LOG_ERR("Failed to initialize experiment");
    } else {
        time_control_reset();
    }
    resp_data->status_code = ret;
    return ret;
}

int cmd_status(uint8_t* data) {
    LOG_INF("received status message");
    struct cmd_status_request* req_data = (struct cmd_status_request*)data;
    int64_t error = 0;
    int ret = time_control_sync(req_data->millis_since_epoch, &error);
    if (ret < 0) {
        LOG_ERR("time sync failed with error %d", ret);
    }
    int16_t mv = 0;
    ret = battery_charge_get_mv(&mv);
    if (ret < 0) {
        LOG_ERR("Failed to read battery voltage");
    }
    struct cmd_status_response* resp_data = (struct cmd_status_response*)data;
    memset(resp_data, 0, sizeof(struct cmd_status_response));
    resp_data->badge_assignment = advertised_data.badge_assignment;
    resp_data->sync_status = time_control_get_status();
    resp_data->sync_error_ms = error;
    resp_data->audio_init_status = audio_sensor_get_status();
    LOG_INF("audio sensor get status");
    resp_data->battery_millivolts = mv;
    resp_data->proximity_init_status = proximity_sensor_get_status();
    LOG_INF("proximity sensor get status");
    resp_data->storage_init_status = storage_get_status();
    LOG_INF("returning status");
    return ret;
}

int cmd_get_fw_version(uint8_t* data) {
    // struct cmd_get_fw_version_request* req_data = (struct cmd_get_fw_version_request*)data;
    struct cmd_get_fw_version_response* resp_data = (struct cmd_get_fw_version_response*)data;
    LOG_INF("received get fw version message");
    memset(resp_data, 0, sizeof(struct cmd_get_fw_version_response));
    // null char at the end is enforced with the memset call and the size limit -1
    strncpy((char*)resp_data->version_str, FW_VERSION, sizeof(resp_data->version_str) - 1);
    LOG_INF("returning fw version: %s", resp_data->version_str);
    return 0;
}

enum cmd_state { READ_SOT, READ_CMD, READ_DATA, READ_EOT };

struct cmd_processor_lut_entry {
    uint8_t cmd_id;
    int req_data_size;
    int resp_data_size;
    int (*execute_cmd)(uint8_t* data);
};

struct cmd_processor_lut_entry commands[] = {
    {CMD_ID_SETUP_EXPERIMENT, sizeof(struct cmd_setup_experiment_request),
     sizeof(struct cmd_setup_experiment_response), cmd_setup_experiment},
    {CMD_ID_STATUS, sizeof(struct cmd_status_request), sizeof(struct cmd_status_response),
     cmd_status},
    {CMD_ID_GET_FW_VERSION, sizeof(struct cmd_get_fw_version_request),
     sizeof(struct cmd_get_fw_version_response), cmd_get_fw_version},
    {CMD_ID_START_MIC, sizeof(struct cmd_start_mic_request), sizeof(struct cmd_start_mic_response),
     cmd_mic_start},
    {CMD_ID_STOP_MIC, sizeof(struct cmd_stop_mic_request), sizeof(struct cmd_stop_mic_response),
     cmd_mic_stop},
    {CMD_ID_START_SCAN, sizeof(struct cmd_start_scan_request),
     sizeof(struct cmd_start_scan_response), cmd_scan_start},
    {CMD_ID_STOP_SCAN, sizeof(struct cmd_stop_scan_request), sizeof(struct cmd_stop_scan_response),
     cmd_scan_stop},
    {CMD_ID_START_IMU, sizeof(struct cmd_start_imu_request), sizeof(struct cmd_start_imu_response),
     cmd_start_imu},
    {CMD_ID_STOP_IMU, sizeof(struct cmd_stop_imu_request), sizeof(struct cmd_stop_imu_response),
     cmd_stop_imu},
    {CMD_ID_ERASE_SD, sizeof(struct cmd_erase_sd_request), sizeof(struct cmd_erase_sd_response),
     cmd_erase_sd},
    {CMD_ID_ERASE_FILE, sizeof(struct cmd_erase_file_request),
     sizeof(struct cmd_erase_file_response), cmd_erase_file},
    {CMD_ID_GET_FREE_SD_SPACE, sizeof(struct cmd_get_free_sd_space_request),
     sizeof(struct cmd_get_free_sd_space_response), cmd_get_free_sd_space},
    {CMD_ID_GET_FILE_INDEX_INFO, sizeof(struct cmd_get_file_index_info_request),
     sizeof(struct cmd_get_file_index_info_response), cmd_get_file_index_info},
    {CMD_ID_GET_FILE_CRC32, sizeof(struct cmd_get_file_crc32_request),
     sizeof(struct cmd_get_file_crc32_response), cmd_get_file_crc32},
    {CMD_ID_DOWNLOAD_FILE_CHUNK, sizeof(struct cmd_download_file_chunk_request),
     sizeof(struct cmd_download_file_chunk_response), cmd_download_file_chunk}};

#define COMMAND_COUNT ARRAY_SIZE(commands)

int invalid_cmd(uint8_t* data) {
    printf("ERROR IN CMD PROCESSOR CONTROL FLOW");
    return -EINVAL;
}

static void received(struct bt_conn* conn, const void* data, uint16_t len, void* ctx) {
    static enum cmd_state rx_cmd_state = READ_SOT;
    // SOT + CMD_ID + DATA + EOT
    static uint8_t cmd_data[INTERFACE_CMD_SZ];
    static int cmd_data_idx = 0;                             // currently read data
    static int cmd_req_data_size = 0;                        // how much data to read
    static int cmd_resp_data_size = 0;                       // how much data to send
    static int (*execute_cmd)(uint8_t* data) = invalid_cmd;  // ptr to command function

    // Get the negotiated MTU for the connection to determine how much data we
    // can send in one chunk
    int mtu = bt_gatt_get_mtu(conn) - 3;  // Subtract ATT header size
    ARG_UNUSED(ctx);
    LOG_DBG("%s() - Len: %d, Message: %.*s\n", __func__, len, len, (char*)data);
    const uint8_t* rx_data = (const uint8_t*)data;
    // State Machine
    for (int i = 0; i < len; i++) {
        uint8_t c = rx_data[i];

        switch (rx_cmd_state) {
            case READ_SOT:
                if (c == CMD_PROCESSOR_SOT) {
                    rx_cmd_state = READ_CMD;
                    cmd_data[0] = c;
                }
                break;
            case READ_CMD:
                bool cmd_is_valid = false;
                execute_cmd = invalid_cmd;  // default to invalid command
                for (int j = 0; j < COMMAND_COUNT; j++) {
                    if (c == commands[j].cmd_id) {
                        execute_cmd = commands[j].execute_cmd;
                        cmd_req_data_size = commands[j].req_data_size + 2;
                        cmd_resp_data_size = commands[j].resp_data_size + 2;
                        cmd_data[1] = c;
                        cmd_data_idx = 2;
                        rx_cmd_state = READ_DATA;
                        cmd_is_valid = true;
                        break;
                    }
                }
                if (!cmd_is_valid) {
                    // invalid command, go back to read SOT
                    rx_cmd_state = READ_SOT;
                    LOG_ERR("Received invalid command ID: %c", c);
                } else {
                    LOG_DBG("Received valid command ID: %c, expecting %d bytes of data", c,
                            cmd_req_data_size - 2);
                }

                break;
            case READ_DATA:
                cmd_data[cmd_data_idx] = c;
                cmd_data_idx++;
                if (cmd_data_idx < cmd_req_data_size) {
                    // keep reading

                } else {
                    // already read all data
                    rx_cmd_state = READ_EOT;
                }
                break;
            case READ_EOT:
                if (c == CMD_PROCESSOR_EOT) {
                    // valid termination
                    execute_cmd(&cmd_data[2]);
                    cmd_data[cmd_resp_data_size] = CMD_PROCESSOR_EOT;
                    // total message size: resp data (includes EOT and cmd_id) + SOT
                    // Send data in chunks of 20 bytes
                    int total_bytes = cmd_resp_data_size + 1;
                    int bytes_sent = 0;
                    while (bytes_sent < total_bytes) {
                        int chunk_size =
                            (total_bytes - bytes_sent) > mtu ? mtu : (total_bytes - bytes_sent);
                        int chunk_stat =
                            bt_nus_send(conn, (void*)&cmd_data[bytes_sent], chunk_size);
                        if (chunk_stat == -ENOMEM || chunk_stat == -EAGAIN) {
                            k_msleep(1);
                            continue;
                        } else if (chunk_stat < 0) {
                            LOG_ERR("Failed to send data over BLE: %d", chunk_stat);
                            break;
                        }
                        bytes_sent += chunk_size;
                    }
                } else {
                    // invalid termination
                }
                rx_cmd_state = READ_SOT;
                break;
            default:
                rx_cmd_state = READ_SOT;
        }
    }
}

/// =========================================
/// BLE
/// =========================================

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, (const uint8_t*)&advertised_data, sizeof(advertised_data)),
};

static struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

#define ADVERTISED_DATA_THREAD_STACK_SIZE 512
#define ADVERTISED_DATA_THREAD_PRIORITY K_LOWEST_APPLICATION_THREAD_PRIO

K_THREAD_STACK_DEFINE(advertised_data_thread_stack, ADVERTISED_DATA_THREAD_STACK_SIZE);
static struct k_thread advertised_data_thread_data;

static void adv_data_update(void) {
    int16_t mv = 0;
    if (battery_charge_get_mv(&mv) == 0) {
        advertised_data.battery_mv = (uint16_t)mv;
    }
    advertised_data.active_sensor_bitflags = storage_get_active_sensor_bitflags();
    bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
}

static void advertised_data_thread(void* arg1, void* arg2, void* arg3) {
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (true) {
        adv_data_update();
        k_sleep(K_SECONDS(1));
    }
}

static void notif_enabled(bool enabled, void* ctx) {
    ARG_UNUSED(ctx);
    printk("%s() - %s\n", __func__, (enabled ? "Enabled" : "Disabled"));
}

struct bt_nus_cb nus_listener = {
    .notif_enabled = notif_enabled,
    .received = received,
};

static void ble_conn_recycled_cb() {
    // move to work queue
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Failed to re-start advertising: %d", err);
    }
}

static void ble_conn_disconnected_cb(struct bt_conn* conn, uint8_t err) {
    (void)err;
    (void)conn;
    err = bt_le_adv_stop();  // stop non connectable advertising
    if (err) {
        LOG_ERR("Connection failure %d", err);
    }
}

static void ble_conn_connected_cb(struct bt_conn* conn, uint8_t err) {
    (void)err;
    (void)conn;
    // start non connectable advertising to allow scanning
    err = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Connection failure %d", err);
    }
}

struct bt_conn_cb conn_cb = {.recycled = ble_conn_recycled_cb,
                             .disconnected = ble_conn_disconnected_cb,
                             .connected = ble_conn_connected_cb};

int cmd_processor_init(void) {
    int err;
    err = bt_nus_cb_register(&nus_listener, NULL);
    if (err) {
        LOG_ERR("Failed to register NUS callback: %d", err);
        return err;
    }
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Failed to enable bluetooth: %d", err);
        return err;
    }
    bt_conn_cb_register(&conn_cb);

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Failed to start advertising: %d", err);
        return err;
    }

    k_thread_create(&advertised_data_thread_data, advertised_data_thread_stack,
                    K_THREAD_STACK_SIZEOF(advertised_data_thread_stack), advertised_data_thread,
                    NULL, NULL, NULL, ADVERTISED_DATA_THREAD_PRIORITY, 0, K_SECONDS(2));
    LOG_INF("Initialization complete");
    return 0;
}
