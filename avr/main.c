#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/delay.h>
#include <string.h>
#include <stdio.h>
#include <avr/pgmspace.h>

#include <stddef.h>
#include <stdbool.h>

#define CLKOUT_PD7 0x02u
#define CLKOUT_PE7 0x03u

volatile uint8_t pwmBits = 11;
volatile uint8_t rows = 32;
volatile uint16_t rowLength = 16;
volatile uint8_t clkPin = CLKOUT_PD7;

void initSysClock(void);
void initClkOut(void);
void clockRow(uint8_t pinSelect);
void scanDisplay();
void startSRAM();
void waitForVsync();

int main(void) {
    cli();
    initSysClock();
    initClkOut();

    PORTB.DIRSET = 0x01u;

    // 1ms timer
    memset(&TCC1, 0, sizeof(TC1_t));
    TCC1.PERBUF = 19999u; // 1ms @ 20 MHz
    TCC1.CTRLA = 0x01u; // 20 MHz clock

    for(;;) {
        PORTB.OUTSET = 0x01u;
        PORTB.OUTCLR = 0x01u;
        scanDisplay();
        waitForVsync();
        TCC1.INTFLAGS |= 0x01u;
        for(;;) {
            if(TCC1.INTFLAGS & 0x01u) break;
        }
    }

    return 0;
}

void initSysClock(void) {
    OSC.PLLCTRL = OSC_PLLSRC_RC2M_gc | 10u; // 2MHz * 10 = 20MHz

    CCP = CCP_IOREG_gc;
    OSC.CTRL = OSC_PLLEN_bm | OSC_RC2MEN_bm ; // Enable PLL

    while(!(OSC.STATUS & OSC_PLLRDY_bm)); // wait for PLL ready

    CCP = CCP_IOREG_gc; //Security Signature to modify clock
    CLK.CTRL = CLK_SCLKSEL_PLL_gc; // Select PLL
    CLK.PSCTRL = 0x00u; // CPU 20 MHz
}

// init PD7 and PE7 as clock outputs
void initClkOut() {
    // set pins as outputs
    PORTD.DIRSET = 0x80;
    PORTE.DIRSET = 0x80;

    // set output as inverted, disable input sensing
    PORTD.PIN7CTRL = 0x47u;
    PORTE.PIN7CTRL = 0x47u;

    // leave clk output disabled for now
    PORTCFG.CLKEVOUT = 0x00u;
}

void clockRow16(uint8_t pinSelect) {
    PORTCFG.CLKEVOUT = pinSelect;
    asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop");
    asm volatile("nop\nnop\nnop\nnop\nnop\nnop\n");
    PORTCFG.CLKEVOUT = 0x00u;
}

void clockRawFull(uint8_t pinSelect) {
    volatile uint8_t cnt = ((rowLength >> 4u) - 1u) & 0xffu;
    PORTCFG.CLKEVOUT = pinSelect;
    do {
        // loop condtion takes 9 cycles, add 7 to make it 16
        asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop");
    }
    while(--cnt);
    asm volatile("nop\nnop\nnop\nnop\nnop\nnop\n");
    PORTCFG.CLKEVOUT = 0x00u;
}

#pragma GCC optimization_level s
void clockRow(uint8_t pinSelect) {
    if(rowLength <= 16) {
        clockRow16(pinSelect);
    } else {
        clockRawFull(pinSelect);
    }
}
#pragma GCC optimization_level reset

void scanDisplay() {
    startSRAM();
    clockRow(clkPin);
    // use row address output as counter
    for(PORTH.OUT = 0; PORTH.OUT < rows; PORTH.OUT++) {

    }
}

void startSRAM() {

}

void waitForVsync() {

}
