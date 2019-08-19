//
// Created by robert on 8/15/19.
//

#include "MatrixDriver.h"

#include <cstring>
#include <cstdlib>
#include <cmath>
#include <zconf.h>

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

// mask for control pins
const uint32_t maskControlPins = gpio_mask(gpio_pin::ctr0)  |
                                 gpio_mask(gpio_pin::ctr1)  |
                                 gpio_mask(gpio_pin::clk)   |
                                 gpio_mask(gpio_pin::write);

// mask for output pins
const uint32_t maskOut = gpio_mask(gpio_pin::ctr0)  |
                         gpio_mask(gpio_pin::ctr1)  |
                         gpio_mask(gpio_pin::clk)   |
                         gpio_mask(gpio_pin::write) |

                         gpio_mask(gpio_pin::r0) |
                         gpio_mask(gpio_pin::g0) |
                         gpio_mask(gpio_pin::b0) |

                         gpio_mask(gpio_pin::r1) |
                         gpio_mask(gpio_pin::g1) |
                         gpio_mask(gpio_pin::b1) |

                         gpio_mask(gpio_pin::r2) |
                         gpio_mask(gpio_pin::g2) |
                         gpio_mask(gpio_pin::b2) |

                         gpio_mask(gpio_pin::r3) |
                         gpio_mask(gpio_pin::g3) |
                         gpio_mask(gpio_pin::b3);

MatrixDriver::MatrixDriver(int pixelsPerRow, int rowsPerScan, int pwmBits) :
frameBuffer{}, threadGpio{}, mutexBuffer(PTHREAD_MUTEX_INITIALIZER), condBuffer(PTHREAD_COND_INITIALIZER),
pwmMapping{}
{
    if(pixelsPerRow < 1) abort();
    if(rowsPerScan < 1) abort();
    if(rowsPerScan > 32) abort();
    if(pwmBits < 1) abort();
    if(pwmBits > 16) abort();

    this->pixelsPerRow = pixelsPerRow;
    this->rowsPerScan = rowsPerScan;
    this->pwmBits = pwmBits;

    sizeFrameBuffer = pwmBits * pixelsPerRow * rowsPerScan * 2 + 4;
    frameRaw = new uint32_t[sizeFrameBuffer * 2];
    bzero(frameRaw, sizeFrameBuffer * 2 * sizeof(uint32_t));

    frameBuffer[0] = frameRaw;
    frameBuffer[1] = frameRaw + sizeFrameBuffer;

    initFrameBuffer(frameBuffer[0]);
    initFrameBuffer(frameBuffer[1]);

    nextBuffer = 0;

    // display off by default
    bzero(pwmMapping, sizeof(pwmMapping));

    initGpio();
}

MatrixDriver::~MatrixDriver() {
    delete frameRaw;
}

void MatrixDriver::initGpio() {

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

void MatrixDriver::setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if(x < 0 || y < 0) return;
    if(x >= pixelsPerRow || y >= (rowsPerScan << 4u)) return;
    uint32_t roff = y % rowsPerScan;
    uint32_t rcnt = y / rowsPerScan;
    uint32_t poff = x + (roff * pwmBits * pixelsPerRow);

    // map pwm values
    uint16_t R = pwmMapping[r];
    uint16_t G = pwmMapping[g];
    uint16_t B = pwmMapping[b];

    // set gpio masks
    uint32_t maskR = 0, maskG = 0, maskB = 0;
    if(rcnt == 0) {
        maskR = gpio_mask(gpio_pin::r0);
        maskG = gpio_mask(gpio_pin::g0);
        maskB = gpio_mask(gpio_pin::b0);
    }
    else if(rcnt == 1) {
        maskR = gpio_mask(gpio_pin::r1);
        maskG = gpio_mask(gpio_pin::g1);
        maskB = gpio_mask(gpio_pin::b1);
    }
    else if(rcnt == 2) {
        maskR = gpio_mask(gpio_pin::r2);
        maskG = gpio_mask(gpio_pin::g2);
        maskB = gpio_mask(gpio_pin::b2);
    }
    else if(rcnt == 3) {
        maskR = gpio_mask(gpio_pin::r3);
        maskG = gpio_mask(gpio_pin::g3);
        maskB = gpio_mask(gpio_pin::b3);
    }

    // set pixel gpio bits
    const auto stride = pwmBits * pixelsPerRow;
    auto pixel = &frameBuffer[nextBuffer][poff];

    for(uint8_t i = 0; i < pwmBits; i++) {
        if(R & 1u) *pixel |= maskR;
        else       *pixel &= ~maskR;

        if(G & 1u) *pixel |= maskG;
        else       *pixel &= ~maskG;

        if(B & 1u) *pixel |= maskB;
        else       *pixel &= ~maskB;

        R >>= 1u;
        G >>= 1u;
        B >>= 1u;
        pixel += stride;
    }
}

void MatrixDriver::setPixel(int x, int y, uint8_t *rgb) {
    setPixel(x, y, rgb[0], rgb[1], rgb[2]);
}

void MatrixDriver::setPixels(int &x, int &y, uint8_t *rgb, int pixelCount) {
    for(int i = 0; i < pixelCount; i++) {
        setPixel(x, y, rgb);
        rgb += 3;
        if(++x >= pixelsPerRow) {
            x = 0;
            ++y;
        }
    }
}

void MatrixDriver::sendFrame(const uint32_t *fb) {
    for(size_t i = 0; i < sizeFrameBuffer; i++) {
        *gpioSet = fb[i];
        *gpioClr = ~fb[i] & maskOut;

        usleep(1);
    }
}



static uint16_t pwmMappingCie1931(uint8_t bits, uint8_t level, uint8_t intensity) {
    auto out_factor = (float) ((1u << bits) - 1);
    auto v = (float) (level * intensity) / 255.0f;
    return out_factor * ((v <= 8.0f) ? v / 902.3f : powf((v + 16.0f) / 116.0f, 3.0f));
}

void createPwmLutCie1931(uint8_t bits, float brightness, MatrixDriver::pwm_lut &pwmLut) {
    for(int i = 0; i < 256; i++) {
        pwmLut[i] = pwmMappingCie1931(bits, (uint8_t)i, (uint8_t)brightness);
    }
}
