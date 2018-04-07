/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2016 Damien P. George
 * Copyright (c) 2017 Ayke van Laethem
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

#include "py/obj.h"
#include "py/mphal.h"
#include "modules/machine/pin.h"
#include "py/runtime.h"
#include "ble_drv.h"

#if MICROPY_PY_NEOPIXEL

#if BLUETOOTH_SD
#include "nrf_soc.h"
#define BLUETOOTH_STACK_ENABLED() (ble_drv_stack_enabled())
#else
#define BLUETOOTH_STACK_ENABLED() (0)
#endif

#define SD_SESSION_DEBUG 0
#if SD_SESSION_DEBUG
#define DEBUG_LOG(s) printf(s)
#else
#define DEBUG_LOG(s)
#endif

STATIC void neopixel_write_func(const pin_obj_t *pin, const uint8_t *buffer, size_t length) {
    uint32_t pin_mask = pin->pin_mask;
    volatile uint32_t *port_set = &((NRF_GPIO_Type *)(GPIO_BASE(pin->port)))->OUTSET;
    volatile uint32_t *port_clr = &((NRF_GPIO_Type *)(GPIO_BASE(pin->port)))->OUTCLR;
    for (size_t ibuf = 0; ibuf < length; ibuf++) { // for each byte...
        uint32_t value = buffer[ibuf] << 24;
        // For some understanding of the protocol:
        // https://cpldcpu.com/2014/01/14/light_ws2812-library-v2-0-part-i-understanding-the-ws2812/
        // https://wp.josh.com/2014/05/13/ws2812-neopixels-are-not-so-finicky-once-you-get-to-know-them/
        //
        // Most of the description below is for the nRF51. nRF52 support
        // is also included, but it is simply an extension of the nRF51
        // support with extra NOPs (well, branch to next instruction). It
        // has been tested with a logic analyzer instead of counting
        // cycles, as the Cortex-M4 (in the nRF52) is far less predictable
        // in execution speed.
        //
        // To send a 0 bit (200ns-500ns or 4-8 cycles):
        // - [2] set GPIO high
        // - [1] pop off the highest bit of the byte into the carry flag,
        //       this bit is 0
        // - [1] do not take the branch
        // - [2] set GPIO low
        // - [3] nop
        // - [3] nop
        // - [2] set GPIO low
        // Together there are 4 cycles between high and low, which is the
        // lower bound according to the datasheet. Testing shows that it
        // can be much lower.
        //
        // To send a 1 bit (625ns-5500ns or 10-88 cycles):
        // - [2] set GPIO high
        // - [1] pop off the highest bit of the byte into the carry flag,
        //       this bit is 1
        // - [3] jump over the str instruction so the GPIO isn't changed
        // - [3] nop
        // - [3] nop
        // - [2] set GPIO low
        // These are 12 cycles, enough to be distinguishable as a high
        // bit. They could maybe be reduced to 9 cycles, by removing one
        // of the 3-cycle nops. This should be legal only on WS2812 and
        // SK6812 chips (not on WS2812B).
        //
        // Note that both a '1' bit and a '0' bit take exactly 18 cycles.
        // This is within the allowed range of a bit: 18 cycles equals
        // 1125ns, while the lower bound is 1100ns.
        //
        // The duration of the 'low' period (after the above 'high'
        // periods) can be much longer. The datasheets say there is a
        // maximum of either 950ns or 750ns, but in practice it can be
        // much longer (up to about 5500ns). So we don't worry about that.
        // In a current GCC (5.4) it produces 8 extra cycles.
        size_t ibit = 8;
        __asm__ __volatile__(
                ".syntax unified                 \n"
                "send_bit:                       \n"
                "  str   %[pin_mask], %[port_set]\n" // [2]   *port_set = pin_mask;
                #ifdef NRF52
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                #if BLUETOOTH_SD
                "  b     . + 2                   \n"
                #endif
                #endif // NRF52
                "  lsls  %[value], #1            \n" // [1]   value <<= 1;
                "  bcs.n . + 4                   \n" // [1/3] if ( /* shifted-off value is bit 0 */ )
                "  str   %[pin_mask], %[port_clr]\n" // [2]       *port_clr = pin_mask;
                "  b     . + 2                   \n" // [3]   // nop, wait 3 cycles by branching to next instruction
                "  b     . + 2                   \n" // [3]   // nop, wait 3 cycles by branching to next instruction
                #ifdef NRF52
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                #endif // NRF52
                "  str   %[pin_mask], %[port_clr]\n" // [2]   *port_clr = pin_mask;
                "  subs  %[ibit], #1             \n" // [1]   ibit--;
                #ifdef NRF52
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                #if BLUETOOTH_SD
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                "  b     . + 2                   \n"
                #endif
                #endif // NRF52
                "  bne.n send_bit                \n" // [1/3] if (ibit != 0) goto send_bit;
                : [value]"+l" (value),
                  [ibit]"+l" (ibit)
                : [port_set]"m" (*port_set),
                  [port_clr]"m" (*port_clr),
                  [pin_mask]"l" (pin_mask)
                : "cc");
    }
}

#if BLUETOOTH_SD

STATIC const pin_obj_t *neopixel_pin;
STATIC const uint8_t *neopixel_buffer;
STATIC size_t neopixel_length;
STATIC volatile bool neopixel_busy = false;

STATIC const nrf_radio_signal_callback_return_param_t neopixel_callback_return = {
    .callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_END,
};

STATIC nrf_radio_signal_callback_return_param_t * neopixel_write_callback(uint8_t signal_type) {
    if (signal_type == NRF_RADIO_CALLBACK_SIGNAL_TYPE_START) {
        neopixel_write_func(neopixel_pin, neopixel_buffer, neopixel_length);
        neopixel_busy = false;
    }
    return (nrf_radio_signal_callback_return_param_t*)&neopixel_callback_return;
}

#endif

STATIC void neopixel_write(const pin_obj_t *pin, const uint8_t *buffer, size_t length) {
#if BLUETOOTH_SD
    if (BLUETOOTH_STACK_ENABLED()) {
        neopixel_pin = pin;
        neopixel_buffer = buffer;
        neopixel_length = length;
        neopixel_busy = true;
        if (sd_radio_session_open(neopixel_write_callback) != 0) {
            DEBUG_LOG("cannot open session\n");
        }
        nrf_radio_request_t request = {
            .request_type = NRF_RADIO_REQ_TYPE_EARLIEST,
            .params = {
                .earliest = {
                    #if NRF51
                    .hfclk = NRF_RADIO_HFCLK_CFG_DEFAULT,
                    #else // NRF52
                    .hfclk = NRF_RADIO_HFCLK_CFG_XTAL_GUARANTEED,
                    #endif
                    .length_us = 100 + 10 * length, // heuristic, each byte costs 9.5us
                    .priority = NRF_RADIO_PRIORITY_NORMAL,
                    .timeout_us = 100000, // 100ms, semi-arbitrarily chosen
                },
            },
        };
        if (sd_radio_request(&request) != 0) {
            DEBUG_LOG("cannot do session request\n");
        }
        while (neopixel_busy) {
            __WFE();
        }
        if (sd_radio_session_close() != 0) {
            DEBUG_LOG("cannot close session\n");
        }
    } else {
        neopixel_write_func(pin, buffer, length);
    }
#else
    neopixel_write_func(pin, buffer, length);
#endif
}

STATIC mp_obj_t neopixel_write_(mp_obj_t pin_in, mp_obj_t buf_in) {
    mp_buffer_info_t bufferinfo;
    mp_get_buffer_raise(buf_in, &bufferinfo, MP_BUFFER_READ);
    if (!MP_OBJ_IS_TYPE(pin_in, &pin_type)) {
        mp_raise_ValueError("pin is  not a Pin object");
    }
    pin_obj_t *pin = (pin_obj_t*)pin_in;
    neopixel_write(pin, bufferinfo.buf, bufferinfo.len);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(neopixel_write_obj, neopixel_write_);

// Here follows what has been copied from micro:bit and modified a bit.
// https://github.com/bbcmicrobit/micropython/blob/master/source/microbit/modneopixel.cpp

extern const mp_obj_type_t neopixel_type;

typedef struct {
    mp_obj_base_t base;
    pin_obj_t *pin;
    size_t num_pixels;
    uint8_t *buffer; // 3x the length of 'num_pixels'
} neopixel_obj_t;

STATIC mp_obj_t neopixel_make_new(const mp_obj_type_t *type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    (void)type_in;
    mp_arg_check_num(n_args, n_kw, 2, 2, false);

    if (!MP_OBJ_IS_TYPE(args[0], &pin_type)) {
        mp_raise_ValueError("pin is  not a Pin object");
    }
    pin_obj_t *pin = args[0];

    mp_int_t num_pixels = mp_obj_get_int(args[1]);
    if (num_pixels <= 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "invalid number of pixels"));
    }

    neopixel_obj_t *self = m_new_obj(neopixel_obj_t);
    self->base.type = &neopixel_type;
    self->pin = pin;
    self->num_pixels = num_pixels;
    self->buffer = m_new0(uint8_t, num_pixels * 3);

    return self;
}

STATIC mp_obj_t neopixel_unary_op(mp_unary_op_t op, mp_obj_t self_in) {
    neopixel_obj_t *self = (neopixel_obj_t*)self_in;
    switch (op) {
        case MP_UNARY_OP_LEN: return MP_OBJ_NEW_SMALL_INT(self->num_pixels);
        default: return MP_OBJ_NULL; // op not supported
    }
}

STATIC mp_obj_t neopixel_subscr(mp_obj_t self_in, mp_obj_t index_in, mp_obj_t value) {
    neopixel_obj_t *self = (neopixel_obj_t*)self_in;
    mp_uint_t index = mp_get_index(self->base.type, self->num_pixels, index_in, false);
    if (value == MP_OBJ_NULL) {
        // delete item
        return MP_OBJ_NULL; // op not supported
    } else if (value == MP_OBJ_SENTINEL) {
        // load
        mp_obj_t rgb[3] = {
            MP_OBJ_NEW_SMALL_INT(self->buffer[index * 3 + 1]), // red
            MP_OBJ_NEW_SMALL_INT(self->buffer[index * 3 + 0]), // green
            MP_OBJ_NEW_SMALL_INT(self->buffer[index * 3 + 2]), // blue
        };
        return mp_obj_new_tuple(3, rgb);
    } else {
        // store
        mp_obj_t *rgb;
        mp_obj_get_array_fixed_n(value, 3, &rgb);
        mp_int_t r = mp_obj_get_int(rgb[0]);
        mp_int_t g = mp_obj_get_int(rgb[1]);
        mp_int_t b = mp_obj_get_int(rgb[2]);
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
            mp_raise_ValueError("invalid colour");
        }
        uint8_t *buf = &self->buffer[index * 3];
        buf[0] = g;
        buf[1] = r;
        buf[2] = b;
        return mp_const_none;
    }
}

STATIC mp_obj_t neopixel_show_(mp_obj_t self_in) {
    neopixel_obj_t *self = (neopixel_obj_t*)self_in;
    neopixel_write(self->pin, self->buffer, self->num_pixels * 3);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(neopixel_show_obj, neopixel_show_);

STATIC const mp_map_elem_t neopixel_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR_show), (mp_obj_t)&neopixel_show_obj },
};

STATIC MP_DEFINE_CONST_DICT(neopixel_locals_dict, neopixel_locals_dict_table);

const mp_obj_type_t neopixel_type = {
    { &mp_type_type },
    .name = MP_QSTR_NeoPixel,
    .make_new = neopixel_make_new,
    .unary_op = neopixel_unary_op,
    .subscr = neopixel_subscr,
    .locals_dict = (mp_obj_dict_t*)&neopixel_locals_dict,
};

STATIC const mp_rom_map_elem_t neopixel_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_neopixel) },
    { MP_ROM_QSTR(MP_QSTR_NeoPixel), MP_ROM_PTR(&neopixel_type) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&neopixel_write_obj) },
};
STATIC MP_DEFINE_CONST_DICT(neopixel_module_globals, neopixel_module_globals_table);

const mp_obj_module_t neopixel_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&neopixel_module_globals,
};

#endif // MICROPY_PY_NEOPIXEL
