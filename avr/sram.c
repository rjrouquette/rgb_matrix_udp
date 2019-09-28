//
// Created by robert on 9/18/19.
//

#include "sram.h"

void initSRAM() {
    // reset SRAM bus mode
    // set chip selects
    csOn0();
    csOn1();
    // set upper command bits (PH6, PH7, PC7, PB7)
    PORTH.DIRSET = 0xc0u;
    PORTC.DIRSET = 0x80u;
    PORTB.DIRSET = 0x80u;
    PORTH.OUTSET = 0xc0u;
    PORTC.OUTSET = 0x80u;
    PORTB.OUTSET = 0x80u;
    // raise lower command bits
    PORTH.DIRSET = BANK0H_MASK | BANK1H_MASK;
    PORTJ.DIRSET = BANK0J_MASK | BANK1J_MASK;
    PORTC.DIRSET = BANK1C_MASK;
    PORTB.DIRSET = BANK0B_MASK;
    PORTH.OUTSET = BANK0H_MASK | BANK1H_MASK;
    PORTJ.OUTSET = BANK0J_MASK | BANK1J_MASK;
    PORTC.OUTSET = BANK1C_MASK;
    PORTB.OUTSET = BANK0B_MASK;

    // wait for data pins to stabilize
    for(uint16_t cnt = 0; cnt < 0xffffu; cnt++) asm("nop\nnop\nnop\nnop");

    // clock data 8 times to guarantee bus mode reset
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    // clear chip selects
    csOff0();
    csOff1();

    // set SO bits as high impedance
    PORTH.DIRCLR = 0x07u; // PH0 - PH2
    PORTJ.DIRCLR = 0xe0u; // PJ5 - PJ7
    PORTC.DIRCLR = 0x07u; // PC0 - PC2
    PORTB.DIRCLR = 0x38u; // PB3 - PB5

    // set SRAM bus mode to QPI
    // set chip selects
    csOn0();
    csOn1();
    // clear lower command bits
    PORTH.OUTCLR = BANK0H_MASK | BANK1H_MASK;
    PORTJ.OUTCLR = BANK0J_MASK | BANK1J_MASK;
    PORTC.OUTCLR = BANK1C_MASK;
    PORTB.OUTCLR = BANK0B_MASK;
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    // set lower command bits
    PORTH.OUTSET = BANK0H_MASK | BANK1H_MASK;
    PORTJ.OUTSET = BANK0J_MASK | BANK1J_MASK;
    PORTC.OUTSET = BANK1C_MASK;
    PORTB.OUTSET = BANK0B_MASK;
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    // clear lower command bits
    PORTH.OUTCLR = BANK0H_MASK | BANK1H_MASK;
    PORTJ.OUTCLR = BANK0J_MASK | BANK1J_MASK;
    PORTC.OUTCLR = BANK1C_MASK;
    PORTB.OUTCLR = BANK0B_MASK;
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    pulseClk0(); pulseClk1();
    // clear chip selects
    csOff0();
    csOff1();

    // clear upper command bits
    PORTH.OUTCLR = 0xc0u;
    PORTC.OUTCLR = 0x80u;
    PORTB.OUTCLR = 0x80u;

    // wait for data pins to stabilize
    for(uint16_t cnt = 0; cnt < 0xffffu; cnt++) asm("nop\nnop\nnop\nnop");
}
