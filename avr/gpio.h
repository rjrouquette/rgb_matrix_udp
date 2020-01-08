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

#endif //RGB_MATRIX_AVR_GPIO_H
