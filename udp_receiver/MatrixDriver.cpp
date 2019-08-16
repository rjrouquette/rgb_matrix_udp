//
// Created by robert on 8/15/19.
//

#include "MatrixDriver.h"

#include <cstring>
#include <cstdlib>
#include <cmath>

enum class gpio_pin : uint8_t {
    r0 = 4,
    g0 = 17,
    b0 = 27,
    r1 = 22,
    g1 = 5,
    b1 = 6,
    r2 = 13,
    g2 = 19,
    b2 = 26,
    r3 = 21,
    g3 = 20,
    b3 = 16,
    ctr0 = 12,
    ctr1 = 25,
    clk = 24,
    write = 23,
    ready = 18,
};

inline uint32_t gpio_mask(gpio_pin pin) noexcept {
    return 1u << static_cast<uint8_t>(pin);
}

const uint32_t maskControlPins = gpio_mask(gpio_pin::ctr0) |
                                 gpio_mask(gpio_pin::ctr1) |
                                 gpio_mask(gpio_pin::clk);

MatrixDriver::MatrixDriver(int pixelsPerRow, int rowsPerScan, int pwmBits) :
frameBuffer{}, threadGpio{}
{
    if(pixelsPerRow < 1) abort();
    if(rowsPerScan < 1) abort();
    if(rowsPerScan > 32) abort();
    if(pwmBits < 1) abort();
    if(pwmBits > 11) abort();

    this->pixelsPerRow = pixelsPerRow;
    this->rowsPerScan = rowsPerScan;
    this->pwmBits = pwmBits;

    brightness = 100;

    sizeFrameBuffer = pwmBits * pixelsPerRow * rowsPerScan * 2 + 4;
    frameRaw = new uint32_t[sizeFrameBuffer * 2];
    bzero(frameRaw, sizeFrameBuffer * 2 * sizeof(uint32_t));

    frameBuffer[0] = frameRaw;
    frameBuffer[1] = frameRaw + sizeFrameBuffer;

    initFrameBuffer(frameBuffer[0]);
    initFrameBuffer(frameBuffer[1]);

    nextBuffer = 0;
    pwmMapping = pwmMappingCie1931;
}

MatrixDriver::~MatrixDriver() {
    delete frameRaw;
}

void MatrixDriver::initFrameBuffer(uint32_t *fb) {
    uint32_t stepSize;

    // set write enable bits
    for(uint32_t i = 0; i < sizeFrameBuffer; i++) {
        fb[i] |= gpio_mask(gpio_pin::write);
    }

    // set clk bits
    for(uint32_t i = 1; i < sizeFrameBuffer; i += 2) {
        fb[i] |= gpio_mask(gpio_pin::clk);
    }

    // set pwm stop bits
    for(uint32_t i = pixelsPerRow - 1; i < sizeFrameBuffer; i += pixelsPerRow) {
        fb[i] |= gpio_mask(gpio_pin::ctr0);
    }

    // set end-of-row advance bits
    stepSize = pixelsPerRow * pwmBits;
    for(uint32_t i = stepSize - 1; i < sizeFrameBuffer; i += stepSize) {
        fb[i] |= gpio_mask(gpio_pin::ctr1);
        fb[i] &= ~gpio_mask(gpio_pin::ctr1);
    }

    // set end-of-frame bits
    stepSize = pixelsPerRow * pwmBits * rowsPerScan;
    for(uint32_t i = stepSize - 1; i < sizeFrameBuffer; i++) {
        fb[i] |= gpio_mask(gpio_pin::ctr0) | gpio_mask(gpio_pin::ctr1);
    }
}

void MatrixDriver::clearFrame() {
    uint32_t *fb = frameBuffer[nextBuffer];
    for(uint32_t i = 0; i < sizeFrameBuffer; i++) {
        fb[i] &= maskControlPins;
    }
}

void MatrixDriver::setBrightness(uint8_t level) {
    if(level > 100)
        brightness = 100;
    else
        brightness = level;
}

void MatrixDriver::setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if(x < 0 || y < 0) return;
    if(x >= pixelsPerRow || y >= (rowsPerScan << 4u)) return;
    uint32_t roff = y % rowsPerScan;
    uint32_t rcnt = y / rowsPerScan;
    uint32_t poff = x + (roff * pwmBits * pixelsPerRow);

    uint16_t R = (*pwmMapping)(pwmBits, r, brightness);
    uint16_t G = (*pwmMapping)(pwmBits, g, brightness);
    uint16_t B = (*pwmMapping)(pwmBits, b, brightness);

    if(rcnt == 0)
        setR0(&frameBuffer[nextBuffer][poff], pwmBits * pixelsPerRow, R, G, B);
    else if(rcnt == 1)
        setR1(&frameBuffer[nextBuffer][poff], pwmBits * pixelsPerRow, R, G, B);
    else if(rcnt == 2)
        setR2(&frameBuffer[nextBuffer][poff], pwmBits * pixelsPerRow, R, G, B);
    else if(rcnt == 3)
        setR3(&frameBuffer[nextBuffer][poff], pwmBits * pixelsPerRow, R, G, B);
}

void MatrixDriver::setR0(uint32_t* pixel, uint32_t stride, uint16_t r, uint16_t g, uint16_t b) {
    for(uint8_t i = 0; i < pwmBits; i++) {
        if(r & 1u) *pixel |= gpio_mask(gpio_pin::r0);
        else       *pixel &= ~gpio_mask(gpio_pin::r0);

        if(g & 1u) *pixel |= gpio_mask(gpio_pin::g0);
        else       *pixel &= ~gpio_mask(gpio_pin::g0);

        if(b & 1u) *pixel |= gpio_mask(gpio_pin::b0);
        else       *pixel &= ~gpio_mask(gpio_pin::b0);

        r >>= 1u;
        g >>= 1u;
        b >>= 1u;
        pixel += stride;
    }
}

void MatrixDriver::setR1(uint32_t* pixel, uint32_t stride, uint16_t r, uint16_t g, uint16_t b) {
    for(uint8_t i = 0; i < pwmBits; i++) {
        if(r & 1u) *pixel |= gpio_mask(gpio_pin::r1);
        else       *pixel &= ~gpio_mask(gpio_pin::r1);

        if(g & 1u) *pixel |= gpio_mask(gpio_pin::g1);
        else       *pixel &= ~gpio_mask(gpio_pin::g1);

        if(b & 1u) *pixel |= gpio_mask(gpio_pin::b1);
        else       *pixel &= ~gpio_mask(gpio_pin::b1);

        r >>= 1u;
        g >>= 1u;
        b >>= 1u;
        pixel += stride;
    }
}

void MatrixDriver::setR2(uint32_t* pixel, uint32_t stride, uint16_t r, uint16_t g, uint16_t b) {
    for(uint8_t i = 0; i < pwmBits; i++) {
        if(r & 1u) *pixel |= gpio_mask(gpio_pin::r2);
        else       *pixel &= ~gpio_mask(gpio_pin::r2);

        if(g & 1u) *pixel |= gpio_mask(gpio_pin::g2);
        else       *pixel &= ~gpio_mask(gpio_pin::g2);

        if(b & 1u) *pixel |= gpio_mask(gpio_pin::b2);
        else       *pixel &= ~gpio_mask(gpio_pin::b2);

        r >>= 1u;
        g >>= 1u;
        b >>= 1u;
        pixel += stride;
    }
}

void MatrixDriver::setR3(uint32_t* pixel, uint32_t stride, uint16_t r, uint16_t g, uint16_t b) {
    for(uint8_t i = 0; i < pwmBits; i++) {
        if(r & 1u) *pixel |= gpio_mask(gpio_pin::r3);
        else       *pixel &= ~gpio_mask(gpio_pin::r3);

        if(g & 1u) *pixel |= gpio_mask(gpio_pin::g3);
        else       *pixel &= ~gpio_mask(gpio_pin::g3);

        if(b & 1u) *pixel |= gpio_mask(gpio_pin::b3);
        else       *pixel &= ~gpio_mask(gpio_pin::b3);

        r >>= 1u;
        g >>= 1u;
        b >>= 1u;
        pixel += stride;
    }
}

uint16_t pwmMappingCie1931(uint8_t bits, uint8_t level, uint8_t intensity) {
    auto out_factor = (float) ((1u << bits) - 1);
    auto v = (float) (level * intensity) / 255.0f;
    return out_factor * ((v <= 8.0f) ? v / 902.3f : powf((v + 16.0f) / 116.0f, 3.0f));
}
