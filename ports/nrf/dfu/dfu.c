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
#include <stdio.h>
#include <string.h>

#include "mpconfigport.h"
#include "nrf_sdm.h"
#include "nrf_mbr.h"
#include "nrf_soc.h"
#if NRF52
#include "nrf_nvic.h"
#endif

#include "dfu.h"
#include "dfu_ble.h"

#if defined(DFU_TYPE_bootloader)
__attribute__((section(".bootloaderaddr"),used))
const uint32_t *bootloaderaddr = BOOTLOADER_START_ADDR;
#endif

#if DEBUG
#if NRF51
static void softdevice_assert_handler(uint32_t pc, uint16_t line_number, const uint8_t * p_file_name) {
#else
static void softdevice_assert_handler(uint32_t id, uint32_t pc, uint32_t info) {
#endif
    LOG("ERROR: SoftDevice assert!!!");
    while (1);
}
#else
void HardFault_Handler(void);
#if NRF51
#define softdevice_assert_handler ((softdevice_assertion_handler_t)HardFault_Handler)
#else
#define softdevice_assert_handler ((nrf_fault_handler_t)HardFault_Handler)
#endif
#endif

static void jump_to_app() {
#if DEBUG
    uart_disable();
#endif
    // Note that the SoftDevice needs to be disabled before calling this
    // function.

#if defined(DFU_TYPE_mbr)
    *(uint32_t*)MBR_VECTOR_TABLE = SD_CODE_BASE;
#endif

    // The ISR vector contains these entries (among others):
    // 0: pointer to the end of the stack (_estack)
    // 1: the Reset_Handler
    // Note that we can't just jump to the app, we have to 'reset' the
    // stack pointer to the beginning of the stack (e.g. the highest
    // address).
    uint32_t *sd_isr = (uint32_t*)SD_CODE_BASE;
    uint32_t new_sp = sd_isr[0]; // load end of stack (_estack)
    uint32_t new_pc = sd_isr[1]; // load Reset_Handler
    __asm__ __volatile__(
            "mov sp, %[new_sp]\n" // set stack pointer to initial stack pointer
            "mov pc, %[new_pc]\n" // jump to SoftDevice Reset_Vector
            :
            : [new_sp]"r" (new_sp),
              [new_pc]"r" (new_pc));
    __builtin_unreachable();
}


uint8_t flash_buf[PAGE_SIZE];
uint8_t *flash_buf_ptr;

#if !NRF51
static MBRCONST nrf_clock_lf_cfg_t clock_config = {
    .source = NRF_CLOCK_LF_SRC_SYNTH,
    .rc_ctiv = 0,
    .rc_temp_ctiv = 0,
    .xtal_accuracy = 0,
};
#endif

void _start(void) {
#if DEBUG
    uart_enable();
#endif

    LOG("");

    // Set the vector table. This may be used by the SoftDevice.
    LOG("init MBR vector table");
#if defined(DFU_TYPE_mbr)
    *(uint32_t*)MBR_VECTOR_TABLE = 0;
#elif defined(DFU_TYPE_bootloader)
    *(uint32_t*)MBR_VECTOR_TABLE = SD_CODE_BASE;
#endif


    // Check whether there is something that looks like a reset handler at
    // the app ISR vector. If the page has been cleared, it will be
    // 0xffffffff.
    // Also, check whether GPREGRET is set: if it is, the application has
    // requested we stay in DFU mode.
    uint32_t *app_isr = (uint32_t*)APP_CODE_BASE;
    uint32_t reset_handler = app_isr[1];
    if (reset_handler != 0xffffffff && NRF_POWER->GPREGRET == 0) {
        // There is a valid application and the application hasn't
        // requested for DFU mode.
        LOG("valid Reset_Handler");
        jump_to_app();
    } else {
        LOG("invalid Reset_Handler or GPREGRET was set");
    }

    // Make sure a reset won't jump to the bootloader again. This only
    // matters if the application requested to go to the bootloader by
    // setting this register (it defaults to 0).
    NRF_POWER->GPREGRET = 0;

    // This always uses the internal clock. Which takes more power, but
    // DFU mode isn't meant to be enabled for long periods anyway. It
    // avoids having to configure internal/external clocks.
    LOG("enable sd");
    #if NRF51
    sd_softdevice_enable(NRF_CLOCK_LFCLKSRC_RC_250_PPM_250MS_CALIBRATION, softdevice_assert_handler);
    #else
    sd_softdevice_enable(&clock_config, softdevice_assert_handler);
    #endif

    // Enable IRQ for SoftDevice.
    // Disabled as it is not necessary as all events are handled in
    // ble_run().
    //LOG("enable irq");
    //sd_nvic_EnableIRQ(SWI2_IRQn);

    flash_buf_ptr = flash_buf;

    ble_init();

    LOG("waiting...");
    ble_run();
}

void handle_command(uint16_t data_len, ble_command_t *cmd) {
    // Format: command (1 byte), payload (any length, up to 19 bytes with
    // default MTU)
    if (INPUT_CHECKS && data_len == 0) return;
    if (cmd->any.command == COMMAND_RESET) {
        LOG("command: reset");
        sd_nvic_SystemReset();
    } else if (cmd->any.command == COMMAND_ERASE_PAGE) {
        if (INPUT_CHECKS && data_len < sizeof(cmd->erase)) return;
        LOG("command: erase page");
        uint32_t err_code = sd_flash_page_erase(cmd->erase.page);
        if (ERROR_REPORTING && err_code != 0) {
            LOG("  error: cannot schedule page erase");
            // Error: the erase command wasn't scheduled.
            ble_send_reply(1);
        }
        if (err_code == NRF_ERROR_INTERNAL) {
            LOG("! internal error");
        } else if (err_code == NRF_ERROR_BUSY) {
            LOG("! busy");
        } else if (err_code != 0) {
            LOG("! could not start erase of page");
        }
    } else if (cmd->any.command == COMMAND_WRITE_BUFFER) {
        LOG("command: do write");
        if (INPUT_CHECKS && data_len < sizeof(cmd->write)) return;
        if (INPUT_CHECKS && cmd->write.n_words > PAGE_SIZE / 4) return;
#if FLASH_PAGE_CHECKS
        if (cmd->write.page < APP_CODE_BASE / PAGE_SIZE || cmd->write.page >= (uint32_t)APP_CODE_END / PAGE_SIZE) {
            if (ERROR_REPORTING) {
                LOG("  error: page out of range");
                ble_send_reply(1);
            }
            return;
        }
#endif
        if (sd_flash_write((uint32_t*)((uintptr_t)cmd->write.page * PAGE_SIZE), (uint32_t*)flash_buf, cmd->write.n_words) != 0) {
            LOG("  error: could not start page write");
            if (ERROR_REPORTING) {
                ble_send_reply(1);
            }
        }
        flash_buf_ptr = flash_buf;
#if !PACKET_CHARACTERISTIC
    } else if (cmd->any.command == COMMAND_ADD_BUFFER) {
        const uint8_t *in_start = cmd->buffer.buffer;
        uint8_t *out_end = flash_buf_ptr + (data_len - 4);
        if (INPUT_CHECKS && out_end > flash_buf + PAGE_SIZE) {
            out_end = flash_buf + PAGE_SIZE;
        }
        while (flash_buf_ptr != out_end) {
            *flash_buf_ptr++ = *in_start++;
        }
#endif
#if DEBUG
    } else if (cmd->any.command == COMMAND_PING) {
        // Only for debugging
        LOG("command: ping");
        ble_send_reply(0);
    } else if (cmd->any.command == COMMAND_START) {
        // Not implementing this saves ~22 bytes.
        // Note that it doesn't always work. That has probably something
        // to do with the SoftDevice.
        LOG("command: start app");
        if (sd_softdevice_disable() != 0) {
            LOG("could not disable SoftDevice");
        }
        jump_to_app();
#endif
    } else {
        LOG("command: ???");
    }
}

void handle_buffer(uint16_t data_len, uint8_t *data) {
    const uint8_t *in_start = data;
    uint8_t *out_end = flash_buf_ptr + data_len;
    if (INPUT_CHECKS && out_end > flash_buf + PAGE_SIZE) {
        return;
    }
    while (flash_buf_ptr != out_end) {
        *flash_buf_ptr++ = *in_start++;
    }
}

void sd_evt_handler(uint32_t evt_id) {
    switch (evt_id) {
        case NRF_EVT_FLASH_OPERATION_SUCCESS:
            //LOG("sd evt: flash operation finished");
            ble_send_reply(0);
            break;
        case NRF_EVT_FLASH_OPERATION_ERROR:
            LOG("sd evt: flash operation error");
            if (ERROR_REPORTING) {
                ble_send_reply(1);
            }
            break;
        default:
            LOG_NUM("sd evt:", evt_id);
            break;
    }
}
