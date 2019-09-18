//
// Created by robert on 9/18/19.
//

#ifndef RGB_MATRIX_AVR_MUX_H
#define RGB_MATRIX_AVR_MUX_H

#include <avr/io.h>

#define OE_PORT PORTE
#define OE_MASK 0x60u
#define OE_MASK_0 0x40u
#define OE_MASK_1 0x20u

inline void enableOutput0() { OE_PORT.OUTSET = OE_MASK_0; }
inline void enableOutput1() { OE_PORT.OUTSET = OE_MASK_1; }

inline void disableOutput()  { OE_PORT.OUTCLR = OE_MASK; }


#define INE_PORT PORTK
#define INE_MASK 0x18u
#define INE_MASK_0 0x10u
#define INE_MASK_1 0x08u

inline void enableInput0() { INE_PORT.OUTSET = INE_MASK_0; }
inline void enableInput1() { INE_PORT.OUTSET = INE_MASK_1; }

inline void disableInput0() { INE_PORT.OUTCLR = INE_MASK_0; }
inline void disableInput1() { INE_PORT.OUTCLR = INE_MASK_1; }


#endif //RGB_MATRIX_AVR_MUX_H
