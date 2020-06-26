# RGB Matrix Panel Driver using Raspberry Pi
Uses UDP protocol and a custom adapter board to drive RGB Panels.

The custom driver board eliminates flickering under high CPU load by utilizing the DPI interface to generate the output signals independent of the CPU.  This prevents heavy CPU load and interrupts from causing the panel brightness to flicker.  The DPI interfaces provides a 24-bit parallel interface with pixel clock, HSYNC, and VSYNC signals.  The 24-bit width allows for driving 4 chains of LED panels.

The UDP server and clients synchornize multiple Raspberry Pis to support both larger aggregate arrays and duplication of video feeds.  The server and clients must be synchronized to the same master clock using either chrony or ntpd.

This project references https://github.com/hzeller/rpi-rgb-led-matrix
