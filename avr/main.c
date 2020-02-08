#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>

#include "gpio.h"

void initSysClock(void);

int main(void) {
    cli();
    initGpio();
    ledOn();
    initSysClock();

    // startup complete
    PMIC.CTRL = 0x04u; // enable high-level interrupts
    ledOff();

    // infinite loop
    sei();
    asm(
        "rjloop:\n"
        "rjmp rjloop"
    );

    return 0;
}

void initSysClock(void) {
    // drop down to 2MHz clock before changing PLL settings
    CCP = CCP_IOREG_gc;
    CLK.CTRL = CLK_SCLKSEL_RC2M_gc; // Select 2MHz RC OSC
    nop4();

    OSC.XOSCCTRL = OSC_XOSCSEL_EXTCLK_gc;
    OSC.CTRL |= OSC_XOSCEN_bm;
    while(!(OSC.STATUS & OSC_XOSCRDY_bm)); // wait for external clock ready

    CCP = CCP_IOREG_gc;
    OSC.XOSCFAIL = 0x01u; // enable interrupt on clock failure
    nop4();

    CCP = CCP_IOREG_gc;
    CLK.PSCTRL = 0x00u; // no prescaling
    nop4();

    CCP = CCP_IOREG_gc;
    CLK.CTRL = CLK_SCLKSEL_XOSC_gc; // Select external clock
    nop4();
}

ISR(OSC_OSCF_vect) {
    // reset xmega
    ledOff();
    cli();
    CCP = 0xD8;
    RST.CTRL = RST_SWRST_bm;
}

// hsync leading edge
ISR(PORTE_INT0_vect, ISR_NAKED) {
    uint8_t buffer[4];
    buffer[0] = META_PORT.IN;
    buffer[1] = META_PORT.IN;
    buffer[2] = META_PORT.IN;
    buffer[3] = META_PORT.IN;
    ledToggle();

    ROWSEL_PORT.OUT = buffer[0];
    ROWSEL_PORT.OUT = buffer[1];
    PWM_TIMER.CCAL = buffer[2];
    PWM_TIMER.CCAH = buffer[3];
    PWM_TIMER.CNTL = 0;
    PWM_TIMER.CNTH = 0;

    asm volatile("reti");
}
