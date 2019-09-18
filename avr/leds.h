//
// Created by robert on 9/18/19.
//

#ifndef RGB_MATRIX_AVR_LEDS_H
#define RGB_MATRIX_AVR_LEDS_H

#include <avr/io.h>

#define LED_PORT PORTA
#define LED_MASK_0 0x08u
#define LED_MASK_1 0x10u
#define LED_MASK_2 0x20u

inline void ledOn0() { LED_PORT.OUTSET = LED_MASK_0; }
inline void ledOn1() { LED_PORT.OUTSET = LED_MASK_1; }
inline void ledOn2() { LED_PORT.OUTSET = LED_MASK_2; }

inline void ledOff0() { LED_PORT.OUTCLR = LED_MASK_0; }
inline void ledOff1() { LED_PORT.OUTCLR = LED_MASK_1; }
inline void ledOff2() { LED_PORT.OUTCLR = LED_MASK_2; }

#endif //RGB_MATRIX_AVR_LEDS_H
