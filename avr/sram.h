//
// Created by robert on 9/18/19.
//

#ifndef RGB_MATRIX_AVR_SRAM_H
#define RGB_MATRIX_AVR_SRAM_H

#include <avr/io.h>
#include "clkout.h"

#define VSYNC_PORT0 PORTK
#define VSYNC_PORT1 PORTA

#define VSYNC_MASK0 0x06u
#define VSYNC0_MASK 0x02u
#define VSYNC1_MASK 0x04u

inline void setVsync0() {
    VSYNC_PORT0.OUTSET = VSYNC0_MASK;
    VSYNC_PORT0.DIRSET = VSYNC0_MASK;
}

inline void clearVsync0() {
    VSYNC_PORT0.OUTCLR = VSYNC0_MASK;
    VSYNC_PORT0.DIRCLR = VSYNC0_MASK;
}

inline void setVsync1() {
    VSYNC_PORT0.OUTSET = VSYNC1_MASK;
    VSYNC_PORT0.DIRSET = VSYNC1_MASK;
}

inline void clearVsync1() {
    VSYNC_PORT0.OUTCLR = VSYNC1_MASK;
    VSYNC_PORT0.DIRCLR = VSYNC1_MASK;
}

inline void clearVsync() {
    clearVsync0();
    clearVsync1();
}

inline void waitVsync() {
    while (VSYNC_PORT0.IN & VSYNC_MASK0);
}

// bank 0 - PH3-PH5, PJ5-PJ7, PB0-PB5 (underside)
#define BANK0H_MASK 0x68u
#define BANK0J_MASK 0xe0u
#define BANK0B_MASK 0x3fu

inline void readBank0() {
    // set data pins as outputs
    PORTH.DIRSET = BANK0H_MASK;
    PORTJ.DIRSET = BANK0J_MASK;
    PORTB.DIRSET = BANK0B_MASK;

    // clock in READ command
    PORTH.OUTCLR = BANK0H_MASK;
    PORTJ.OUTCLR = BANK0J_MASK;
    PORTC.OUTCLR = BANK0B_MASK;
    pulseClk0();
    PORTH.OUTSET = BANK0H_MASK;
    PORTJ.OUTSET = BANK0J_MASK;
    PORTB.OUTSET = BANK0B_MASK;
    pulseClk0();

    // clock in zero as starting address
    PORTH.OUTCLR = BANK0H_MASK;
    PORTJ.OUTCLR = BANK0J_MASK;
    PORTB.OUTCLR = BANK0B_MASK;
    pulseClk0();
    pulseClk0();
    pulseClk0();
    pulseClk0();

    // set data pins as inputs
    PORTH.DIRCLR = BANK0H_MASK;
    PORTJ.DIRCLR = BANK0J_MASK;
    PORTB.DIRCLR = BANK0B_MASK;

    // dummy byte
    pulseClk0();
    pulseClk0();
}

// bank 1 - PH0-PH2, PJ2-PJ4, PC0-PC5 (topside)
#define BANK1H_MASK 0x07u
#define BANK1J_MASK 0x1cu
#define BANK1C_MASK 0x3fu

inline void readBank1() {
    // set data pins as outputs
    PORTH.DIRSET = BANK1H_MASK;
    PORTJ.DIRSET = BANK1J_MASK;
    PORTC.DIRSET = BANK1C_MASK;

    // clock in READ command
    PORTH.OUTCLR = BANK1H_MASK;
    PORTJ.OUTCLR = BANK1J_MASK;
    PORTC.OUTCLR = BANK1C_MASK;
    pulseClk1();
    PORTH.OUTSET = BANK1H_MASK;
    PORTJ.OUTSET = BANK1J_MASK;
    PORTC.OUTSET = BANK1C_MASK;
    pulseClk1();

    // clock in zero as starting address
    PORTH.OUTCLR = BANK1H_MASK;
    PORTJ.OUTCLR = BANK1J_MASK;
    PORTC.OUTCLR = BANK1C_MASK;
    pulseClk1();
    pulseClk1();
    pulseClk1();
    pulseClk1();

    // set data pins as inputs
    PORTH.DIRCLR = BANK1H_MASK;
    PORTJ.DIRCLR = BANK1J_MASK;
    PORTC.DIRCLR = BANK1C_MASK;

    // dummy byte
    pulseClk1();
    pulseClk1();
}

void initSRAM();

#endif //RGB_MATRIX_AVR_SRAM_H
