//
// Created by robert on 9/18/19.
//

#ifndef RGB_MATRIX_AVR_LEDS_H
#define RGB_MATRIX_AVR_LEDS_H

#include <avr/io.h>

#define LED_PORT PORTC
#define LED_MASK 0x02u

inline void ledOn() { LED_PORT.OUTSET = LED_MASK; }
inline void ledOff() { LED_PORT.OUTCLR = LED_MASK; }
inline void ledToggle() { LED_PORT.OUTTGL = LED_MASK; }

#endif //RGB_MATRIX_AVR_LEDS_H
