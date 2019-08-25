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
volatile uint16_t rowLength = 320;
volatile uint8_t rowClkCnt = 0;
volatile uint16_t pwmBase = 3u;
volatile bool doReset = false;

void initSysClock(void);
void initClkOut(void);
void initPwm(void);
void initSRAM();

int main(void) {
    uint8_t clkPin, vsyncMask;
    bool waitForPulse;
    bool prime = true;

    cli();
    initSysClock();
    initClkOut();
    initPwm();
    initSRAM();

    PORTH.DIRSET = 0x1f;
    PORTB.DIRSET = 0x01u;

    // LAT output
    PORTE.DIRSET = 0x01u;

    rowClkCnt = ((rowLength + 15u) >> 4u) & 0xffu;

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

        waitForPulse = false;
        // use row address output as counter
        for(uint8_t row = 0; row < rows; row++) {
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

                if (waitForPulse) {
                    // wait for pwm pulse to complete
                    while (!(TCC0.INTFLAGS & 0x20u));
                    TCC0.CTRLA = 0x00u;
                }

                // set row select
                PORTH.OUT = row;

                // pulse latch signal
                PORTE.OUTSET = 0x01u;
                asm volatile("nop\nnop");
                asm volatile("nop\nnop");
                PORTE.OUTCLR = 0x01u;

                // do PWM pulse
                TCC0.CCB = pwmPulse;
                TCC0.CNT = 0;
                TCC0.INTFLAGS |= 0x20u;
                TCC0.CTRLA = 0x01u;
                waitForPulse = true;
                pwmPulse <<= 1u;
            }
        }

        // wait for last pwm pulse
        while (!(TCC0.INTFLAGS & 0x20u));
        TCC0.CTRLA = 0x00u;

        // wait for vsync
        while(!(PORTK.IN & vsyncMask));

        // flip SRAM buffer
        prime = !prime;
    }

    // reset xmega
    RST.CTRL = RST_SWRST_bm;
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

void initPwm() {
    // full timer period
    TCC0.PER = 0xffffu;
    // single slope PWM mode
    TCC0.CTRLB = 0x24u;

    // set PE1 as OCCB output pin
    PORTE.DIRSET = 0x02u;
    // inverted, input sensing disabled
    PORTE.PIN1CTRL = 0x47u;
}

void initSRAM() {

}