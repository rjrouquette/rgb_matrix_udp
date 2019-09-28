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
#include <set>

#define FB_WIDTH (483)
#define FB_HEIGHT (271)
#define FB_DEPTH (32)
#define MAX_PIXELS (483*271)

// little-endian RGBA byte order on frame buffer
#define DPI_R(x) (0u + x)
#define DPI_G(x) (8u + x)
#define DPI_B(x) (16u + x)

struct output_bits {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

/*
 *  Pin Translation
 *
 *  RPi Header  DPI     RGB Matrix      SRAM    XMega
 *  38          R0      P3.B0           SIO0    CFG.10
 *  40          R1      P3.R0           SIO1    CFG.11
 *  15          R2      P1.R0           SIOx
 *  16          R3      P1.G0           SIO1    CFG.03
 *  18          R4      P1.B0           SIOx
 *  22          R5      P1.B1           SIO1    CFG.05
 *  37          R6      P3.G0           SIOx
 *  13          R7      P0.B1           SIO0    CFG.02
 *  32          G0      P2.G0           SIOx
 *  33          G1      P3.B1           SIOx
 *  08          G2      P0.R0           SIO0    CFG.00
 *  10          G3      P0.B0           SIOx
 *  36          G4      P3.G1           SIO1    CFG.09
 *  11          G5      P0.G1           SIO1    CFG.01
 *  12          G6      P0.R1           SIOx
 *  35          G7      P3.R1           SIOx
 *  07          B0      P0.G0           SIOx
 *  29          B1      P2.B0           SIO1    CFG.07
 *  31          B2      P2.R0           SIO0    CFG.08
 *  26          B3      P2.R1           SIOx
 *  24          B4      P2.B1           SIOx
 *  21          B5      P1.G1           SIOx
 *  19          B6      P1.R1           SIO0    CFG.04
 *  23          B7      P2.G1           SIO0    CFG.06
*/

output_bits output_map[8] = {
    { DPI_G(2), DPI_B(0), DPI_G(3) }, // P0.0 = G2, B0, G3
    { DPI_G(6), DPI_G(5), DPI_R(7) }, // P0.1 = G6, G5, R7
    { DPI_R(2), DPI_R(3), DPI_R(4) }, // P1.0 = R2, R3, R4
    { DPI_B(6), DPI_B(5), DPI_R(5) }, // P1.1 = B6, B5, R5
    { DPI_B(2), DPI_G(0), DPI_B(1) }, // P2.0 = B2, G0, B1
    { DPI_B(3), DPI_B(7), DPI_B(4) }, // P2.1 = B3, B7, B4
    { DPI_R(1), DPI_R(6), DPI_R(0) }, // P3.0 = R1, R6, R0
    { DPI_G(7), DPI_G(4), DPI_G(1) }  // P3.1 = G7, G4, G1
};
static bool checkOutputMap();

uint8_t write_map[6] = {
        DPI_R(1),   DPI_R(3),   DPI_R(5),
        DPI_G(4),   DPI_G(5),   DPI_B(1)
};
static bool checkWriteMap();

uint8_t config_map[12] = {
        DPI_G(2),   DPI_G(5),   DPI_R(7),
        DPI_R(3),   DPI_B(6),   DPI_R(5),
        DPI_B(7),   DPI_B(1),   DPI_B(2),
        DPI_G(4),   DPI_R(0),   DPI_R(1)
};
static bool checkConfigMap();

static uint16_t crc_ccitt_update (uint16_t crc, uint8_t data);

MatrixDriver::MatrixDriver(const char *fbDev, const char *ttyDev, int pixelsPerRow, int rowsPerScan, int pwmBits) :
threadGpio{}, mutexBuffer(PTHREAD_MUTEX_INITIALIZER), condBuffer(PTHREAD_COND_INITIALIZER),
pwmMapping{}, finfo{}, vinfo{}
{
    if(!checkOutputMap()) die("overlapping bits in output map");
    if(!checkWriteMap()) die("overlapping bits in write command map");
    if(!checkConfigMap()) die("overlapping bits in config map");

    if(pixelsPerRow < 1) die("display must have at least one pixel per row, %d were specified", pixelsPerRow);
    if(rowsPerScan < 1) die("display must have at least one row address, %d were specified", rowsPerScan);
    if(rowsPerScan > 32) die("display may not have more than 32 row addresses, %d were specified", rowsPerScan);
    if(pwmBits < 1) die("display must have at least one pwm bit per pixel, %d were specified", pwmBits);
    if(pwmBits > 16) die("display may not have more than 16 pwm bits per pixel, %d were specified", pwmBits);

    // validate raster size
    if(pwmBits * pixelsPerRow * rowsPerScan > MAX_PIXELS)
        die("raster size requires %d cells, the maximum is %d", pwmBits * pixelsPerRow * rowsPerScan, MAX_PIXELS);

    this->pixelsPerRow = pixelsPerRow;
    this->rowsPerScan = rowsPerScan;
    this->pwmBits = pwmBits;

    initFrameHeader();

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

    // set frame header cells
    auto *ptr = currFrame;
    for(auto v : frameHeader) {
        *(ptr++) = v;
    }

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

static const uint8_t xmega_ext_clk = (0x03u<<6u);

// taken from xmega64a1u headers
static const uint8_t xmega_clk_div[10] = {
        (0x00u<<2u),  /* Divide by 1 */
        (0x01u<<2u),  /* Divide by 2 */
        (0x03u<<2u),  /* Divide by 4 */
        (0x05u<<2u),  /* Divide by 8 */
        (0x07u<<2u),  /* Divide by 16 */
        (0x09u<<2u),  /* Divide by 32 */
        (0x0Bu<<2u),  /* Divide by 64 */
        (0x0Du<<2u),  /* Divide by 128 */
        (0x0Fu<<2u),  /* Divide by 256 */
        (0x11u<<2u),  /* Divide by 512 */
};

void MatrixDriver::initFrameHeader() {
    // set SRAM write command
    frameHeader[0] = 0xff000000;    // set alpha bits, data bits zero
    frameHeader[1] = 0xff000000;    // set alpha bits, data bits zero
    // set bits for write command
    for (const auto bit : write_map) {
        frameHeader[1] |= 1u << bit;
    }

    // compute optimal timing
    uint8_t pllctrl, psctrl, pwmBase;
    // TODO establish profile framework
    // empirically derived values for 4x5 array of 64x64 panels
    pllctrl = xmega_ext_clk | 9u; // external clock, multiply by 9
    psctrl = xmega_clk_div[2]; // divide by 4
    pwmBase = 4;

    // compute xmega config
    uint8_t config[12];
    // magic bytes
    config[0] = 0xda;
    config[1] = 0x21;
    config[2] = pllctrl;
    config[3] = psctrl;
    config[4] = pwmBits;
    config[5] = rowsPerScan;
    config[6] = pixelsPerRow;
    config[7] = pwmBase;
    config[8] = 0; // future use
    config[9] = 0; // future use

    // compute CRC-16 CCITT checksum
    uint16_t crc = 0u;
    for (int j = 0; j < 10; j++) {
        crc = crc_ccitt_update(crc, config[j]);
    }
    config[10] = crc & 0xffu;
    config[11] = (crc >> 8u) & 0xffu;

    for(auto &v : config) {
        printf("%02x", v);
        v = 0xffu;
    }
    printf("\n");
    fflush(stdout);

    // apply bits
    for(size_t i = 0; i < 8; i++) {
        auto &fh = frameHeader[i + 2];
         fh = 0xff000000u; // set alpha bits, data bits zero
        for(size_t j = 0; j < 12; j++) {
            if(config[j] & 0x01u) fh |= (1u << config_map[j]);
            config[j] >>= 1u;
        }
    }
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


static bool checkOutputMap() {
    std::set<uint8_t> bits;
    for(const auto &m : output_map) {
        if(bits.count(m.red)) return false;
        bits.insert(m.red);
        if(bits.count(m.green)) return false;
        bits.insert(m.green);
        if(bits.count(m.blue)) return false;
        bits.insert(m.blue);
    }
    return true;
}

static bool checkWriteMap() {
    std::set<uint8_t> bits;
    for(const auto &v : write_map) {
        if(bits.count(v)) return false;
        bits.insert(v);
    }
    return true;
}

static bool checkConfigMap() {
    std::set<uint8_t> bits;
    for(const auto &v : config_map) {
        if(bits.count(v)) return false;
        bits.insert(v);
    }
    return true;
}

// CRC16-CCITT
static inline uint8_t lo8(uint16_t v) { return v & 0xffu; }
static inline uint8_t hi8(uint16_t v) { return (v >> 8u) & 0xffu; }

static uint16_t crc_ccitt_update (uint16_t crc, uint8_t data) {
    data ^= lo8 (crc);
    data ^= data << 4u;
    return ((((uint16_t)data << 8u) | hi8 (crc)) ^ (uint8_t)(data >> 4u)
             ^ ((uint16_t)data << 3u));
}
