/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Glenn Ruben Bakke
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
#include "genhdr/isr-vector.h"

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

typedef void (*func)(void);

extern void  _start(void) __attribute__((noreturn));
extern void SystemInit(void);

#if 0
void uart_write(char *s);

void Default_Handler(void) {
    uart_write("Default_Handler\r\n");
    while (1);
}

void HardFault_Handler(void) {
    uart_write("HardFault_Handler\r\n");
    while (1);
}
#else
void Default_Handler(void) {
    while (1);
}
#endif

void Reset_Handler(void) {
#if 0
    // RAMON and RAMONB registers. These are the default values (after
    // reset, but are retained), so don't change them. Saves 20 bytes.
    uint32_t * ram_on_addr   = (uint32_t *)0x40000524;
    uint32_t * ram_on_b_addr = (uint32_t *)0x40000554;
    // RAM on in on-mode
    *ram_on_addr   = 3; // block 0 and 1
    *ram_on_b_addr = 3; // block 2 and 3
#endif

#if 0
    // RAM on in off-mode
    ram_on_addr   = 1 << 16;
    ram_on_b_addr = 1 << 17;
#endif

#if defined(DFU_TYPE_mbr) || !defined(NRF51)
    // Initialize .data segment. By avoiding non-zero non-const values, we
    // can also avoid this startup code. Saves 36 bytes.
    uint32_t * p_src  = &_sidata;
    uint32_t * p_dest = &_sdata;

    while (p_dest < &_edata) {
      *p_dest++ = *p_src++;
    }
#endif

    // Initialize .bss segment.
    uint32_t * p_bss     = &_sbss;
    uint32_t * p_bss_end = &_ebss;
    while (p_bss < p_bss_end) {
        *p_bss++ = 0ul;
    }

    _start();
}

void NMI_Handler            (void) __attribute__ ((weak, alias("Default_Handler")));
void HardFault_Handler      (void) __attribute__ ((weak, alias("Default_Handler")));


const func __Vectors[] __attribute__ ((section(".isr_vector"),used)) = {
    (func)&_estack,
    Reset_Handler,
    NMI_Handler, // TODO: should this one be redirected to the SoftDevice?
    HardFault_Handler,
    // Dirty hack to save space: the following IRQs aren't used by the
    // bootloader so we can put anything in this space. It saves 152
    // bytes.
    // TODO: make sure only even values get written here (e.g. by
    // selecting readonly literals). This to ensure that even if an
    // interrupt gets called here, the CPU will fault (as function
    // pointers must always have the lowest bit set in Thumb mode).
#if defined(DFU_TYPE_mbr)
    ISR_VECTOR_4,
    ISR_VECTOR_5,
    ISR_VECTOR_6,
    ISR_VECTOR_7,
    ISR_VECTOR_8,
    ISR_VECTOR_9,
    ISR_VECTOR_10,
    ISR_VECTOR_11,
    ISR_VECTOR_12,
    ISR_VECTOR_13,
    ISR_VECTOR_14,
    ISR_VECTOR_15,

    /* External Interrupts */
    ISR_VECTOR_16,
    ISR_VECTOR_17,
    ISR_VECTOR_18,
    ISR_VECTOR_19,
    ISR_VECTOR_20,
    ISR_VECTOR_21,
    ISR_VECTOR_22,
    ISR_VECTOR_23,
    ISR_VECTOR_24,
    ISR_VECTOR_25,
    ISR_VECTOR_26,
    ISR_VECTOR_27,
    ISR_VECTOR_28,
    ISR_VECTOR_29,
    ISR_VECTOR_30,
    ISR_VECTOR_31,
    ISR_VECTOR_32,
    ISR_VECTOR_33,
    ISR_VECTOR_34,
    ISR_VECTOR_35,
    ISR_VECTOR_36,
    ISR_VECTOR_37,
    ISR_VECTOR_38,
    ISR_VECTOR_39,
    ISR_VECTOR_40,
    ISR_VECTOR_41,
#if NRF52
    ISR_VECTOR_42,
    ISR_VECTOR_43,
    ISR_VECTOR_44,
    ISR_VECTOR_45,
    ISR_VECTOR_46,
    ISR_VECTOR_47,
    ISR_VECTOR_48,
    ISR_VECTOR_49,
    ISR_VECTOR_50,
    ISR_VECTOR_51,
    ISR_VECTOR_52,
    ISR_VECTOR_53,
#endif
#endif
};
