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


#include "nrf_uart.h"
#include "dfu.h"
#include "mpconfigboard.h"
#include "dfu_uart.h"


void uart_write_char(char ch) {
    NRF_UART0->TXD = ch;
    while (NRF_UART0->EVENTS_TXDRDY != 1) {}
    NRF_UART0->EVENTS_TXDRDY = 0;
}

void uart_write(char *s) {
    while (*s) {
        uart_write_char(*s++);
    }
}

void uart_write_num(uint32_t n) {
    uart_write_char('0');
    uart_write_char('x');

    // write hex digits
    for (int i = 0; i < 8; i++) {
        char ch = (n >> 28) + '0';
        if (ch > '9') {
            ch = (n >> 28) + 'a' - 10;
        }
        uart_write_char(ch);
        n <<= 4;
    }
}

#if DEBUG
void uart_enable() {
    // TODO: set correct GPIO configuration? Only necessary when system
    // goes to OFF state.
    NRF_UART0->ENABLE        = UART_ENABLE_ENABLE_Enabled;
    NRF_UART0->BAUDRATE      = UART_BAUDRATE_BAUDRATE_Baud115200;
    NRF_UART0->TASKS_STARTTX = 1;
    #if defined(WT51822_S4AT)
    NRF_UART0->PSELTXD       = 2; // P0.02
    #elif defined(PCA10040)
    NRF_UART0->PSELTXD       = 6; // P0.06
    #else
    #error Setup TX pin for debugging
    #endif
}
#endif

void uart_disable() {
    NRF_UART0->ENABLE        = UART_ENABLE_ENABLE_Disabled;
    NRF_UART0->PSELTXD       = 0xffffffff;
}
