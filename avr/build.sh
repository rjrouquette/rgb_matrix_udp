#!/bin/bash

# cleanup old files
rm rgb_matrix.elf rgb_matrix.hex

avr-gcc -Os -mmcu=atxmega16a4u -DF_CPU=2000000 -o rgb_matrix.elf main.c gpio.c
if [ $? -ne 0 ]; then exit 1; fi

avr-objcopy -j .text -j .data -O ihex rgb_matrix.elf rgb_matrix.hex
if [ $? -ne 0 ]; then exit 1; fi

# change this back to xmega64A1U for production
avrdude -v -B 1.0 -c jtag3pdi -p ATxmega16A4U -P usb -u -Uflash:w:rgb_matrix.hex
