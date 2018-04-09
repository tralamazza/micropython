/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Ayke van Laethem
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include <stdint.h>
#include <stddef.h>
#include "ble.h"
#include "nrf_sdm.h"
#include "nrf_mbr.h"
#include "dfu/dfu.h"
#include "dfu/dfu_ble.h"
#include "dfu/dfu_uart.h"

#define MSEC_TO_UNITS(TIME, RESOLUTION) (((TIME) * 1000) / (RESOLUTION))
#define UNIT_0_625_MS (625)
#define UNIT_10_MS    (10000)

#define DEVICE_NAME {'D', 'F', 'U'}

// Use the highest speed possible (lowest connection interval allowed,
// 7.5ms), while trying to keep the connection alive by setting the
// connection timeout to the largest allowed (4 seconds).
#define BLE_MIN_CONN_INTERVAL        BLE_GAP_CP_MIN_CONN_INTVL_MIN
#define BLE_MAX_CONN_INTERVAL        BLE_GAP_CP_MAX_CONN_INTVL_MIN
#define BLE_SLAVE_LATENCY            0
#define BLE_CONN_SUP_TIMEOUT         MSEC_TO_UNITS(4000, UNIT_10_MS)

// Randomly generated UUID. This UUID is the base UUID, but also the
// service UUID.
#define UUID_BASE {0xf4, 0x22, 0xb8, 0xef, 0x72, 0xba, 0x4b, 0xf8, 0x8c, 0xf5, 0xae, 0x83, 0x01, 0x00, 0xfc, 0x67}
#define UUID_DFU_SERVICE      0x0001
#define UUID_DFU_CHAR_INFO    0x0002
#define UUID_DFU_CHAR_COMMAND 0x0003
#define UUID_DFU_CHAR_BUFFER  0x0004

static MBRCONST ble_uuid128_t uuid_base = {
    UUID_BASE,
};

static MBRCONST uint8_t device_name[] = DEVICE_NAME;
static MBRCONST struct {
    uint8_t flags_len;
    uint8_t flags_type;
    uint8_t flags_value;
    uint8_t name_len;
    uint8_t name_type;
    uint8_t name_value[sizeof(device_name)];
    uint8_t uuid_len;
    uint8_t uuid_type;
    uint8_t uuid_value[16];
} adv_data = {
    2,
    BLE_GAP_AD_TYPE_FLAGS,
    BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE,
    sizeof(device_name) + 1, // type + name
    BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
    DEVICE_NAME,
    16 + 1, // uuid-128 is 16 bytes, plus a type
    BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE,
    UUID_BASE,
};

static ble_enable_params_t ble_enable_params = {
    #if NRF52
    .common_enable_params.p_conn_bw_counts = NULL,
    .common_enable_params.vs_uuid_count = 1,
    .gap_enable_params.periph_conn_count = 1,
    .gap_enable_params.central_conn_count = 0,
    .gap_enable_params.central_sec_count = 0,
    .gap_enable_params.p_device_name = NULL,
    .gatt_enable_params.att_mtu = 0, // default
    #endif
    .gatts_enable_params.service_changed = 0,
    .gatts_enable_params.attr_tab_size = BLE_GATTS_ATTR_TAB_SIZE_DEFAULT,
};

static MBRCONST ble_gap_conn_params_t gap_conn_params = {
    .min_conn_interval = BLE_MIN_CONN_INTERVAL,
    .max_conn_interval = BLE_MAX_CONN_INTERVAL,
    .slave_latency     = BLE_SLAVE_LATENCY,
    .conn_sup_timeout  = BLE_CONN_SUP_TIMEOUT,
};

static MBRCONST ble_gap_conn_sec_mode_t sec_mode = {
    // Values as set with:
    // BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
    .sm = 1,
    .lv = 1,
};

static MBRCONST ble_gap_adv_params_t m_adv_params = {
    .type        = BLE_GAP_ADV_TYPE_ADV_IND,            // connectable
    .p_peer_addr = NULL,                                // undirected advertisement
    .fp          = BLE_GAP_ADV_FP_ANY,
    .interval    = MSEC_TO_UNITS(100, UNIT_0_625_MS),   // approx 100ms
    .timeout     = 0,                                   // infinite advertisment
};

// Value of the 'info' characteristic.
static MBRCONST struct {
    uint8_t  version;
    uint8_t  pagesize;         // as a log2, actual page size is 2^pagesize
    uint16_t number_of_pages;
    char     chip_mnemonic[4]; // chip ID
    uint16_t app_first_page;
    uint16_t app_number_of_pages;
} char_info_value = {
    1,
    PAGE_SIZE_LOG2,
    FLASH_SIZE /  PAGE_SIZE, // will be updated when DYNAMIC_INFO_CHAR is set
#if NRF51
    {'N', '5', '1', 'a'}, // nRF51, 'serial number' a (if ever needed)
#elif NRF52
    {'N', '5', '2', 'a'}, // nRF52, 'serial number' a (if ever needed)
#endif
    APP_CODE_BASE / PAGE_SIZE, // will be updated when DYNAMIC_INFO_CHAR is set
    (APP_CODE_END / PAGE_SIZE) - (APP_CODE_BASE / PAGE_SIZE), // same for this one
};

static ble_uuid_t uuid;

static MBRCONST ble_gatts_attr_md_t attr_md_readonly = {
    .vloc    = BLE_GATTS_VLOC_STACK,
    .rd_auth = 0,
    .wr_auth = 0,
    .vlen    = 1,

    // Equivalent of:
    // BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md_readonly.read_perm);
    .read_perm = {
        .sm = 1,
        .lv = 1,
    },
};

static MBRCONST ble_gatts_attr_md_t attr_md_writeonly = {
    .vloc    = BLE_GATTS_VLOC_STACK,
    .rd_auth = 0,
    .wr_auth = 0,
    .vlen    = 1,

    // Equivalent of:
    // BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md_writeonly.write_perm);
    .write_perm = {
        .sm = 1,
        .lv = 1,
    },
};

static MBRCONST ble_gatts_attr_t attr_char_info = {
    .p_uuid    = &uuid,
    .p_attr_md = (ble_gatts_attr_md_t*)&attr_md_readonly,
    .init_len  = sizeof(char_info_value),
    .init_offs = 0,
    .p_value   = (void*)&char_info_value,
    .max_len   = sizeof(char_info_value),
};

static MBRCONST ble_gatts_attr_t attr_char_write = {
    .p_uuid    = &uuid,
    .p_attr_md = (ble_gatts_attr_md_t*)&attr_md_writeonly,
    .init_len  = 0,
    .init_offs = 0,
    .p_value   = NULL,
    .max_len   = (GATT_MTU_SIZE_DEFAULT - 3),
};

static MBRCONST ble_gatts_char_md_t char_md_readonly = {
    .char_props.broadcast      = 0,
    .char_props.read           = 1,
    .char_props.write_wo_resp  = 0,
    .char_props.write          = 0,
    .char_props.notify         = 0,
    .char_props.indicate       = 0,

    .p_char_user_desc  = NULL,
    .p_char_pf         = NULL,
    .p_user_desc_md    = NULL,
    .p_sccd_md         = NULL,
    .p_cccd_md         = NULL,
};

static MBRCONST ble_gatts_char_md_t char_md_write_notify = {
    .char_props.broadcast      = 0,
    .char_props.read           = 0,
    .char_props.write_wo_resp  = 0,
    .char_props.write          = 1,
    .char_props.notify         = 1,
    .char_props.indicate       = 0,

    .p_char_user_desc  = NULL,
    .p_char_pf         = NULL,
    .p_user_desc_md    = NULL,
    .p_sccd_md         = NULL,
    .p_cccd_md         = NULL,
};

#if PACKET_CHARACTERISTIC
static MBRCONST ble_gatts_char_md_t char_md_write_wo_resp = {
    .char_props.broadcast      = 0,
    .char_props.read           = 0,
    .char_props.write_wo_resp  = 1,
    .char_props.write          = 0,
    .char_props.notify         = 0,
    .char_props.indicate       = 0,

    .p_char_user_desc  = NULL,
    .p_char_pf         = NULL,
    .p_user_desc_md    = NULL,
    .p_sccd_md         = NULL,
    .p_cccd_md         = NULL,
};
#endif

ble_gatts_char_handles_t char_command_handles;
ble_gatts_char_handles_t char_buffer_handles;

static uint16_t ble_command_conn_handle;

#if !NRF51
static MBRCONST uint32_t app_ram_base = APP_RAM_BASE;
#endif

void ble_init(void) {
    LOG("enable ble");

    // Enable BLE stack.

    #if NRF51
    uint32_t err_code = sd_ble_enable(&ble_enable_params);
    #else
    uint32_t err_code = sd_ble_enable(&ble_enable_params, &app_ram_base);
    #endif
    if (err_code != 0) {
        LOG_NUM("cannot enable BLE:", err_code);
    }

    if (sd_ble_gap_device_name_set(&sec_mode,
                                   adv_data.name_value,
                                   sizeof(adv_data.name_value)) != 0) {
        LOG("cannot apply GAP parameters");
    }

    // set connection parameters
    if (sd_ble_gap_ppcp_set(&gap_conn_params) != 0) {
        LOG("cannot set PPCP parameters");
    }

    if (sd_ble_gap_adv_data_set((const uint8_t*)&adv_data, sizeof(adv_data), NULL, 0) != 0) {
        LOG("cannot apply advertisment data");
    }

    if (sd_ble_gap_adv_start(&m_adv_params) != 0) {
        LOG("cannot start advertisment");
    }

    uuid.uuid = UUID_DFU_SERVICE;
    if (sd_ble_uuid_vs_add(&uuid_base, &uuid.type) != 0) {
        LOG("cannot add UUID");
    }

    uint16_t service_handle;
    if (sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY,
                                 &uuid,
                                 &service_handle) != 0) {
        LOG("cannot add service");
    }

    // Load values for 'info' characteristic.
    // If this fails (nRF51 in bootloader mode), disable
    // DYNAMIC_INFO_CHAR.
    #if DYNAMIC_INFO_CHAR
    char_info_value.number_of_pages = NRF_FICR->CODESIZE;
    char_info_value.app_first_page = SD_SIZE_GET(MBR_SIZE) / PAGE_SIZE;
    char_info_value.app_number_of_pages = char_info_value.number_of_pages - char_info_value.app_first_page;
    #endif

    // Add 'info' characteristic
    uuid.uuid = UUID_DFU_CHAR_INFO;
    ble_gatts_char_handles_t handles;
    if (sd_ble_gatts_characteristic_add(BLE_GATT_HANDLE_INVALID,
                                        &char_md_readonly,
                                        &attr_char_info,
                                        &handles) != 0) {
        LOG("cannot add info char");
    }

    // Add 'command' characteristic
    uuid.uuid = UUID_DFU_CHAR_COMMAND;
    if (sd_ble_gatts_characteristic_add(BLE_GATT_HANDLE_INVALID,
                                        &char_md_write_notify,
                                        &attr_char_write,
                                        &char_command_handles) != 0) {
        LOG("cannot add cmd char");
    }

#if PACKET_CHARACTERISTIC
    // Add 'buffer' characteristic
    uuid.uuid = UUID_DFU_CHAR_BUFFER;
    if (sd_ble_gatts_characteristic_add(BLE_GATT_HANDLE_INVALID,
                                        &char_md_write_wo_resp,
                                        &attr_char_write,
                                        &char_buffer_handles) != 0) {
        LOG("cannot add buf char");
    }
#endif
}



void handle_irq(void);

void ble_run() {
    // Now wait for incoming events, using the 'thread model' (instead of
    // the IRQ model). This saves 20 bytes.
    while (1) {
        //__WFE();
        sd_app_evt_wait();
        handle_irq();
    }
}

static uint8_t m_ble_evt_buf[sizeof(ble_evt_t) + (GATT_MTU_SIZE_DEFAULT)] __attribute__ ((aligned (4)));

static void ble_evt_handler(ble_evt_t * p_ble_evt);

void handle_irq(void) {
    uint32_t evt_id;
    while (sd_evt_get(&evt_id) != NRF_ERROR_NOT_FOUND) {
        sd_evt_handler(evt_id);
    }

    while (1) {
        uint16_t evt_len = sizeof(m_ble_evt_buf);
        uint32_t err_code = sd_ble_evt_get(m_ble_evt_buf, &evt_len);
#if DEBUG
        if (err_code != NRF_SUCCESS) {
           if (err_code == NRF_ERROR_NOT_FOUND) {
               // expected
           } else if (err_code == NRF_ERROR_INVALID_ADDR) {
               LOG("ble event error: invalid addr");
           } else if (err_code == NRF_ERROR_DATA_SIZE) {
               LOG("ble event error: data size");
           } else {
               LOG("ble event error: other");
           }
        }
#endif
        if (err_code != NRF_SUCCESS) return; // may be "not found" or a serious issue
        ble_evt_handler((ble_evt_t *)m_ble_evt_buf);
    };
}

static void ble_evt_handler(ble_evt_t * p_ble_evt) {
    switch (p_ble_evt->header.evt_id) {
        case BLE_GAP_EVT_CONNECTED: {
            LOG("ble: connected");
            uint16_t  conn_handle = p_ble_evt->evt.gatts_evt.conn_handle;
            if (sd_ble_gap_conn_param_update(conn_handle, &gap_conn_params) != 0) {
                LOG("! failed to update conn params");
            }
            break;
        }

        case BLE_GAP_EVT_DISCONNECTED: {
            LOG("ble: disconnected");
            if (sd_ble_gap_adv_start(&m_adv_params) != 0) {
                LOG("Could not restart advertising after disconnect.");
            }
            break;
        }

        case BLE_GATTS_EVT_HVC: {
            LOG("ble: hvc");
            break;
        }

        case BLE_GATTS_EVT_WRITE: {
            uint16_t  conn_handle = p_ble_evt->evt.gatts_evt.conn_handle;
            uint16_t  attr_handle = p_ble_evt->evt.gatts_evt.params.write.handle;
            uint16_t  data_len    = p_ble_evt->evt.gatts_evt.params.write.len;
            uint8_t * data        = &p_ble_evt->evt.gatts_evt.params.write.data[0];

#if DEBUG
            //for (size_t i=0; i<data_len; i++) {
            //    uart_write_char(data[i]);
            //}
            //LOG("");
#endif

            // TODO: for now all writes must be using this handle (there
            // is only one writable character). So we can avoid this check
            // (saving 12 bytes).
            if (attr_handle == char_command_handles.value_handle) {
                ble_command_conn_handle = conn_handle;
                handle_command(data_len, (ble_command_t*)data);
            } else if (PACKET_CHARACTERISTIC && attr_handle == char_buffer_handles.value_handle) {
                ble_command_conn_handle = conn_handle;
                handle_buffer(data_len, data);
            }
            break;
        }

        case BLE_GAP_EVT_CONN_PARAM_UPDATE: {
            LOG("ble: conn param update");
            if (p_ble_evt->evt.gap_evt.params.conn_param_update.conn_params.min_conn_interval > 10) {
                LOG("  > 10");
            }
            if (p_ble_evt->evt.gap_evt.params.conn_param_update.conn_params.min_conn_interval > 20) {
                LOG("  > 20");
            }
            if (p_ble_evt->evt.gap_evt.params.conn_param_update.conn_params.min_conn_interval > 30) {
                LOG("  > 30");
            }
            if (p_ble_evt->evt.gap_evt.params.conn_param_update.conn_params.min_conn_interval > 40) {
                LOG("  > 40");
            }
            if (p_ble_evt->evt.gap_evt.params.conn_param_update.conn_params.min_conn_interval > 50) {
                LOG("  > 50");
            }
            if (p_ble_evt->evt.gap_evt.params.conn_param_update.conn_params.min_conn_interval > 100) {
                LOG("  > 100");
            }
            break;
        }

        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            LOG("ble: sys attr missing");
            break;

#if NRF52
        case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:
            LOG("ble: exchange MTU request");
            sd_ble_gatts_exchange_mtu_reply(p_ble_evt->evt.gatts_evt.conn_handle, GATT_MTU_SIZE_DEFAULT);
            break;
#endif

        case BLE_EVT_TX_COMPLETE:
            //LOG("ble: tx complete");
            break;

#if NRF52
        case BLE_GAP_EVT_ADV_REPORT:
            LOG("ble: adv report");
            break;

        case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST:
            LOG("ble: conn param update request");
            break;

        case BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP:
            LOG("ble: prim srvc disc rsp");
            break;
#endif

        default: {
            LOG("ble: ???");
            break;
        }
    }
}

void ble_send_reply(uint8_t code) {
    // send notification
    uint8_t reply_ok[] = {code};
    uint16_t reply_ok_len = sizeof(reply_ok);
    const ble_gatts_hvx_params_t hvx_params = {
        .handle = char_command_handles.value_handle,
        .type = BLE_GATT_HVX_NOTIFICATION,
        .offset = 0,
        .p_len = &reply_ok_len,
        .p_data = reply_ok,
    };
    uint32_t err_val = sd_ble_gatts_hvx(ble_command_conn_handle, &hvx_params);
    if (err_val == BLE_ERROR_INVALID_CONN_HANDLE) {
        LOG("  notify: BLE_ERROR_INVALID_CONN_HANDLE");
    } else if (err_val == NRF_ERROR_INVALID_STATE) {
        LOG("  notify: NRF_ERROR_INVALID_STATE");
    } else if (err_val == NRF_ERROR_INVALID_ADDR) {
        LOG("  notify: NRF_ERROR_INVALID_ADDR");
    } else if (err_val == NRF_ERROR_INVALID_PARAM) {
        LOG("  notify: NRF_ERROR_INVALID_PARAM");
    } else if (err_val == BLE_ERROR_INVALID_ATTR_HANDLE) {
        LOG("  notify: BLE_ERROR_INVALID_ATTR_HANDLE");
    } else if (err_val == BLE_ERROR_GATTS_INVALID_ATTR_TYPE) {
        LOG("  notify: BLE_ERROR_GATTS_INVALID_ATTR_TYPE");
    } else if (err_val != 0) {
        LOG("  notify: failed to send notification");
    }
}
