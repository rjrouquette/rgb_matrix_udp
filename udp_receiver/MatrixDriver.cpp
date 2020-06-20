//
// Created by robert on 8/15/19.
//

#include "MatrixDriver.h"
#include "RowEncoding.h"

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <sys/mman.h>
#include <sched.h>
#include <cstdarg>
#include <cstdio>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <fcntl.h>

#define GPIO_REGISTER_OFFSET    0x200000    // start of gpio registers
#define REGISTER_BLOCK_SIZE     4096        // 4 kiB block size

#define DEV_MEM ("/dev/mem")
#define DEV_FB ("/dev/fb0")
#define DEV_TTY ("/dev/tty1")

#define FB_DEPTH (32)
#define HEADER_OFFSET (2)
#define ROW_PADDING (32)

// DPI RGB to panel RGB bit mapping
// bit offsets are in octal notation
// 000 -> B0
// 010 -> G0
// 020 -> R0
static const uint8_t mapRGB[8][3] = {
//       RED  GRN  BLU
        {016, 013, 027}, // p0r0 -> g6, p0g0 -> g3, p0b0 -> r7
        {015, 012, 000}, // p0r1 -> g5, p0g1 -> g2, p0b1 -> b0
        {025, 005, 006}, // p1r0 -> r5, p1g0 -> b5, p1b0 -> b6
        {024, 023, 022}, // p1r1 -> r4, p1g1 -> r3, p1b1 -> r2
        {004, 007, 003}, // p2r0 -> b4, p2g0 -> b7, p2b0 -> b3
        {010, 001, 002}, // p2r1 -> g0, p2g1 -> b1, p2b1 -> b2
        {020, 014, 017}, // p3r0 -> r0, p3g0 -> g4, p3b0 -> g7
        {026, 011, 021}, // p3r1 -> r6, p3g1 -> g1, p3b1 -> r1
};

// mangle header bits to match board layout
// see mapRGB above for bit offset interpretation
static const unsigned mapHeaderBits[8] = {
        027, // PD0 -> R7
        023, // PD1 -> R3
        022, // PD2 -> R2
        024, // PD3 -> R4
        025, // PD4 -> R5
        021, // PD5 -> R1
        026, // PD6 -> R6
        020  // PD7 -> R0
};

// number of scan rows for each panel type
static const unsigned mapPanelScanRows[5] = {
        16,
        32,
        32,
        8,
	4
};

static void setHeaderRowCode(uint32_t *header, unsigned srow, unsigned pwmRows, RowEncoding::Encoder encoder);
static void setHeaderPulseWidth(uint32_t *header, unsigned pulseWidth);
static void setHeaderByte(uint32_t &headerCell, unsigned byte);
static void createPwmMap(unsigned pwmBits, unsigned pwmRows, unsigned *&mapPulseWidth, unsigned *&mapPwmBit);

static void selfTestRGB();
static void selfTestHeader();
static void die(const char *format, ...) __attribute__ ((__format__ (__printf__, 1, 2))) __attribute__((noreturn));

// default pixel mapping does nothing
void PixelMapping::remap(unsigned int &x, unsigned int &y) {}

MatrixDriver * MatrixDriver::createInstance(unsigned pwmBits, RowFormat rowFormat) {
    selfTestRGB();
    selfTestHeader();

    unsigned *mapPulseWidth = nullptr, *mapPwmBit = nullptr;
    fb_fix_screeninfo finfo = {};
    fb_var_screeninfo vinfo = {};

    // validate pwm bits
    if(pwmBits > 16)
        die("invalid number of pwm bits: %d > 16", pwmBits);

    // set tty to graphics mode
    auto ttyfd = open(DEV_TTY, O_RDWR);
    if(ttyfd < 0)
        die("failed to open tty device: %s", DEV_TTY);
    if(ioctl(ttyfd, KDSETMODE, KD_GRAPHICS) != 0)
        die("failed to set %s to graphics mode: %s", DEV_TTY, strerror(errno));

    // access raw framebuffer device
    auto fbfd = open(DEV_FB, O_RDWR);
    if(fbfd < 0)
        die("failed to open fb device: %s", DEV_FB);

    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) != 0)
        die("failed to get variable screen info: %s",strerror(errno));

    if(vinfo.bits_per_pixel != FB_DEPTH)
        die("framebuffer depth is unexpected: %d != %d", vinfo.bits_per_pixel, FB_DEPTH);
    if(vinfo.xres < ROW_PADDING)
        die("framebuffer width is unexpected: %d < %d", vinfo.xres, ROW_PADDING);

    // validate row count
    const auto scanRowCnt = mapPanelScanRows[rowFormat];
    unsigned rows = vinfo.yres - 1;
    auto pwmRows = rows / scanRowCnt;
    auto yfit = (pwmRows * scanRowCnt) + 1;
    if(vinfo.yres != yfit)
        die("framebuffer height is invalid: %d != %d", vinfo.yres, yfit);

    // create pwm mappings
    createPwmMap(pwmBits, pwmRows, mapPulseWidth, mapPwmBit);

    // configure virtual framebuffer for blitting
    vinfo.xoffset = 0;
    vinfo.yoffset = 0;
    vinfo.xres_virtual = vinfo.xres;
    vinfo.yres_virtual = vinfo.yres;
    if(ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo) != 0)
        die("failed to set variable screen info: %s",strerror(errno));

    if(ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) != 0)
        die("failed to get fixed screen info: %s",strerror(errno));

    // determine block sizes
    const size_t rowBlock = finfo.line_length / sizeof(uint32_t);
    const size_t pwmBlock = rowBlock * pwmRows;
    if(rowBlock * sizeof(uint32_t) != finfo.line_length)
        die("row size is not integer multiple of uint32_t: %d", finfo.line_length);

    auto driver = new MatrixDriver(
            scanRowCnt,
            pwmRows,
            mapPwmBit,
            rowBlock,
            pwmBlock
    );
    driver->ttyfd = ttyfd;
    driver->fbfd = fbfd;

    // get pointer to raw frame buffer data
    driver->frameRaw = (uint8_t *) mmap(nullptr, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if(driver->frameRaw == MAP_FAILED)
        die("failed to map raw screen buffer data: %s",strerror(errno));

    // configure frame pointers
    driver->frameSize = vinfo.yres * rowBlock;
    driver->currFrame = new uint32_t[driver->frameSize];
    driver->nextFrame = new uint32_t[driver->frameSize];

    driver->frameHeader = new uint32_t[(pwmRows * scanRowCnt + 1) * ROW_PADDING];
    // clear header cells
    for(size_t i = 0; i < (pwmRows * scanRowCnt + 1) * ROW_PADDING; i++) {
        driver->frameHeader[i] = 0xff000000u;
    }

    // set scan row headers
    auto encoder = RowEncoding::encoder[rowFormat];
    unsigned pwFact = driver->getWidth() / mapPulseWidth[pwmRows-1];
    auto header = driver->frameHeader + ROW_PADDING + HEADER_OFFSET;
    unsigned srow = 0;
    setHeaderRowCode(header, srow++, pwmRows, encoder);
    for(unsigned r = 0; r < scanRowCnt; r++) {
        for (unsigned p = 0; p < pwmRows; p++) {
            setHeaderRowCode(header, srow++, pwmRows, encoder);
            setHeaderPulseWidth(header + 4, mapPulseWidth[p] * pwFact);
            header += ROW_PADDING;
        }
    }
    delete[] mapPulseWidth;

    printf("x res: %d\n", vinfo.xres);
    printf("y res: %d\n", vinfo.yres);
    printf("pixels: %d\n", vinfo.yres * vinfo.xres);
    printf("frame size: %ld\n", driver->frameSize);
    printf("left margin: %d\n", vinfo.left_margin);
    printf("right margin: %d\n", vinfo.right_margin);
    printf("x offset: %d\n", vinfo.xoffset);
    printf("y offset: %d\n", vinfo.yoffset);
    printf("x vres: %d\n", vinfo.xres_virtual);
    printf("y vres: %d\n", vinfo.yres_virtual);
    printf("row block: %ld\n", rowBlock);
    printf("pwm block: %ld\n", pwmBlock);

    // clear frame buffers
    driver->clearFrame();
    driver->flipBuffer();
    driver->clearFrame();
    driver->flipBuffer();

    // start output thread
    driver->start();
    return driver;
}

MatrixDriver::MatrixDriver(
        unsigned _scanRowCnt,
        unsigned _pwmRows,
        const unsigned *_mapPwmBit,
        size_t _rowBlock,
        size_t _pwmBlock
) :
    matrixWidth(_rowBlock - ROW_PADDING), matrixHeight(_scanRowCnt * 8), scanRowCnt(_scanRowCnt), pwmRows(_pwmRows),
    mapPwmBit(_mapPwmBit), rowBlock(_rowBlock), pwmBlock(_pwmBlock), threadOutput{},
    mutexBuffer(PTHREAD_MUTEX_INITIALIZER), pwmMapping{}, finfo{}, vinfo{}
{
    isRunning = false;
    ttyfd = -1;
    fbfd = -1;
    frameSize = 0;
    frameRaw = nullptr;
    frameHeader = nullptr;
    currFrame = nullptr;
    nextFrame = nullptr;
    pixelMapping = this;

    // display off by default
    bzero(pwmMapping, sizeof(pwmMapping));
}

MatrixDriver::~MatrixDriver() {
    stop();

    delete[] mapPwmBit;
    delete[] frameHeader;
    delete[] currFrame;
    delete[] nextFrame;
    munmap(frameRaw, finfo.smem_len);
    close(fbfd);
    close(ttyfd);
}

void MatrixDriver::start() {
    isRunning = true;
    pthread_create(&threadOutput, nullptr, doRefresh, this);
}

void MatrixDriver::stop() {
    clearFrame();
    flipBuffer();
    usleep(10000);
    isRunning = false;
    pthread_join(threadOutput, nullptr);
}

void MatrixDriver::flipBuffer() {
    // swap buffer pointers
    pthread_mutex_lock(&mutexBuffer);
    auto temp = currFrame;
    currFrame = nextFrame;
    nextFrame = temp;
    pthread_mutex_unlock(&mutexBuffer);
}

void MatrixDriver::clearFrame() const {
    for(size_t i = 0; i < frameSize; i++) {
        nextFrame[i] = 0xff000000u;
    }

    // set row headers
    size_t rcnt = (pwmRows * scanRowCnt) + 1;
    auto header = frameHeader;
    auto row = nextFrame;
    for(size_t r = 0; r < rcnt; r++) {
        memcpy(row, header, ROW_PADDING * sizeof(uint32_t));
        header += ROW_PADDING;
        row += rowBlock;
    }
}

unsigned MatrixDriver::measureMappedWidth() const {
    unsigned w = 0;
    while(w < -1u) {
        auto x = w;
        unsigned y = 0;
        pixelMapping->remap(x, y);
        if(x >= matrixWidth) break;
        if(y >= matrixHeight) break;
        ++w;
    }
    return w;
}

unsigned MatrixDriver::measureMappedHeight() const {
    unsigned h = 0;
    while(h < -1u) {
        auto y = h;
        unsigned x = 0;
        pixelMapping->remap(x, y);
        if(x >= matrixWidth) break;
        if(y >= matrixHeight) break;
        ++h;
    }
    return h;
}

void MatrixDriver::setPixel(unsigned x, unsigned y, uint8_t r, uint8_t g, uint8_t b) const {
    // apply pixel coordinate remapping
    pixelMapping->remap(x, y);

    // verify coordinate bounds
    if(x >= matrixWidth || y >= matrixHeight) return;

    // compute pixel offset
    const auto yoff = y % scanRowCnt;

    const auto rgbOff = y / scanRowCnt;
    const uint32_t maskRedHi = 1u << mapRGB[rgbOff][0];
    const uint32_t maskGrnHi = 1u << mapRGB[rgbOff][1];
    const uint32_t maskBluHi = 1u << mapRGB[rgbOff][2];

    const uint32_t maskRedLo = ~maskRedHi;
    const uint32_t maskGrnLo = ~maskGrnHi;
    const uint32_t maskBluLo = ~maskBluHi;

    // get pwm values
    unsigned R = pwmMapping[r];
    unsigned G = pwmMapping[g];
    unsigned B = pwmMapping[b];

    // set pixel bits
    auto pixel = nextFrame + (yoff * pwmBlock) + ROW_PADDING + x;
    for(unsigned pwmRow = 0; pwmRow < pwmRows; pwmRow++) {
        const auto bit = mapPwmBit[pwmRow];
        auto &p = *pixel;
        pixel += rowBlock;

        if ((R >> bit) & 1u)
            p |= maskRedHi;
        else
            p &= maskRedLo;

        if ((G >> bit) & 1u)
            p |= maskGrnHi;
        else
            p &= maskGrnLo;

        if ((B >> bit) & 1u)
            p |= maskBluHi;
        else
            p &= maskBluLo;
    }
}

void MatrixDriver::setPixel(unsigned x, unsigned y, uint8_t *rgb) const {
    setPixel(x, y, rgb[0], rgb[1], rgb[2]);
}

void MatrixDriver::setPixels(unsigned &x, unsigned &y, uint8_t *rgb, size_t pixelCount) const {
    for(size_t i = 0; i < pixelCount; i++) {
        setPixel(x, y, rgb);
        rgb += 3;
        if(++x >= matrixWidth) {
            x = 0;
            ++y;
        }
    }
}

void* MatrixDriver::doRefresh(void *obj) {
    // set thread name
    pthread_setname_np(pthread_self(), "dpi_out");

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
    while(ctx.isRunning) {
        // blit frame data
        pthread_mutex_lock(&ctx.mutexBuffer);
        memcpy(ctx.frameRaw, ctx.currFrame, ctx.frameSize * sizeof(uint32_t));
        pthread_mutex_unlock(&ctx.mutexBuffer);

        // allow window for frame flip
        usleep(1000);
    }

    return nullptr;
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

// 5x7 hex characters
char hexChars[16][7][6] = {
        {
                " ### ",
                "#   #",
                "#  ##",
                "# # #",
                "##  #",
                "#   #",
                " ### "
        },{
                "  #  ",
                " ##  ",
                "# #  ",
                "  #  ",
                "  #  ",
                "  #  ",
                "#####"
        },{
                " ### ",
                "#   #",
                "    #",
                "  ## ",
                " #   ",
                "#    ",
                "#####",
        },{
                " ### ",
                "#   #",
                "    #",
                "  ## ",
                "    #",
                "#   #",
                " ### "
        },{
                "   # ",
                "  ## ",
                " # # ",
                "#  # ",
                "#####",
                "   # ",
                "   # "
        },{
                "#####",
                "#    ",
                "#### ",
                "    #",
                "    #",
                "#   #",
                " ### "
        },{
                " ### ",
                "#   #",
                "#    ",
                "#### ",
                "#   #",
                "#   #",
                " ### "
        },{
                "#####",
                "    #",
                "   # ",
                "  #  ",
                "  #  ",
                "  #  ",
                "  #  "
        },{
                " ### ",
                "#   #",
                "#   #",
                " ### ",
                "#   #",
                "#   #",
                " ### "
        },{
                " ### ",
                "#   #",
                "#   #",
                " ####",
                "    #",
                "#   #",
                " ### "
        },{
                " ### ",
                "#   #",
                "#   #",
                "#   #",
                "#####",
                "#   #",
                "#   #"
        },{
                "#### ",
                "#   #",
                "#   #",
                "#### ",
                "#   #",
                "#   #",
                "#### "
        },{
                " ### ",
                "#   #",
                "#    ",
                "#    ",
                "#    ",
                "#   #",
                " ### "
        },{
                "#### ",
                "#   #",
                "#   #",
                "#   #",
                "#   #",
                "#   #",
                "#### "
        },{
                "#####",
                "#    ",
                "#    ",
                "#### ",
                "#    ",
                "#    ",
                "#####"
        },{
                "#####",
                "#    ",
                "#    ",
                "#### ",
                "#    ",
                "#    ",
                "#    "
        }
};

void MatrixDriver::drawHex(unsigned xoff, unsigned yoff, uint8_t hexValue, uint32_t fore, uint32_t back) const {
    auto pattern = hexChars[hexValue & 0xfu];

    for(int y = 0; y < 7; y++) {
        for(int x = 0; x < 5; x++) {
            if (pattern[y][x] == '#')
                setPixel(x + xoff, y + yoff, (uint8_t *) &fore);
            else
                setPixel(x + xoff, y + yoff, (uint8_t *) &back);
        }
    }
}

#define INP_GPIO(g) *(gpio+((g)/10u)) &= ~(7u<<(((g)%10u)*3u))
//#define OUT_GPIO(g) *(gpio+((g)/10u)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10u))) |= (((a)<=3u?(a)+4u:(a)==4u?3u:2u)<<(((g)%10u)*3u))

void MatrixDriver::initGpio(PeripheralBase peripheralBase) {
    // mmap gpio memory
    auto memfd = open(DEV_MEM, O_RDWR | O_SYNC);
    if(memfd < 0) {
        fprintf(stderr, "failed to open %s", DEV_MEM);
        abort();
    }

    auto gpio = (uint32_t*) mmap(
            nullptr,
            REGISTER_BLOCK_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            memfd,
            peripheralBase + GPIO_REGISTER_OFFSET
    );
    if(gpio == MAP_FAILED) {
        fprintf(stderr, "failed to mmap gpio registers");
        abort();
    }
    close(memfd);

    for(int i = 0; i < 28; i++) {
        INP_GPIO(i);
        SET_GPIO_ALT(i, 2);
    }

    munmap(gpio, REGISTER_BLOCK_SIZE);
}

static void setHeaderRowCode(uint32_t *header, unsigned srow, unsigned pwmRows, RowEncoding::Encoder encoder) {
    setHeaderByte(header[0], (*encoder)(pwmRows, srow, 0));
    header[1] = header[0];
    setHeaderByte(header[2], (*encoder)(pwmRows, srow, 1));
    header[3] = header[2];
}

static void setHeaderPulseWidth(uint32_t *header, unsigned pulseWidth) {
    setHeaderByte(header[0], pulseWidth & 0xffu);
    header[1] = header[0];
    setHeaderByte(header[2], pulseWidth >> 8u);
    header[3] = header[2];
}

// calculate multi-row pwm bit spreading
static unsigned getPwmSpread(unsigned pwmBits, unsigned pwmRows) {
    const unsigned limit = pwmRows - pwmBits + 1;
    unsigned n, z = 0;
    for(n = 0; n < 32; n++) {
        z = (1u << n) - n;
        if(z >= limit) break;
    }
    if(z == limit)
        return n;

    die("pwmRows could not be aligned with pwmBits: %d, %d, %d, %d", pwmRows, pwmBits, limit, z);
}

// create pwm row mappings
static void createPwmMap(unsigned pwmBits, unsigned pwmRows, unsigned *&mapPulseWidth, unsigned *&mapPwmBit) {
    mapPulseWidth = nullptr;
    mapPwmBit = nullptr;

    // quick sanity check
    if(pwmRows < pwmBits)
        die("pwmRows is less than pwmBits: %d < %d", pwmRows, pwmBits);

    const auto n = getPwmSpread(pwmBits, pwmRows);
    mapPwmBit = new unsigned[pwmRows];
    mapPulseWidth = new unsigned[pwmRows];

    // ordinary pwm bits
    const unsigned ordinaryBits = pwmBits - n;
    for(unsigned i = 0; i < pwmBits - n; i++) {
        mapPwmBit[i] = i;
        mapPulseWidth[i] = 1u << i;
    }

    // multi-row pwm bits
    auto b = mapPwmBit + ordinaryBits;
    auto w = mapPulseWidth + ordinaryBits;
    const unsigned width = 1u << ordinaryBits;
    for(unsigned i = 0; i < n; i++) {
        auto cnt = 1u << (unsigned)i;
        for(unsigned j = 0; j < cnt; j++) {
            *(b++) = i + ordinaryBits;
            *(w++) = width;
        }
    }
}

static void setHeaderByte(uint32_t &headerCell, unsigned byte) {
    for(auto offset : mapHeaderBits) {
        headerCell |= (byte & 1u) << offset;
        byte >>= 1u;
    }
}

static void selfTestRGB() {
    unsigned maskRGB = 0;
    for(const auto &p : mapRGB) {
        for(auto b : p) {
            auto mask = 1u << b;
            if(maskRGB & mask)
                die("DPI output bit %03o mapped more than once.", b);
            maskRGB |= mask;
        }
    }

    if(maskRGB != 0xffffffu) {
        die("RGB bitmap contains invalid bit offsets: 0x%08x", maskRGB);
    }
}

static void selfTestHeader() {
    unsigned headerMask = 0;
    for(auto b : mapHeaderBits) {
        auto mask = 1u << b;
        if(headerMask & mask)
            die("header output bit %03o mapped more than once.", b);
        headerMask |= mask;
    }

    if(headerMask != 0xff0000u) {
        die("header bitmap contains invalid bit offsets: 0x%08x", headerMask);
    }
}

// print message and abort
static void die(const char *format, ...) {
    va_list argptr;
    va_start(argptr, format);
    vfprintf(stderr, format, argptr);
    fwrite("\n", 1, 1, stderr);
    fflush(stderr);
    va_end(argptr);
    abort();
}

// interleaved 32 pixel wide panels
void RemapInterleaved32A::remap(unsigned &x, unsigned &y) {
    // determine interleaving offset
    auto offset = (y & 8u) ^ 8u;
    // interleave y-coordinate
    y = (y & 0x7u) | ((y & 0xfffffff0u) >> 1u);
    // interleave x-coordinate: leave lower 5 bits intact, shift upper bits, and add offset
    x = (x & 0x1fu) | ((x & 0x7fffffe0u) << 1u) | (offset << 2u);
}
