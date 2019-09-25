#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdbool.h>

#include "gpio.h"

#define CFG_BYTES 12u
#define CFG_MAGIC_0 0xdau
#define CFG_MAGIC_1 0x21u

volatile uint8_t CFG_PLLCTRL = OSC_PLLSRC_RC2M_gc | 10u; // default is 20 MHz using internal osc
volatile uint8_t CFG_PSCTRL = CLK_PSADIV_1_gc; // default is no prescaling
volatile uint8_t pwmBits = 11;
volatile uint8_t rows = 32;
volatile uint16_t rowLength = 64;
volatile uint16_t pwmBase = 4u;
volatile bool doReset = false;

void doConfig();
void initSysClock(void);

int main(void) {
    // disable all interrupts
    // ISR timing is too variable
    cli();

    // init gpio and external sram
    initGpio();
    initSRAM();
    ledOn0();

    // wait for configuration
    doConfig();
    ledOn1();

    // reconfigure system clock
    initSysClock();
    ledOn2();

    // start matrix output
    uint8_t clkPin;
    uint8_t prime = 1u;
    const uint8_t rowClkCnt = ((rowLength + 15u) >> 4u) & 0xffu;

    // dummy pulse to set pwm state
    doPwmPulse(0);

    // load first frame into bank 0
    waitVsync();
    disableClk0();
    enableInput0();
    waitVsync();

    while(!doReset) {
        // mux pins
        if(prime) {
            // set bank 1 as input
            disableClk1();
            enableInput1();

            // set bank 0 as output
            disableInput0();
            enableClk0();
            setVsync0();
            readBank0();
            enableOutput0();
        } else {
            // set bank 0 as input
            disableClk0();
            enableInput0();

            // set bank 1 as output
            disableInput1();
            enableClk1();
            setVsync1();
            readBank1();
            enableOutput1();
        }

        // set clk output pin
        clkPin = prime ? CLKOUT0 : CLKOUT1;

        // skip over configuration block (8 clock cycles)
        PORTCFG.CLKEVOUT = clkPin;
        asm volatile("nop\nnop\nnop\nnop");
        asm volatile("nop\nnop");
        PORTCFG.CLKEVOUT = 0x00u;

        // use row address output as counter
        for(ADDR_PORT.OUT = 0; ADDR_PORT.OUT < rows; ADDR_PORT.OUT++) {
            uint16_t pwmPulse = pwmBase;
            for(uint8_t pwmCnt = 0; pwmCnt < pwmBits; pwmCnt++) {
                // clock out pixel data, timing is empirically derived
                volatile uint8_t clkRem = rowClkCnt;
                do {
                    asm volatile("nop");
                    PORTCFG.CLKEVOUT = clkPin;
                    asm volatile("nop\nnop\nnop\nnop");
                } while (--clkRem);
                asm volatile("nop\nnop");
                PORTCFG.CLKEVOUT = 0x00u;

                waitPwm();
                pulseLatch();

                // do PWM pulse
                doPwmPulse(pwmPulse);
                pwmPulse <<= 1u;
            }
        }
        waitPwm();

        // disable panel output
        disableOutput();
        clearVsync();

        // flip SRAM buffer on vsync
        waitVsync();
        prime ^= 1u;
    }

    // reset xmega
    RST.CTRL = RST_SWRST_bm;
    return 0;
}

void initSysClock(void) {
    // drop down to 2MHz clock before changing PLL settings
    CCP = CCP_IOREG_gc;
    CLK.CTRL = CLK_SCLKSEL_RC2M_gc; // Select 2MHz RC OSC

    OSC.XOSCCTRL = OSC_XOSCSEL_EXTCLK_gc;
    OSC.CTRL |= OSC_XOSCEN_bm;
    while(!(OSC.STATUS & OSC_XOSCRDY_bm)); // wait for external clock ready

    // configure PLL
    OSC.PLLCTRL = CFG_PLLCTRL;
    CCP = CCP_IOREG_gc;
    OSC.CTRL |= OSC_PLLEN_bm; // Enable PLL

    while(!(OSC.STATUS & OSC_PLLRDY_bm)); // wait for PLL ready

    CCP = CCP_IOREG_gc;
    CLK.PSCTRL = CFG_PSCTRL;
    asm volatile("nop\nnop\nnop\nnop");

    CCP = CCP_IOREG_gc;
    CLK.CTRL = CLK_SCLKSEL_PLL_gc; // Select PLL
}

void doConfig() {
    // configure PLL as system clk at 20 MHz
    OSC.PLLCTRL = OSC_PLLSRC_RC2M_gc | 10u; // 20 MHz
    CCP = CCP_IOREG_gc;
    OSC.CTRL |= OSC_PLLEN_bm; // Enable PLL

    while(!(OSC.STATUS & OSC_PLLRDY_bm)); // wait for PLL ready

    CCP = CCP_IOREG_gc;
    CLK.PSCTRL = CLK_PSADIV_1_gc; // CPU = 20 MHz
    asm volatile("nop\nnop\nnop\nnop");

    CCP = CCP_IOREG_gc;
    CLK.CTRL = CLK_SCLKSEL_PLL_gc; // Select PLL

    volatile uint8_t config[CFG_BYTES];

    // wait for valid config
    for(;;) {
        clearVsync();
        waitVsyncOff();
        waitVsync();

        // accept frame data on bank 1
        disableClk1();
        enableInput1();

        waitVsyncOff();
        waitVsync();
        ledOn1();

        // stop frame data on bank 1
        disableInput1();
        enableClk1();
        setVsync1();
        readBank1();

        // clear config bytes
        for(uint8_t i = 0; i < CFG_BYTES; i++)
            config[i] = 0;

        // extract config bytes
        for(uint8_t cnt = 0; cnt < 8; cnt++) {
            // shift previous bits
            for(uint8_t i = 0; i < CFG_BYTES; i++)
                config[i] >>= 1u;

            // bank 1 lower half (PH0-PH2, PJ2-PJ4)
            config[0] |= (PORTH.IN & 0x01u) ? 0x80u : 0x00u;
            config[1] |= (PORTH.IN & 0x02u) ? 0x80u : 0x00u;
            config[2] |= (PORTH.IN & 0x04u) ? 0x80u : 0x00u;
            config[3] |= (PORTJ.IN & 0x04u) ? 0x80u : 0x00u;
            config[4] |= (PORTJ.IN & 0x08u) ? 0x80u : 0x00u;
            config[5] |= (PORTJ.IN & 0x10u) ? 0x80u : 0x00u;

            // bank 1 upper half (PC0-PC5)
            config[6] |= (PORTC.IN & 0x01u) ? 0x80u : 0x00u;
            config[7] |= (PORTC.IN & 0x02u) ? 0x80u : 0x00u;
            config[8] |= (PORTC.IN & 0x04u) ? 0x80u : 0x00u;
            config[9] |= (PORTC.IN & 0x08u) ? 0x80u : 0x00u;
            config[10] |= (PORTC.IN & 0x10u) ? 0x80u : 0x00u;
            config[11] |= (PORTC.IN & 0x20u) ? 0x80u : 0x00u;

            pulseClk1();
        }
        ledOff1();

        // verify magic bytes
        if(config[0] != CFG_MAGIC_0) continue;
        if(config[1] != CFG_MAGIC_1) continue;

        // compute CRC-16 CCITT checksum
        CRC.CTRL |= CRC_RESET_RESET0_gc;
        asm("nop");
        CRC.CTRL = CRC_SOURCE_IO_gc;

        for(uint8_t i = 0; i < CFG_BYTES - 2; i++)
            CRC.DATAIN = config[i];

        CRC.STATUS = CRC_BUSY_bm;
        while(CRC.STATUS & CRC_BUSY_bm);

        bool ok = true;
        if(config[CFG_BYTES - 2] != CRC.CHECKSUM0) ok = false;
        if(config[CFG_BYTES - 1] != CRC.CHECKSUM1) ok = false;
        CRC.CTRL = CRC_SOURCE_DISABLE_gc;

        if(ok) break;
    }
    clearVsync();

    // extract config fields (2 bytes currently unused)
    CFG_PLLCTRL = config[2];
    CFG_PSCTRL = config[3];
    pwmBits = config[4];
    rows = config[5];
    rowLength = config[6];
    pwmBase = config[7];
}
