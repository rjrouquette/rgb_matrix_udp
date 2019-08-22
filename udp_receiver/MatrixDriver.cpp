//
// Created by robert on 8/15/19.
//

#include "MatrixDriver.h"

#include <cstring>
#include <cstdlib>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <syscall.h>
#include <sched.h>
#include <cassert>
#include <sys/ioctl.h>

struct output_bits {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

output_bits output_map[8] = {
    {0, 1, 2},
    {3, 4, 5},
    {6, 7, 8},
    {9, 10, 11},
    {12, 13, 14},
    {15, 16, 17},
    {18, 19, 20},
    {21, 22, 23}
};

MatrixDriver::MatrixDriver(const char *fbDev, int pixelsPerRow, int rowsPerScan, int pwmBits) :
frameBuffer{}, threadGpio{}, mutexBuffer(PTHREAD_MUTEX_INITIALIZER), condBuffer(PTHREAD_COND_INITIALIZER),
pwmMapping{}, finfo{}, vinfo{}
{
    fbfd = open(fbDev, O_RDWR);
    assert(fbfd >= 0);
    assert(0 == ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo));
    assert(0 == ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo));

    std::cout << "id: " << finfo.id << std::endl;
    std::cout << "line: " << finfo.line_length << std::endl;
    std::cout << "smem: " << finfo.smem_len << std::endl;
    std::cout << "mmio: " << finfo.mmio_len << std::endl;

    std::cout << "dim: " << vinfo.width << " x " << vinfo.height << std::endl;
    std::cout << "res: " << vinfo.xres << " x " << vinfo.yres << std::endl;
    std::cout << "virt: " << vinfo.xres_virtual << " x " << vinfo.yres_virtual << std::endl;
    std::cout << "color depth: " << vinfo.bits_per_pixel << std::endl;

    if(pixelsPerRow < 1) abort();
    if(rowsPerScan < 1) abort();
    if(rowsPerScan > 32) abort();
    if(pwmBits < 1) abort();
    if(pwmBits > 16) abort();

    this->pixelsPerRow = pixelsPerRow;
    this->rowsPerScan = rowsPerScan;
    this->pwmBits = pwmBits;

    sizeFrameBuffer = pwmBits * pixelsPerRow * rowsPerScan;
    frameRaw = new uint32_t[sizeFrameBuffer * 2];
    mlock(frameRaw, sizeFrameBuffer * 2 * sizeof(uint32_t));
    bzero(frameRaw, sizeFrameBuffer * 2 * sizeof(uint32_t));

    frameBuffer[0] = frameRaw;
    frameBuffer[1] = frameRaw + sizeFrameBuffer;
    nextBuffer = 0;

    // display off by default
    bzero(pwmMapping, sizeof(pwmMapping));

    // start output thread
    isRunning = true;
    pthread_create(&threadGpio, nullptr, doRefresh, this);
}

MatrixDriver::~MatrixDriver() {
    // stop gpio thread
    isRunning = false;
    clearFrame();
    flipBuffer();
    pthread_join(threadGpio, nullptr);

    delete frameRaw;
    close(fbfd);
}

void MatrixDriver::flipBuffer() {
    assert(0 == ioctl(fbfd, FBIO_WAITFORVSYNC, nullptr));
    return;

    pthread_mutex_lock(&mutexBuffer);
    nextBuffer ^= 1u;
    pthread_cond_signal(&condBuffer);
    pthread_mutex_unlock(&mutexBuffer);
}

void MatrixDriver::clearFrame() {
    bzero(frameBuffer[nextBuffer], sizeFrameBuffer);
}

void MatrixDriver::setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if(x < 0 || y < 0) return;
    if(x >= pixelsPerRow || y >= (rowsPerScan << 3u)) return;
    uint32_t roff = y % rowsPerScan;
    uint32_t rcnt = y / rowsPerScan;
    uint32_t poff = x + (roff * pwmBits * pixelsPerRow);

    // set gpio masks
    const uint32_t maskR = 1u << output_map[rcnt].red;
    const uint32_t maskG = 1u << output_map[rcnt].green;
    const uint32_t maskB = 1u << output_map[rcnt].blue;

    // get pwm values
    uint16_t R = pwmMapping[r];
    uint16_t G = pwmMapping[g];
    uint16_t B = pwmMapping[b];

    // set pixel bits
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
    // wait for vsync
    assert(0 == ioctl(fbfd, FBIO_WAITFORVSYNC, nullptr));

    // write out to frame buffer
}

void* MatrixDriver::doRefresh(void *obj) {
    // set thread name
    pthread_setname_np(pthread_self(), "gpio_out");

    // set cpu affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

    // set scheduling priority
    sched_param p = { sched_get_priority_max(SCHED_FIFO) };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &p);

    // do gpio output
    auto &ctx = *(MatrixDriver *)obj;
    pthread_mutex_lock(&ctx.mutexBuffer);
    while(ctx.isRunning) {
        pthread_cond_wait(&ctx.condBuffer, &ctx.mutexBuffer);
        ctx.sendFrame(ctx.frameBuffer[ctx.nextBuffer ^ 1u]);
    }
    pthread_mutex_unlock(&ctx.mutexBuffer);

    return nullptr;
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
