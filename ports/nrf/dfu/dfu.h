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


#pragma once

#include "mpconfigboard.h"
#include "dfu_uart.h"

#define DEBUG                  (0)
#define INPUT_CHECKS           (1) // whether the received buffer is the correct length
#define FLASH_PAGE_CHECKS      (1) // check that flash pages are within the app area
#define ERROR_REPORTING        (1) // send error when something goes wrong (e.g. flash write fail)
#define PACKET_CHARACTERISTIC  (1) // add a separate transport characteristic - improves speed but costs 32 bytes
#define DYNAMIC_INFO_CHAR      (1) // load 'info' characteristic from calculated values

#if DEBUG
#define LOG(s) uart_write(s "\r\n")
#define LOG_NUM(s, n) uart_write(s " "); uart_write_num(n); uart_write("\r\n")
#else
#define LOG(s)
#define LOG_NUM(s, n)
#endif

extern const uint32_t _stext[];

#define BOOTLOADER_START_ADDR  (_stext)
#define SD_CODE_BASE           (0x00001000)
#define MBR_VECTOR_TABLE       (0x20000000)

#if NRF51
#define APP_CODE_BASE          (0x00018000)
#define APP_RAM_BASE           (0x20002000)
#define PAGE_SIZE              (1024)
#define PAGE_SIZE_LOG2         (10)
#define FLASH_SIZE             (0x00040000) // 256kB - nearly all of them are 256kB

#elif NRF52
#define APP_CODE_BASE          (0x0001F000) // TODO: check SD version
#define APP_RAM_BASE           (0x20002800)
#define PAGE_SIZE              (4096)
#define PAGE_SIZE_LOG2         (12)

#if NRF52832_XXAA
#define FLASH_SIZE             (0x00080000) // 512kB
#elif NRF52840_XXAA
#define FLASH_SIZE             (0x00100000) // 1MB
#else
#error Unknown nRF52 chip
#endif

#else
#error Unknown chip
#endif

#if defined(DFU_TYPE_mbr)
#define APP_BOOTLOADER_SIZE    (0)
#else
#define APP_BOOTLOADER_SIZE    (PAGE_SIZE)
#endif

#define APP_CODE_END           (FLASH_SIZE - APP_BOOTLOADER_SIZE)

#define COMMAND_RESET        (0x01) // do a reset
#define COMMAND_ERASE_PAGE   (0x02) // start erasing this page
#define COMMAND_WRITE_BUFFER (0x03) // start writing this page and reset buffer
#define COMMAND_ADD_BUFFER   (0x04) // add data to write buffer
#define COMMAND_PING         (0x10) // just ask a response (debug)
#define COMMAND_START        (0x11) // start the app (debug, unreliable)

#if defined(DFU_TYPE_mbr) || !defined(NRF51)
#define MBRCONST
#else
#define MBRCONST const
#endif

typedef union {
    struct {
        uint8_t  command;
    } any;
    struct {
        uint8_t  command;
        uint8_t  flags; // or rather: padding
        uint16_t page;
    } erase; // COMMAND_ERASE_PAGE
#if !PACKET_CHARACTERISTIC
    struct {
        uint8_t  command;
        uint8_t  flags; // or rather: padding
        uint16_t padding;
        uint8_t  buffer[16];
    } buffer; // COMMAND_ADD_BUFFER
#endif
    struct {
        uint8_t  command;
        uint8_t  flags; // or rather: padding
        uint16_t page;
        uint16_t n_words;
    } write; // COMMAND_WRITE_BUFFER
} ble_command_t;

void handle_command(uint16_t data_len, ble_command_t *data);
void handle_buffer(uint16_t data_len, uint8_t *data);

void sd_evt_handler(uint32_t evt_id);
