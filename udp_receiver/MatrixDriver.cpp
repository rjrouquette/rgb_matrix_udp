//
// Created by robert on 8/15/19.
//

#include "MatrixDriver.h"

#include <SDL/SDL.h>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <sys/mman.h>
#include <sched.h>
#include <cstdarg>

#define PANEL_ROWS (64)
#define PANEL_COLS (64)
#define PWM_BITS (11)
#define PWM_MAX (8)
#define PWM_ROWS (15)

#define ROW_PADDING (32)
#define PANEL_STRING_LENGTH (4)
#define PANEL_STRINGS (4)

// xmega to panel rgb bit mapping
// bit offsets are in octal notation
uint8_t mapRGB[8][3] = {
//       RED  GRN  BLU
        {001, 000, 002}, // string 0, y-plane 0
        {004, 003, 005}, // string 0, y-plane 1
        {011, 010, 012}, // string 1, y-plane 0
        {014, 013, 015}, // string 1, y-plane 1
        {021, 020, 022}, // string 2, y-plane 0
        {024, 023, 025}, // string 2, y-plane 1
        {006, 007, 016}, // string 3, y-plane 0
        {017, 026, 027}  // string 3, y-plane 1
};

MatrixDriver::MatrixDriver() :
        panelRows(PANEL_ROWS), panelCols(PANEL_COLS), scanRowCnt(PANEL_ROWS / 2), pwmBits(PWM_BITS),
        threadOutput{}, mutexBuffer(PTHREAD_MUTEX_INITIALIZER), condBuffer(PTHREAD_COND_INITIALIZER),
        pwmMapping{}
{
    SDL_Init(SDL_INIT_VIDEO);
    auto videoInfo = SDL_GetVideoInfo();

    // create SDL canvas
    screen = SDL_SetVideoMode(
            videoInfo->current_w,
            videoInfo->current_h,
            32,
            SDL_DOUBLEBUF | SDL_FULLSCREEN
    );
    if(screen == nullptr)
        die("SDL_SetVideoMode failed: %d x %d x %d", videoInfo->current_w, videoInfo->current_h, 32);
    if(screen->format->BytesPerPixel != 4)
        die("SDL_Surface is not 32-bit color");

    rowBlock = (panelCols * PANEL_STRING_LENGTH) + ROW_PADDING;
    pwmBlock = rowBlock * PWM_ROWS;

    frameSize = (pwmBlock * scanRowCnt) + 1;
    currFrame = new uint32_t[frameSize];
    nextFrame = new uint32_t[frameSize];

    // display off by default
    bzero(pwmMapping, sizeof(pwmMapping));
    // clear frame buffers
    bzero(currFrame, sizeof(uint32_t) * frameSize);
    bzero(nextFrame, sizeof(uint32_t) * frameSize);

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
    SDL_FreeSurface(screen);
}

void MatrixDriver::flipBuffer() {
    pthread_mutex_lock(&mutexBuffer);

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

    // set can headers
    nextFrame[10] = 0xff0000ffu;
    for(uint8_t r = 0; r < scanRowCnt; r++) {
        for(uint8_t p = 0; p < PWM_ROWS; p++) {
            int row = (r * pwmBits) + p + 1;
            auto header = nextFrame + (row * rowBlock) + 10;
            header[0] = 0xff0000ffu;
            header[2] = 0xff000000u | r;

            uint8_t pw = (p > PWM_MAX) ? PWM_MAX : p;
            header[1] = 0xff000000u | (1u << pw);
        }
    }
}

void MatrixDriver::setPixel(int panel, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if(panel < 0 || panel >= 24) return;
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
    uint16_t R = pwmMapping[r];
    uint16_t G = pwmMapping[g];
    uint16_t B = pwmMapping[b];

    // set pixel bits
    auto pixel = nextFrame + (yoff * pwmBlock) + xoff;
    for(uint8_t i = 0; i < pwmBits; i++) {
        if(R & 1u)  *pixel |= maskRedHi;
        else        *pixel &= maskRedLo;

        if(G & 1u)  *pixel |= maskGrnHi;
        else        *pixel &= maskGrnLo;

        if(B & 1u)  *pixel |= maskBluHi;
        else        *pixel &= maskBluLo;

        R >>= 1u;
        G >>= 1u;
        B >>= 1u;
        pixel += pwmBlock;
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
    SDL_LockSurface(screen);
    auto srow = (uint8_t *) screen->pixels;
    auto prow = currFrame;
    for(int x = 0; x < scanRowCnt; x++) {
        for(int p = 0; p < pwmBits; p++) {
            memcpy(srow, prow, rowBlock * sizeof(uint32_t));
            srow += screen->pitch;
            prow += rowBlock;
        }
    }
    SDL_UnlockSurface(screen);
    SDL_Flip(screen);
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
    for(uint8_t p = 0; p < 24; p++) {
        drawHex(p, 0, 0, p >> 3u, 0xffffff, 0);
        drawHex(p, 6, 0, p & 7u, 0xffffff, 0);
    }
    flipBuffer();
}
