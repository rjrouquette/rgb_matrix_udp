//
// Created by robert on 9/18/19.
//

#include "gpio.h"

void initGpio() {
    // configure register and output buffer mux pins
    // inverted, disable input sensing
    OE_PORT.DIRSET = OE_MASK;
    OE_PORT.PIN5CTRL = 0x47u;
    OE_PORT.PIN6CTRL = 0x47u;
    INE_PORT.DIRSET = INE_MASK;
    INE_PORT.PIN3CTRL = 0x47u;
    INE_PORT.PIN4CTRL = 0x47u;

    // configure LED pins
    // disable input sensing
    LED_PORT.DIRSET = 0x38u;
    LED_PORT.PIN3CTRL = 0x07u;
    LED_PORT.PIN4CTRL = 0x07u;
    LED_PORT.PIN5CTRL = 0x07u;

    // configure clock output pins
    // high-impedance, disable input sensing, pull down
    CLK0_PORT.DIRCLR = CLK_PIN_MASK;
    CLK0_PORT.PIN7CTRL = 0x17u;
    CLK1_PORT.DIRCLR = CLK_PIN_MASK;
    CLK1_PORT.PIN7CTRL = 0x17u;
    // leave clk output disabled for now
    PORTCFG.CLKEVOUT = 0x00u;


    // configure row select outputs
    // inverted, disable input sensing
    ADDR_PORT.DIRSET = 0x1f;
    ADDR_PORT.PIN0CTRL = 0x47u;
    ADDR_PORT.PIN1CTRL = 0x47u;
    ADDR_PORT.PIN2CTRL = 0x47u;
    ADDR_PORT.PIN3CTRL = 0x47u;
    ADDR_PORT.PIN4CTRL = 0x47u;

    // full timer period
    // single slope PWM mode
    PWM_TIMER.PER = 0xffffu;
    PWM_TIMER.CTRLB = 0x23u;

    // set PE1 as OCCB output pin
    // inverted, disable input sensing
    PWM_PORT.DIRSET = PWM_MASK;
    PWM_PORT.PIN1CTRL = 0x47u;

    // LAT output
    // disable input sensing
    LAT_PORT.DIRSET = LAT_MASK;
    LAT_PORT.PIN0CTRL = 0x07u;


    // configure vsync pins (sram chip selects)
    // high impedance, inverted, pull-ups
    VSYNC_PORT0.OUTCLR = VSYNC_MASK0;
    VSYNC_PORT0.DIRCLR = VSYNC_MASK0;
    VSYNC_PORT0.PIN3CTRL = 0x58u;
    VSYNC_PORT0.PIN4CTRL = 0x58u;
    VSYNC_PORT1.OUTCLR = VSYNC_MASK1;
    VSYNC_PORT1.DIRCLR = VSYNC_MASK1;
    VSYNC_PORT1.PIN6CTRL = 0x58u;
    VSYNC_PORT1.PIN7CTRL = 0x58u;


}
