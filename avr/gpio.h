//
// Created by robert on 9/18/19.
//

#ifndef RGB_MATRIX_AVR_GPIO_H
#define RGB_MATRIX_AVR_GPIO_H

#include "leds.h"
#include "matrix.h"

#define VSYNC_PORT PORTE
#define VSYNC_MASK 0x01u

#define HSYNC_PORT PORTE

#define META_PORT PORTD

inline uint8_t isVsync() {
    return VSYNC_PORT.IN & VSYNC_MASK;
}

void initGpio();

inline void nop1() { asm volatile("nop"); }
inline void nop2() { nop1(); nop1(); }
inline void nop4() { nop2(); nop2(); }

#endif //RGB_MATRIX_AVR_GPIO_H
