//
// Created by robert on 8/15/19.
//

#include "MatrixDriver.h"

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

#define FB_WIDTH (1632)
#define FB_HEIGHT (241)
#define FB_DEPTH (32)

#define PANEL_ROWS (32)
#define PANEL_COLS (64)
#define PWM_BITS (11)
#define PWM_ROWS (15)

#define HEADER_OFFSET (2)
#define ROW_PADDING (32)
#define PANEL_STRING_LENGTH (25)

// xmega to panel rgb bit mapping
// bit offsets are in octal notation
uint8_t mapRGB[8][3] = {
//       RED  GRN  BLU
        {027, 015, 016}, // p0r0 -> r7, p0g0 -> g5, p0b0 -> g6
        {013, 000, 012}, // p0r1 -> g3, p0g1 -> b0, p0b1 -> g2
        {015, 025, 006}, // p1r0 -> b5, p1g0 -> r5, p1b0 -> b6
        {024, 022, 023}, // p1r1 -> r4, p1g1 -> r2, p1b1 -> r3
        {011, 014, 017}, // p2r0 -> g1, p2g0 -> g4, p2b0 -> g7
        {026, 020, 021}, // p2r1 -> r6, p2g1 -> r0, p2b1 -> r1
        {007, 004, 003}, // p3r0 -> b7, p3g0 -> b4, p3b0 -> b3
        {010, 001, 002}, // p3r1 -> g0, p3g1 -> b1, p3b1 -> b2
};
/*
unsigned mapPulseWidth[PWM_ROWS] = {
        0x0001u, 0x0002u, 0x0004u, 0x0008u,
        0x0010u, 0x0020u, 0x0040u, 0x0080u,
        0x0100u, 0x0100u, 0x0100u, 0x0100u,
        0x0100u, 0x0100u, 0x0100u
};
*/

unsigned mapPulseWidth[PWM_ROWS] = {
        0x0001u * 6, 0x0002u * 6, 0x0004u * 6, 0x0008u * 6,
        0x0010u * 6, 0x0020u * 6, 0x0040u * 6, 0x0080u * 6,
        0x0100u * 6, 0x0100u * 6, 0x0100u * 6, 0x0100u * 6,
        0x0100u * 6, 0x0100u * 6, 0x0100u * 6
};

unsigned mapPwmBit[PWM_ROWS] = {
         0,  1,  2,  3,
         4,  5,  6,  7,
         8,  9,  9, 10,
        10, 10, 10
};

MatrixDriver::MatrixDriver(RowEncoder encoder) :
        panelRows(PANEL_ROWS), panelCols(PANEL_COLS), scanRowCnt(PANEL_ROWS / 2), pwmBits(PWM_BITS), pwmRows(PWM_ROWS),
        rowEncoder(encoder), threadOutput{}, mutexBuffer(PTHREAD_MUTEX_INITIALIZER),
        condBuffer(PTHREAD_COND_INITIALIZER), pwmMapping{}, finfo{}, vinfo{}
{
    auto ttyfd = open(DEV_TTY, O_RDWR);
    if(ttyfd < 0)
        die("failed to open tty device: %s", DEV_TTY);
    if(ioctl(ttyfd, KDSETMODE, KD_GRAPHICS) != 0)
        die("failed to set %s to graphics mode: %s", DEV_TTY, strerror(errno));

    fbfd = open(DEV_FB, O_RDWR);
    if(fbfd < 0)
        die("failed to open fb device: %s", DEV_FB);

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

    // determine block sizes
    rowBlock = finfo.line_length / sizeof(uint32_t);
    pwmBlock = rowBlock * PWM_ROWS;
    if(rowBlock * sizeof(uint32_t) != finfo.line_length)
        die("row size is not integer multiple of uint32_t: %d", finfo.line_length);

    // configure frame pointers
    currOffset = 0;
    frameSize = vinfo.yres * rowBlock;
    //currFrame = ((uint32_t *) frameRaw) + frameSize;
    //nextFrame = ((uint32_t *) frameRaw) + frameSize * 2;
    currFrame = new uint32_t[frameSize];
    nextFrame = new uint32_t[frameSize];

    frameHeader = new uint32_t[(PWM_ROWS * scanRowCnt + 1) * ROW_PADDING];
    // clear header cells
    for(size_t i = 0; i < (PWM_ROWS * scanRowCnt + 1) * ROW_PADDING; i++) {
        frameHeader[i] = 0xff000000u;
    }

    // set scan row headers
    unsigned srow = 0;
    auto header = frameHeader + ROW_PADDING + HEADER_OFFSET;
    setHeaderRowCode(header, srow++);
    for(unsigned r = 0; r < scanRowCnt; r++) {
        for (unsigned p = 0; p < pwmRows; p++) {
            setHeaderRowCode(header, srow++);
            setHeaderPulseWidth(header + 4, mapPulseWidth[p]);
            header += ROW_PADDING;
        }
    }

    printf("pixels: %d\n", vinfo.yres * vinfo.xres);
    printf("frame size: %ld\n", frameSize);
    printf("left margin: %d\n", vinfo.left_margin);
    printf("right margin: %d\n", vinfo.right_margin);
    printf("x offset: %d\n", vinfo.xoffset);
    printf("x virt res: %d\n", vinfo.xres_virtual);
    printf("row block: %ld\n", rowBlock);
    printf("pwm block: %ld\n", pwmBlock);

    // display off by default
    bzero(pwmMapping, sizeof(pwmMapping));

    // clear frame buffers
    clearFrame();
    flipBuffer();
    clearFrame();
    flipBuffer();

    // start output thread
    isRunning = true;
    pthread_create(&threadOutput, nullptr, doRefresh, this);
}

MatrixDriver::~MatrixDriver() {
    clearFrame();
    flipBuffer();

    // stop output thread
    isRunning = false;
    pthread_join(threadOutput, nullptr);

    delete[] currFrame;
    delete[] nextFrame;
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
    for(size_t i = 0; i < frameSize; i++) {
        nextFrame[i] = 0xff000000u;
    }

    // set row headers
    size_t rcnt = (PWM_ROWS * scanRowCnt) + 1;
    auto header = frameHeader;
    auto row = nextFrame;
    for(size_t r = 0; r < rcnt; r++) {
        memcpy(row, header, ROW_PADDING * sizeof(uint32_t));
        header += ROW_PADDING;
        row += rowBlock;
    }
}

void MatrixDriver::setPixel(int panel, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if(panel < 0 || panel >= 100) return;
    if(x < 0 || y < 0) return;
    if(x >= panelCols || y >= panelRows) return;

    // compute pixel offset
    const auto yoff = y % scanRowCnt;
    const auto xoff = x + ((panel % PANEL_STRING_LENGTH) * panelCols);

    const auto rgbOff = ((panel / PANEL_STRING_LENGTH) * 2) + (y / scanRowCnt);
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
    auto pixel = nextFrame + (yoff * pwmBlock) + ROW_PADDING + xoff;
    for(auto bit : mapPwmBit) {
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

void MatrixDriver::setPixel(int panel, int x, int y, uint8_t *rgb) {
    setPixel(panel, x, y, rgb[0], rgb[1], rgb[2]);
}

void MatrixDriver::setPixels(int &panel, int &x, int &y, uint8_t *rgb, size_t pixelCount) {
    for(size_t i = 0; i < pixelCount; i++) {
        setPixel(panel, x, y, rgb);
        rgb += 3;
        if(++x >= panelCols) {
            x = 0;
            if(++y >= panelRows) {
                y = 0;
                ++panel;
            }
        }
    }
}

void MatrixDriver::blitFrame() {
    //fb_var_screeninfo temp = vinfo;
    //temp.yoffset = (currOffset + 1) * vinfo.yres;
    //temp.xoffset = 0;

    //if(ioctl(fbfd, FBIOPAN_DISPLAY, &temp) != 0)
    //    die("failed to pan frame buffer: %s", strerror(errno));

    //if(ioctl(fbfd, FBIO_WAITFORVSYNC, nullptr) != 0)
    //    die("failed to wait for vsync: %s", strerror(errno));

    memcpy(frameRaw, currFrame, frameSize * sizeof(uint32_t));
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
    pthread_mutex_lock(&ctx.mutexBuffer);
    while(ctx.isRunning) {
        pthread_cond_wait(&ctx.condBuffer, &ctx.mutexBuffer);
        ctx.blitFrame();
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

void MatrixDriver::drawHex(int panel, int xoff, int yoff, uint8_t hexValue, uint32_t fore, uint32_t back) {
    auto pattern = hexChars[hexValue & 0xfu];

    for(int y = 0; y < 7; y++) {
        for(int x = 0; x < 5; x++) {
            if (pattern[y][x] == '#')
                setPixel(panel, x + xoff, y + yoff, (uint8_t *) &fore);
            else
                setPixel(panel, x + xoff, y + yoff, (uint8_t *) &back);
        }
    }
}

void MatrixDriver::enumeratePanels() {
    clearFrame();
    for(uint8_t p = 0; p < 100; p++) {
        drawHex(p, 0, 0, p >> 3u, 0xffffff, 0);
        drawHex(p, 6, 0, p & 7u, 0xffffff, 0);
    }
    flipBuffer();
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

unsigned MatrixDriver::mangleRowBits(unsigned rowCode) {
    return ((rowCode & 0xfu) << 1u) | ((rowCode >> 4u) & 0x1u);
}

void MatrixDriver::setHeaderRowCode(uint32_t *header, const unsigned srow) const {
    header[0] |= mangleRowBits((*rowEncoder)(*this, srow, 0)) << 19u;
    header[1] = header[0];
    header[2] |= mangleRowBits((*rowEncoder)(*this, srow, 1)) << 19u;
    header[3] = header[2];
}

void MatrixDriver::setHeaderPulseWidth(uint32_t *header, unsigned pulseWidth) {
    header[0] |= (pulseWidth & 0xffu) << 16u;
    header[1] = header[0];
    header[2] |= (pulseWidth >> 8u) << 16u;
    header[3] = header[2];
}

/*
 * Qiangli Q3E
 * A => CLK
 * B => DATA
 * C => BLANK
 * D => EN0
 * E => EN1
 */

unsigned MatrixDriver::RowEncoder_Qiangli32(const MatrixDriver &matrixDriver, unsigned srow, unsigned idx) {
    const auto pwmRows = matrixDriver.pwmRows;
    auto row = srow / pwmRows;
    auto step = srow % pwmRows;

    unsigned code = 0x05u;

    // shift in set bit for row block
    if((row % 8) == 0)
        code |= 0x02u;

    // perform clock transition
    if(step == 1 && idx == 1)
        code &= 0xfeu;

    // chip selects
    if(srow > (0 * pwmRows)  && srow <= (8 * pwmRows))
        code |= 0x00u;
    else if(srow > (8 * pwmRows)  && srow <= (16 * pwmRows))
        code |= 0x08u;
    else if(srow > (16 * pwmRows)  && srow <= (24 * pwmRows))
        code |= 0x10u;
    else if(srow > (24 * pwmRows)  && srow <= (32 * pwmRows))
        code |= 0x18u;

    return code;
}

unsigned MatrixDriver::RowEncoder_Adafruit16(const MatrixDriver &matrixDriver, unsigned srow, unsigned idx) {
    auto row = (srow - 1) / matrixDriver.pwmRows;
    return (row) & 0x0fu;
}

unsigned MatrixDriver::RowEncoder_Adafruit32(const MatrixDriver &matrixDriver, unsigned srow, unsigned idx) {
    auto row = (srow - 1) / matrixDriver.pwmRows;
    return (~row) & 0x1fu;
}
