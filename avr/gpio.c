//
// Created by robert on 9/18/19.
//

#include "gpio.h"

void initGpio() {
    // clk input is inverted
    PORTR.DIRCLR = 0x02u;
    PORTR.PIN2CTRL = 0x40u;

    // configure LED pin (PC1)
    // disable input sensing
    LED_PORT.DIRSET = LED_MASK;
    LED_PORT.PIN2CTRL = 0x07u;

    // configure vsync input
    VSYNC_PORT.DIRCLR = VSYNC_MASK;

    // configure hsync input (PE1, PE2)
    HSYNC_PORT.DIRCLR = 0x06u;
    // falling-edge interrupt end event
    HSYNC_PORT.PIN2CTRL = 0x01u; // falling edge
    //HSYNC_PORT.INT0MASK = 0x04u; // interrupt on pin 2
    EVSYS.CH0MUX = 0x72u; // event channel 0
    // rising edge interrupt
    //HSYNC_PORT.PIN1CTRL = 0x02u; // rising edge
    //HSYNC_PORT.INT1MASK = 0x02u; // interrupt on pin 1
    // enable both port interrupts
    //HSYNC_PORT.INTCTRL = 0x0fu; // port interrupt 0+1, high level

    // configure row select outputs
    // disable input sensing
    ROWSEL_PORT.DIRSET = 0xf8;
    ROWSEL_PORT.PIN0CTRL = 0x07u;
    ROWSEL_PORT.PIN1CTRL = 0x07u;
    ROWSEL_PORT.PIN2CTRL = 0x07u;
    ROWSEL_PORT.PIN3CTRL = 0x07u;
    ROWSEL_PORT.PIN4CTRL = 0x07u;
    ROWSEL_PORT.PIN5CTRL = 0x07u;
    ROWSEL_PORT.PIN6CTRL = 0x07u;
    ROWSEL_PORT.PIN7CTRL = 0x07u;

    // init pwm timer
    PWM_TIMER.PER = 0xffffu; // maximum period
    PWM_TIMER.CCA = 0;       // start with zero width pulse
    PWM_TIMER.CTRLA = 0x01u; // no clock divisor
    PWM_TIMER.CTRLB = 0x13u; // CCA, single slope pwm
    //PWM_TIMER.CTRLD = 0x89u; // restart on event channel 1

    // PWM output (PC0)
    // inverted, disable input sensing
    PWM_PORT.DIRSET = PWM_MASK;
    PWM_PORT.PIN0CTRL = 0x47u;

    // LAT output (PC7)
    // disable input sensing
    // event channel 0 as output value
    LAT_PORT.DIRSET = LAT_MASK;
    LAT_PORT.PIN7CTRL = 0x07u;
    PORTCFG.CLKEVOUT = 0x10u;
    PORTCFG.EVOUTSEL = 0x00u;

    // line meta data input
    META_PORT.DIRCLR = 0xffu;
}
