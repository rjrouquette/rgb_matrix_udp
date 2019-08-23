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
#include <sched.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <cstdarg>

#define FB_WIDTH (483)
#define FB_HEIGHT (271)
#define FB_DEPTH (32)
#define MAX_PIXELS (483*271)

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

MatrixDriver::MatrixDriver(const char *fbDev, const char *ttyDev, int pixelsPerRow, int rowsPerScan, int pwmBits) :
threadGpio{}, mutexBuffer(PTHREAD_MUTEX_INITIALIZER), condBuffer(PTHREAD_COND_INITIALIZER),
pwmMapping{}, finfo{}, vinfo{}
{
    if(pixelsPerRow < 1) abort();
    if(rowsPerScan < 1) abort();
    if(rowsPerScan > 32) abort();
    if(pwmBits < 1) abort();
    if(pwmBits > 16) abort();
    if(pixelsPerRow * rowsPerScan > MAX_PIXELS) abort();

    this->pixelsPerRow = pixelsPerRow;
    this->rowsPerScan = rowsPerScan;
    this->pwmBits = pwmBits;

    auto ttyfd = open(ttyDev, O_RDWR);
    if(ttyfd < 0)
        die("failed to open tty device: %s", ttyDev);
    if(ioctl(ttyfd, KDSETMODE, KD_GRAPHICS) != 0)
        die("failed to set %s to graphics mode: %s", ttyDev, strerror(errno));

    fbfd = open(fbDev, O_RDWR);
    if(fbfd < 0)
        die("failed to open fb device: %s", fbDev);

    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) != 0)
        die("failed to get variable screen info: %s",strerror(errno));

    // abort if framebuffer is not configured correctly
    if(vinfo.xres != FB_WIDTH)
        die("framebuffer width is unexpected: %d != %d", vinfo.xres, FB_WIDTH);
    if(vinfo.yres != FB_HEIGHT)
        die("framebuffer height is unexpected: %d != %d", vinfo.yres, FB_HEIGHT);
    if(vinfo.bits_per_pixel != FB_DEPTH)
        die("framebuffer depth is unexpected: %d != %d", vinfo.bits_per_pixel, FB_DEPTH);

    // configure virtual framebuffer for flipping
    vinfo.xoffset = 0;
    vinfo.yoffset = 0;
    vinfo.xres_virtual = vinfo.xres;
    vinfo.yres_virtual = vinfo.yres * 3;
    if(ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo) != 0)
        die("failed to set variable screen info: %s",strerror(errno));

    if(ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) != 0)
        die("failed to get fixed screen info: %s",strerror(errno));

    // get pointer to raw frame buffer data
    frameRaw = (uint8_t *) mmap(nullptr, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if(frameRaw == MAP_FAILED)
        die("failed to map raw screen buffer data: %s",strerror(errno));

    // configure frame pointers
    currOffset = 0;
    frameSize = vinfo.yres * finfo.line_length;
    currFrame = (uint32_t *) (frameRaw + frameSize);
    nextFrame = (uint32_t *) (frameRaw + frameSize * 2);

    printf("pixels: %d\n", vinfo.yres * vinfo.xres);
    printf("frame size: %ld\n", frameSize);
    printf("left margin: %d\n", vinfo.left_margin);
    printf("right margin: %d\n", vinfo.right_margin);
    printf("x offset: %d\n", vinfo.xoffset);
    printf("x virt res: %d\n", vinfo.xres_virtual);

    // clear frame buffer
    bzero(frameRaw, finfo.smem_len);
    for(uint32_t y = 0; y < vinfo.yres; y++) {
        for(uint32_t x = 0; x < vinfo.xres; x++) {
            uint32_t off = y * (finfo.line_length / 4) + x;
            currFrame[off] = 0xff000000;
            nextFrame[off] = 0xff000000;
        }
    }

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

    munmap(frameRaw, finfo.smem_len);
    close(fbfd);
}

void MatrixDriver::flipBuffer() {
    pthread_mutex_lock(&mutexBuffer);

    // move frame offset
    currOffset = (currOffset + 1u) % 2u;

    // swap buffer pointers
    auto temp = currFrame;
    currFrame = nextFrame;
    nextFrame = temp;

    // wake output thread
    pthread_cond_signal(&condBuffer);
    pthread_mutex_unlock(&mutexBuffer);
}

void MatrixDriver::clearFrame() {
    for(uint32_t y = 0; y < vinfo.yres; y++) {
        for(uint32_t x = 0; x < vinfo.xres; x++) {
            uint32_t off = y * (finfo.line_length / 4) + x;
            nextFrame[off] = 0xff000000;
        }
    }
}

size_t MatrixDriver::translateOffset(size_t off) {
    auto a = off / vinfo.xres;
    auto b = off % vinfo.xres;

    return (a * (finfo.line_length / 4)) + b;
}

void MatrixDriver::setPixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
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
    for(uint8_t i = 0; i < pwmBits; i++) {
        auto pixel = &nextFrame[translateOffset(poff)];

        if(R & 1u) *pixel |= maskR;
        else       *pixel &= ~maskR;

        if(G & 1u) *pixel |= maskG;
        else       *pixel &= ~maskG;

        if(B & 1u) *pixel |= maskB;
        else       *pixel &= ~maskB;

        R >>= 1u;
        G >>= 1u;
        B >>= 1u;
        poff += pixelsPerRow;
    }
}

void MatrixDriver::setPixel(uint32_t x, uint32_t y, uint8_t *rgb) {
    setPixel(x, y, rgb[0], rgb[1], rgb[2]);
}

void MatrixDriver::setPixels(uint32_t &x, uint32_t &y, uint8_t *rgb, size_t pixelCount) {
    for(size_t i = 0; i < pixelCount; i++) {
        setPixel(x, y, rgb);
        rgb += 3;
        if(++x >= pixelsPerRow) {
            x = 0;
            ++y;
        }
    }
}

void MatrixDriver::sendFrame(const uint32_t *fb) {
    fb_var_screeninfo temp = vinfo;
    temp.yoffset = (currOffset + 1) * vinfo.yres;
    temp.xoffset = 0;

    if(ioctl(fbfd, FBIOPAN_DISPLAY, &temp) != 0)
        die("failed to pan frame buffer: %s", strerror(errno));

    if(ioctl(fbfd, FBIO_WAITFORVSYNC, nullptr) != 0)
        die("failed to wait for vsync: %s", strerror(errno));
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
        ctx.sendFrame(ctx.currFrame);
    }
    pthread_mutex_unlock(&ctx.mutexBuffer);

    return nullptr;
}

void MatrixDriver::die(const char *format, ...) {
    va_list argptr;
    va_start(argptr, format);
    vfprintf(stderr, format, argptr);
    fwrite("\n", 1, 1, stderr);
    fflush(stderr);
    va_end(argptr);
    abort();
}



static uint16_t pwmMappingCie1931(uint8_t bits, int level, float intensity) {
    auto scale = (double) ((1u << bits) - 1);
    auto v = ((double) level) * intensity / 255.0;
    auto value = ((v <= 8.0) ? v / 902.3 : pow((v + 16.0) / 116.0, 3.0));
    return (uint16_t) lround(scale * value);
}

void createPwmLutCie1931(uint8_t bits, float brightness, MatrixDriver::pwm_lut &pwmLut) {
    for(int i = 0; i < 256; i++) {
        pwmLut[i] = pwmMappingCie1931(bits, i, brightness);
    }
}

static uint16_t pwmMappingLinear(uint8_t bits, int level, float intensity) {
    auto scale = (double) ((1u << bits) - 1);
    auto value = ((double) level) * intensity / 25500.0;
    return (uint16_t) lround(scale * value);
}

void createPwmLutLinear(uint8_t bits, float brightness, MatrixDriver::pwm_lut &pwmLut) {
    for(int i = 0; i < 256; i++) {
        pwmLut[i] = pwmMappingLinear(bits, i, brightness);
    }
}
