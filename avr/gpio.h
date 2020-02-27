//
// Created by robert on 9/18/19.
//

#ifndef RGB_MATRIX_AVR_GPIO_H
#define RGB_MATRIX_AVR_GPIO_H

#include "leds.h"
#include "matrix.h"

#define HSYNC_PORT PORTC

#define META_PORT PORTD

void initGpio();

inline void nop1() { asm volatile("nop"); }
inline void nop2() { nop1(); nop1(); }
inline void nop4() { nop2(); nop2(); }

#endif //RGB_MATRIX_AVR_GPIO_H
