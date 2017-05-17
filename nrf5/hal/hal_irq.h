/*
 * This file is part of the Micro Python project, http://micropython.org/
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

#ifndef HAL_IRQ_H__
#define HAL_IRQ_H__

#include <stdint.h>

#include "nrf.h"

#if BLUETOOTH_SD
#include "nrf_sdm.h"
#endif

static inline void hal_irq_clear(uint32_t irq_num) {
#if BLUETOOTH_SD
    sd_nvic_ClearPendingIRQ(irq_num);
#else
    NVIC_ClearPendingIRQ(irq_num);
#endif
}

static inline void hal_irq_enable(uint32_t irq_num) {
    hal_irq_clear(irq_num);
#if BLUETOOTH_SD
    sd_nvic_EnableIRQ(irq_num);
#else
    NVIC_EnableIRQ(irq_num);
#endif
}

static inline void hal_irq_disable(uint32_t irq_num) {
#if BLUETOOTH_SD
    sd_nvic_DisableIRQ(irq_num);
#else
    NVIC_DisableIRQ(irq_num);
#endif
}

static inline void hal_irq_priority(uint32_t irq_num, uint8_t priority) {
#if BLUETOOTH_SD
    sd_nvic_SetPriority(irq_num, priority);
#else
    NVIC_SetPriority(irq_num, priority);
#endif
}

static inline void hal_irq_pending(uint32_t irq_num) {
#if BLUETOOTH_SD
    sd_nvic_SetPendingIRQ(irq_num);
#else
    NVIC_SetPendingIRQ(irq_num);
#endif
}

#endif // HAL_IRQ_H__
