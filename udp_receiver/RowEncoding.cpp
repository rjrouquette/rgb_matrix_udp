//
// Created by robert on 2/25/20.
//

#include "RowEncoding.h"

RowEncoding::Encoder RowEncoding::encoder[3] = {
        RowEncoding::Hub75,
        RowEncoding::Hub75e,
        RowEncoding::Qiangli_Q3F32
};

// HUB75 4-bit row select address
unsigned RowEncoding::Hub75(unsigned pwmRows, unsigned srow, unsigned idx) {
    auto row = (srow - 1) / pwmRows;
    return (row & 0x0fu) << 3u;
}

// HUB75E 5-bit row select address
unsigned RowEncoding::Hub75e(unsigned pwmRows, unsigned srow, unsigned idx) {
    auto row = (srow - 1) / pwmRows;
    return (row & 0x1fu) << 3u;
}

/*
 * Qiangli Q3E
 * A => CLK
 * B => DATA
 * C => BLANK
 * D => EN0
 * E => EN1
 */

unsigned RowEncoding::Qiangli_Q3F32(unsigned pwmRows, unsigned srow, unsigned idx) {
    // skip dead row
    if(srow == 0)
        return 0;

    // compute active row and pwm step
    srow -= 1;
    auto row = srow / pwmRows;
    auto step = srow % pwmRows;

    unsigned code = 0;

    // shift in set bit for row block
    if((row % 8) == 0)
        code |= 0x02u;

    // perform clock transition
    if(step == 1 && idx == 1)
        code |= 0x01u;

    // chip select
    code |= (row & 0x18u);
    return code << 3u;
}
