//
// Created by robert on 9/18/19.
//

#ifndef RGB_MATRIX_AVR_MATRIX_H
#define RGB_MATRIX_AVR_MATRIX_H

#define ROWSEL_PORT PORTA

#define LAT_PORT PORTC
#define LAT_MASK 0x80u

#define PWM_PORT PORTC
#define PWM_MASK 0x01u
#define PWM_TIMER TCC0

#endif //RGB_MATRIX_AVR_MATRIX_H
