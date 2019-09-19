#!/bin/bash

# cleanup old files
rm rgb_matrix.elf rgb_matrix.hex

avr-gcc -Os -mmcu=atxmega64a1u -DF_CPU=20000000 -o rgb_matrix.elf main.c gpio.c sram.c
if [ $? -ne 0 ]; then exit 1; fi

avr-objcopy -j .text -j .data -O ihex rgb_matrix.elf rgb_matrix.hex
if [ $? -ne 0 ]; then exit 1; fi

# change this back to xmega64A1U for production
avrdude -v -B 1.0 -c jtag3pdi -p ATxmega64A1U -P usb -u -Uflash:w:rgb_matrix.hex
