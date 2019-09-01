#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>

#include <stdbool.h>

#define CFG_BYTES 12u
#define CFG_MAGIC_0 0xdau
#define CFG_MAGIC_1 0x21u

#define CLKOUT_PD7 0x02u
#define CLKOUT_PE7 0x03u

#define VSYNC_MASK0 0x02u
#define VSYNC_MASK1 0x04u

#define CLK_PIN_MASK PIN7_bm

volatile uint8_t CFG_PLLCTRL = OSC_PLLSRC_RC2M_gc | 10u; // default is 20 MHz using internal osc
volatile uint8_t CFG_PSCTRL = CLK_PSADIV_1_gc; // default is no prescaling
volatile uint8_t pwmBits = 11;
volatile uint8_t rows = 32;
volatile uint16_t rowLength = 64;
volatile uint16_t pwmBase = 4u;
volatile bool doReset = false;

void doConfig();

void initSysClock(void);
void initClkOut(void);
void initMatrixOutputs(void);
void initSRAM();

inline void doPulse(uint16_t width);

inline void readBank0();
inline void readBank1();

inline void clkBank0();
inline void clkBank1();

inline void startRxBank0();
inline void stopRxBank0();
inline void startRxBank1();
inline void stopRxBank1();

int main(void) {
    // wait for configuration
    initSRAM();
    doConfig();

    // initialize xmega
    cli();
    initSysClock();
    initClkOut();
    initMatrixOutputs();

    // start matrix output
    uint8_t clkPin, vsyncMask;
    uint8_t prime = 1u;
    const uint8_t rowClkCnt = ((rowLength + 15u) >> 4u) & 0xffu;

    // dummy pulse to set pwm state
    doPulse(0);

    for(;;) {
        // check for reset flag
        if(doReset) break;

        // mux pins
        if(prime) {
            startRxBank1();
            stopRxBank0();
            readBank0();
        } else {
            startRxBank0();
            stopRxBank1();
            readBank1();
        }

        // set clk output pin
        clkPin = prime ? CLKOUT_PD7 : CLKOUT_PE7;
        // set vsync pin mask
        vsyncMask = prime ? VSYNC_MASK0 : VSYNC_MASK1;

        // advance over configuration block (8 clock cycles)
        PORTCFG.CLKEVOUT = clkPin;
        asm volatile("nop\nnop\nnop\nnop");
        asm volatile("nop\nnop");
        PORTCFG.CLKEVOUT = 0x00u;

        // use row address output as counter
        for(PORTF.OUT = 0; PORTF.OUT < rows; PORTF.OUT++) {
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

                // wait for pwm pulse to complete
                while (!(TCE0.INTFLAGS & 0x20u));
                TCE0.CTRLA = 0x00u;

                // pulse latch signal
                PORTE.OUTSET = 0x01u;
                asm volatile("nop\nnop");
                asm volatile("nop\nnop");
                PORTE.OUTCLR = 0x01u;

                // do PWM pulse
                doPulse(pwmPulse);
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

// init PD7 and PE7 as clock outputs
void initClkOut() {
    // set pins as high-impedance
    PORTD.DIRCLR = 0x80;
    PORTE.DIRCLR = 0x80;

    // disable input sensing
    PORTD.PIN7CTRL = 0x07u;
    PORTE.PIN7CTRL = 0x07u;

    // leave clk output disabled for now
    PORTCFG.CLKEVOUT = 0x00u;
}

void initMatrixOutputs() {
    // row select outputs
    PORTF.DIRSET = 0x1f;

    // inverted, input sensing disabled
    PORTF.PIN0CTRL = 0x47u;
    PORTF.PIN1CTRL = 0x47u;
    PORTF.PIN2CTRL = 0x47u;
    PORTF.PIN3CTRL = 0x47u;
    PORTF.PIN4CTRL = 0x47u;

    // full timer period
    TCE0.PER = 0xffffu;
    // single slope PWM mode
    TCE0.CTRLB = 0x23u;

    // set PE1 as OCCB output pin
    PORTE.DIRSET = 0x02u;
    // inverted, input sensing disabled
    PORTE.PIN1CTRL = 0x47u;

    // LAT output
    PORTE.DIRSET = 0x01u;
    PORTE.PIN0CTRL = 0x07u;
}

void initSRAM() {

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
        // wait for vsync
        while (!(PORTK.IN & VSYNC_MASK0));
        // accept frame data on bank 0
        startRxBank0();

        // wait for vsync
        while (!(PORTK.IN & VSYNC_MASK0));
        // stop frame data on bank 0
        stopRxBank0();

        // clear config bytes
        for(uint8_t i = 0; i < CFG_BYTES; i++)
            config[i] = 0;

        // prepare to read from bank 0
        readBank0();

        // extract config bytes
        for(uint8_t cnt = 0; cnt < 8; cnt++) {
            // shift previous bits
            for(uint8_t i = 0; i < CFG_BYTES; i++)
                config[i] <<= 1u;

            // bank 0 lower half (PH0-PH2, PJ2-PJ4)
            config[0] |= (PORTH.IN & PIN0_bm) ? 1u : 0u;
            config[1] |= (PORTH.IN & PIN1_bm) ? 1u : 0u;
            config[2] |= (PORTH.IN & PIN2_bm) ? 1u : 0u;
            config[3] |= (PORTJ.IN & PIN2_bm) ? 1u : 0u;
            config[4] |= (PORTJ.IN & PIN3_bm) ? 1u : 0u;
            config[5] |= (PORTJ.IN & PIN4_bm) ? 1u : 0u;

            // bank 0 upper half (PC0-PC5)
            config[6] |= (PORTC.IN & PIN0_bm) ? 1u : 0u;
            config[7] |= (PORTC.IN & PIN1_bm) ? 1u : 0u;
            config[8] |= (PORTC.IN & PIN2_bm) ? 1u : 0u;
            config[9] |= (PORTC.IN & PIN3_bm) ? 1u : 0u;
            config[10] |= (PORTC.IN & PIN4_bm) ? 1u : 0u;
            config[11] |= (PORTC.IN & PIN5_bm) ? 1u : 0u;

            clkBank0();
        }

        // verify magic bytes
        if(config[0] != CFG_MAGIC_0) continue;
        if(config[1] != CFG_MAGIC_1) continue;

        // https://lb9mg.no/2016/09/03/using-xmega-hardware-crc-generator-for-crc-16-ccitt/
        // validate CRC-16 CCITT checksum using hardware module

        // Set initial checksum value
        CRC.CTRL |= CRC_RESET_RESET0_gc;
        CRC.CHECKSUM0 = 0x0F;
        CRC.CHECKSUM1 = 0x1D;
        CRC.CHECKSUM2 = 0xFF;
        CRC.CHECKSUM3 = 0xFF;

        //source is IO
        CRC.CTRL &= ~CRC_SOURCE_gm;
        CRC.CTRL |= CRC_SOURCE_IO_gc;

        // Write data to DATAIN register (include magic bytes)
        for(uint8_t i = 0; i < CFG_BYTES - 2; i++)
            CRC.DATAIN = config[i];

        // Signal CRC complete
        CRC.STATUS |= CRC_BUSY_bm;

        //busy wait until the module is ready
        while (CRC.STATUS & CRC_BUSY_bm) {}

        CRC.CTRL &= ~CRC_SOURCE_gm; //disable CRC module

        if(config[CFG_BYTES - 2] == CRC.CHECKSUM0) continue;
        if(config[CFG_BYTES - 1] != CRC.CHECKSUM1) break;
    }

    // extract config fields (2 bytes currently unused)
    CFG_PLLCTRL = config[2];
    CFG_PSCTRL = config[3];
    pwmBits = config[4];
    rows = config[5];
    rowLength = config[6];
    pwmBase = config[7];
}

inline void doPulse(uint16_t width) {
    TCE0.CCB = width;
    TCE0.CNT = 0;
    TCE0.INTFLAGS |= 0x20u;
    TCE0.CTRLA = 0x01u;
}

inline void readBank0() {
    PORTD.DIRSET = CLK_PIN_MASK;

    // set data pins to outputs
    // bank 0 lower half (PH0-PH2, PJ2-PJ4)
    PORTH.DIRSET = 0x07u;
    PORTJ.DIRSET = 0x0eu;
    // bank 0 upper half (PC0-PC5)
    PORTC.DIRSET = 0x3fu;

    // clock in command
    PORTH.OUTCLR = 0x07u;
    PORTJ.OUTCLR = 0x0eu;
    PORTC.OUTCLR = 0x3fu;
    clkBank0();
    PORTH.OUTSET = 0x07u;
    PORTJ.OUTSET = 0x0eu;
    PORTC.OUTSET = 0x3fu;
    clkBank0();

    // clock in zero as starting address
    PORTH.OUTCLR = 0x07u;
    PORTJ.OUTCLR = 0x0eu;
    PORTC.OUTCLR = 0x3fu;
    clkBank0();
    clkBank0();
    clkBank0();
    clkBank0();

    // set data pins back to inputs
    PORTH.DIRCLR = 0x07u;
    PORTJ.DIRCLR = 0x0eu;
    PORTC.DIRCLR = 0x3fu;

    // dummy byte
    clkBank0();
    clkBank0();
}

inline void readBank1() {
    PORTE.DIRSET = CLK_PIN_MASK;

    // dummy byte
    clkBank0();
    clkBank0();
}

inline void clkBank0() {
    PORTD.OUTSET = CLK_PIN_MASK;
    PORTD.OUTCLR = CLK_PIN_MASK;
}

inline void clkBank1() {
    PORTE.OUTSET = CLK_PIN_MASK;
    PORTE.OUTCLR = CLK_PIN_MASK;
}

inline void startRxBank0() {
    PORTD.DIRCLR = CLK_PIN_MASK;

}

inline void stopRxBank0() {

}

inline void startRxBank1() {
    PORTE.DIRCLR = CLK_PIN_MASK;

}

inline void stopRxBank1() {

}
