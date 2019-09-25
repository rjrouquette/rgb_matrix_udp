//
// Created by robert on 9/18/19.
//

#ifndef RGB_MATRIX_AVR_GPIO_H
#define RGB_MATRIX_AVR_GPIO_H

#include "leds.h"
#include "mux.h"
#include "clkout.h"
#include "matrix.h"
#include "sram.h"

#define VSYNC_PORT PORTK
#define VSYNC_MASK 0x01u  // debug board errata
//#define VSYNC_MASK 0x20u

inline void waitVsync() {
    while ((VSYNC_PORT.IN & VSYNC_MASK) == 0) asm("");
}

inline void waitNotVsync() {
    while ((VSYNC_PORT.IN & VSYNC_MASK) != 0) asm("");
}

void initGpio();

#endif //RGB_MATRIX_AVR_GPIO_H
