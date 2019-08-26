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
volatile uint16_t rowLength = 64;
volatile uint16_t pwmBase = 4u;
volatile bool doReset = false;

void initSysClock(void);
void initClkOut(void);
void initPwm(void);
void initRowSelect(void);
void initSRAM();

int main(void) {
    // initialize xmega
    cli();
    initSysClock();
    initClkOut();
    initPwm();
    initRowSelect();
    initSRAM();

    // start matrix output
    uint8_t clkPin, vsyncMask;
    uint8_t prime = 1u;
    const uint8_t rowClkCnt = ((rowLength + 15u) >> 4u) & 0xffu;

    PORTB.DIRSET = 0x03u;
    PORTB.OUTSET = 0x02u;

    // LAT output
    PORTE.DIRSET = 0x01u;
    PORTE.PIN0CTRL = 0x07u;

    // dummy pulse
    // do PWM pulse
    TCE0.CCB = 0;
    TCE0.CNT = 0;
    TCE0.INTFLAGS |= 0x20u;
    TCE0.CTRLA = 0x01u;

    for(;;) {
        // check for serial commands

        // check for reset flag
        if(doReset) break;

        PORTB.OUTSET = 0x01u;
        PORTB.OUTCLR = 0x01u;

        // TODO init SRAM

        // set clk output pin
        clkPin = prime ? CLKOUT_PD7 : CLKOUT_PE7;
        // set vsync pin mask
        vsyncMask = prime ? 0x02u : 0x04u;

        // mux pins
        if(prime) {
            PORTE.DIRCLR = 0x80u;
            PORTD.DIRSET = 0x80u;
        } else {
            PORTD.DIRCLR = 0x80u;
            PORTE.DIRSET = 0x80u;
        }

        // use row address output as counter
        for(PORTF.OUT = 0; PORTF.OUT < rows; PORTF.OUT++) {
            uint16_t pwmPulse = pwmBase;
            for(uint8_t pwmCnt = 0; pwmCnt < pwmBits; pwmCnt++) {
                // clock out pixel data
                volatile uint8_t cnt = rowClkCnt;
                do {
                    asm volatile("nop");
                    PORTCFG.CLKEVOUT = clkPin;
                    // loop condtion takes 9 cycles, add 7 to make it 16
                    asm volatile("nop\nnop\nnop\nnop");
                } while (--cnt);
                asm volatile("nop\nnop");
                PORTCFG.CLKEVOUT = 0x00u;

                // wait for pwm pulse to complete
                while (!(TCE0.INTFLAGS & 0x20u));
                TCE0.CTRLA = 0x00u;

                // pulse latch signal
                PORTE.OUTSET = 0x01u;
                asm volatile("nop\nnop");
                asm volatile("nop\nnop");
                PORTE.OUTCLR = 0x01u;

                // do PWM pulse
                TCE0.CCB = pwmPulse;
                TCE0.CNT = 0;
                TCE0.INTFLAGS |= 0x20u;
                TCE0.CTRLA = 0x01u;
                pwmPulse <<= 1u;
            }
        }

        // wait for pwm pulse to complete
        while (!(TCE0.INTFLAGS & 0x20u));
        TCE0.CTRLA = 0x00u;

        // wait for vsync
        while(!(PORTK.IN & vsyncMask));

        // flip SRAM buffer
        prime ^= 1u;
    }

    // reset xmega
    RST.CTRL = RST_SWRST_bm;
    return 0;
}

void initSysClock(void) {
    // enable external clock (9.6 MHz)
    OSC.XOSCCTRL = OSC_XOSCSEL_EXTCLK_gc;
    OSC.CTRL |= OSC_XOSCEN_bm;
    while(!(OSC.STATUS & OSC_XOSCRDY_bm)); // wait for external clock ready

    // configure PLL
    OSC.PLLCTRL = OSC_PLLSRC_XOSC_gc | 9u; // 9.6 MHz * 9 = 86.4 MHz
    //OSC.PLLCTRL = OSC_PLLSRC_XOSC_gc | 2u; // 9.6 MHz * 2 = 19.2 MHz
    CCP = CCP_IOREG_gc;
    OSC.CTRL |= OSC_PLLEN_bm; // Enable PLL

    while(!(OSC.STATUS & OSC_PLLRDY_bm)); // wait for PLL ready

    CCP = CCP_IOREG_gc;
    CLK.PSCTRL = CLK_PSADIV_4_gc; // CPU = 21.6 MHz = 86.4 / 4
    //CLK.PSCTRL = CLK_PSADIV_1_gc; // CPU = 19.2 MHz
    asm volatile("nop\nnop\nnop\nnop");

    CCP = CCP_IOREG_gc;
    CLK.CTRL = CLK_SCLKSEL_PLL_gc; // Select PLL
}

// init PD7 and PE7 as clock outputs
void initClkOut() {
    // set pins as high-impedance
    PORTD.DIRCLR = 0x80;
    PORTE.DIRCLR = 0x80;

    // set output as inverted, disable input sensing
    PORTD.PIN7CTRL = 0x07u;
    PORTE.PIN7CTRL = 0x07u;

    // leave clk output disabled for now
    PORTCFG.CLKEVOUT = 0x00u;
}

void initPwm() {
    // full timer period
    TCE0.PER = 0xffffu;
    // single slope PWM mode
    TCE0.CTRLB = 0x23u;

    // set PE1 as OCCB output pin
    PORTE.DIRSET = 0x02u;
    // inverted, input sensing disabled
    PORTE.PIN1CTRL = 0x47u;
}

void initRowSelect() {
    // row select outputs
    PORTF.DIRSET = 0x1f;

    // inverted, input sensing disabled
    PORTF.PIN0CTRL = 0x47u;
    PORTF.PIN1CTRL = 0x47u;
    PORTF.PIN2CTRL = 0x47u;
    PORTF.PIN3CTRL = 0x47u;
    PORTF.PIN4CTRL = 0x47u;
}

void initSRAM() {

}