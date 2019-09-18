//
// Created by robert on 9/18/19.
//

#ifndef RGB_MATRIX_AVR_CLKOUT_H
#define RGB_MATRIX_AVR_CLKOUT_H

#define CLK0_PORT PORTD
#define CLK1_PORT PORTE
#define CLK_PIN_MASK 0x80u

inline void enableClk0() {
    CLK0_PORT.OUTCLR = CLK_PIN_MASK;
    CLK0_PORT.DIRSET = CLK_PIN_MASK;
}

inline void disableClk0() {
    CLK0_PORT.OUTCLR = CLK_PIN_MASK;
    CLK0_PORT.DIRCLR = CLK_PIN_MASK;
}

inline void pulseClk0() {
    CLK0_PORT.OUTSET = CLK_PIN_MASK;
    CLK0_PORT.OUTCLR = CLK_PIN_MASK;
}

inline void enableClk1() {
    CLK1_PORT.OUTCLR = CLK_PIN_MASK;
    CLK1_PORT.DIRSET = CLK_PIN_MASK;
}

inline void disableClk1() {
    CLK1_PORT.OUTCLR = CLK_PIN_MASK;
    CLK1_PORT.DIRCLR = CLK_PIN_MASK;
}

inline void pulseClk1() {
    CLK1_PORT.OUTSET = CLK_PIN_MASK;
    CLK1_PORT.OUTCLR = CLK_PIN_MASK;
}

#endif //RGB_MATRIX_AVR_CLKOUT_H
