/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Glenn Ruben Bakke
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

#include "mphalport.h"
#include "hal_timer.h"

#ifdef HAL_TIMER_MODULE_ENABLED

void hal_timer_init(hal_timer_conf_t const * p_timer_conf) {

}

void hal_timer_start(uint8_t id) {
}

void hal_timer_stop(uint8_t id) {
}

#if (MICROPY_PY_MACHINE_SOFT_PWM != 1)
void TIMER0_IRQHandler(void) {

}
#endif

void TIMER1_IRQHandler(void) {

}

void TIMER2_IRQHandler(void) {

}

#if NRF52

void TIMER3_IRQHandler(void) {

}

void TIMER4_IRQHandler(void) {

}

#endif

#endif // HAL_TIMER_MODULE_ENABLED
