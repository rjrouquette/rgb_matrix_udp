#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>

#include "gpio.h"

void initSysClock(void);

volatile uint8_t buffer[8];

volatile uint16_t pulseWidth = 64;
volatile uint8_t rowSelect = 0xa0u;

int main(void) {
    cli();
    initGpio();
    initSysClock();

    // wait for raspberry pi startup
    ledOn();
    for(uint8_t i = 0u; i < 16u; i++) {
        while(!isVsync()) nop1();
        while(isVsync()) nop1();
    }
    while(!isVsync()) nop1();

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
    RST.CTRL = RST_SWRST_bm;
}

// hsync leading edge
ISR(PORTE_INT0_vect, ISR_NAKED) {
    ROWSEL_PORT.OUT = rowSelect;
    PWM_TIMER.CCA = pulseWidth;
    PWM_TIMER.CNT = 0;
    asm volatile("reti");
}

// capture line config
ISR(PORTE_INT1_vect, ISR_NAKED) {
    // capture leading bytes
    buffer[0] = PORTD.IN;
    buffer[1] = PORTD.IN;
    buffer[2] = PORTD.IN;
    buffer[3] = PORTD.IN;
    buffer[4] = PORTD.IN;
    buffer[5] = PORTD.IN;
    buffer[6] = PORTD.IN;
    buffer[7] = PORTD.IN;

    // locate line header
    uint8_t i = 0;
    while(i < 4) {
        if(buffer[i++] == 0xffu) break;
    }

    // extract line config
    pulseWidth = *(uint16_t*)(buffer + i);
    rowSelect = *(uint8_t*)(buffer + i + 2);
    asm volatile("reti");
}
