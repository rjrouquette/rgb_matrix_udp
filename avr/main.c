#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>

#include "gpio.h"

void initDMA(void);
void initSysClock(void);

struct __attribute__((packed)) {
    uint16_t pulseWidth;
    uint8_t rowSelect;
} lineMeta = {0,0};

int main(void) {
    cli();
    initGpio();
    initDMA();
    initSysClock();
    sei();

    // wait for raspberry pi startup
    ledOn();
    for(uint8_t i = 0u; i < 16u; i++) {
        while(!isVsync()) asm("nop");
        while(isVsync()) asm("nop");
    }
    while(!isVsync()) asm("nop");

    // startup complete
    PMIC.CTRL = 0x06u; // enable high-level interrupts
    DMA.CTRL = 0x83u; // enable DMA
    ledOff();

    // infinite loop
    for(;;) asm("nop");

    return 0;
}

void initSysClock(void) {
    // drop down to 2MHz clock before changing PLL settings
    CCP = CCP_IOREG_gc;
    CLK.CTRL = CLK_SCLKSEL_RC2M_gc; // Select 2MHz RC OSC

    OSC.XOSCCTRL = OSC_XOSCSEL_EXTCLK_gc;
    OSC.CTRL |= OSC_XOSCEN_bm;
    while(!(OSC.STATUS & OSC_XOSCRDY_bm)); // wait for external clock ready
    OSC.XOSCFAIL = 0x01u; // enable interrupt on clock failure

    CCP = CCP_IOREG_gc;
    CLK.PSCTRL = 0x00u; // no prescaling
    asm volatile("nop\nnop\nnop\nnop");

    CCP = CCP_IOREG_gc;
    CLK.CTRL = CLK_SCLKSEL_XOSC_gc; // Select external clock
}

ISR(OSC_OSCF_vect) {
    // reset xmega
    ledOff();
    RST.CTRL = RST_SWRST_bm;
}

#define DMA_ADDR0(x) ((uint8_t)(((uint16_t)x) >> 0u))
#define DMA_ADDR1(x) ((uint8_t)(((uint16_t)x) >> 8u))
#define DMA_ADDR2(x) (0x00u)

void initDMA(void) {
    // 3 bytes
    DMA.CH0.SRCADDR0 = DMA_ADDR0(&(PORTD.IN));
    DMA.CH0.SRCADDR1 = DMA_ADDR1(&(PORTD.IN));
    DMA.CH0.SRCADDR1 = DMA_ADDR2(&(PORTD.IN));
    DMA.CH0.DESTADDR0 = DMA_ADDR0(&lineMeta);
    DMA.CH0.DESTADDR1 = DMA_ADDR1(&lineMeta);
    DMA.CH0.DESTADDR2 = DMA_ADDR2(&lineMeta);
    DMA.CH0.TRFCNT = sizeof(lineMeta);
    DMA.CH0.REPCNT = 1;
    DMA.CH0.TRIGSRC = 0x02u; // event channel 1 trigger
    DMA.CH0.ADDRCTRL = 0x05u; // fixed source address, reset destination after each block
    DMA.CH0.CTRLB = 0x02u; // mid-level interrupt
    DMA.CH0.CTRLA = 0x80u; // enabled
}

ISR(DMA_CH0_vect) {
    DMA.CH0.CTRLA = 0x80u; // enabled
}

ISR(PORTE_INT0_vect) {
    PWM_TIMER.CCA = lineMeta.pulseWidth;
    ROWSEL_PORT.OUT = lineMeta.rowSelect;
    ledToggle();
}
