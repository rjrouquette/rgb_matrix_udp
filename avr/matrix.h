//
// Created by robert on 9/18/19.
//

#ifndef RGB_MATRIX_AVR_MATRIX_H
#define RGB_MATRIX_AVR_MATRIX_H

#define ADDR_PORT PORTF

#define LAT_PORT PORTE
#define LAT_MASK 0x01u

inline void pulseLatch() {
    // set latch signal
    LAT_PORT.OUTSET = LAT_MASK;

    // wait 4 clock cycles
    asm volatile("nop\nnop");
    asm volatile("nop\nnop");

    // clear latch signal
    LAT_PORT.OUTCLR = LAT_MASK;
}

#define PWM_PORT PORTE
#define PWM_MASK 0x02u
#define PWM_TIMER TCE0

inline void doPwmPulse(uint16_t width) {
    // set OCCB value
    PWM_TIMER.CCB = width;
    // reset count
    PWM_TIMER.CNT = 0;
    // clear OCCB flag
    PWM_TIMER.INTFLAGS |= 0x20u;
    // start timer
    PWM_TIMER.CTRLA = 0x01u;
}

inline void waitPwm() {
    // wait for pwm pulse to complete
    while (!(PWM_TIMER.INTFLAGS & 0x20u)) asm("");
    // stop timer
    PWM_TIMER.CTRLA = 0x00u;
}

#endif //RGB_MATRIX_AVR_MATRIX_H
