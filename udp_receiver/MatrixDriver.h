//
// Created by robert on 8/15/19.
//

#ifndef UDP_RECEIVER_MATRIXDRIVER_H
#define UDP_RECEIVER_MATRIXDRIVER_H

#include <cstdint>
#include <cstddef>
#include <pthread.h>
#include <linux/fb.h>

class MatrixDriver {
public:
    MatrixDriver(const char *fbDev, const char *ttyDev, int pixelsPerRow, int rowsPerScan, int pwmBits);
    ~MatrixDriver();

    void flipBuffer();

    void clearFrame();

    void setPixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);
    void setPixel(uint32_t x, uint32_t y, uint8_t *rgb);
    void setPixels(uint32_t &x, uint32_t &y, uint8_t *rgb, size_t pixelCount);

    typedef uint16_t pwm_lut[256];
    pwm_lut& getPwmMapping() { return pwmMapping; }

private:
    uint32_t pixelsPerRow;
    uint32_t rowsPerScan;
    uint8_t pwmBits;
    uint8_t currOffset;
    bool isRunning;

    size_t frameSize;
    uint8_t *frameRaw;
    uint32_t *currFrame;
    uint32_t *nextFrame;
    pthread_t threadGpio;
    pthread_mutex_t mutexBuffer;
    pthread_cond_t condBuffer;

    pwm_lut pwmMapping;

    // frame buffer
    int fbfd;
    fb_fix_screeninfo finfo;
    fb_var_screeninfo vinfo;

    // frame header
    // 2 cells for SRAM write command
    // 8 cells for xmega config
    uint32_t frameHeader[10];
    void initFrameHeader();

    void sendFrame(const uint32_t *fb);
    size_t translateOffset(size_t off);

    static void* doRefresh(void *obj);

    static void die(const char *format, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
};

void createPwmLutCie1931(uint8_t bits, float brightness, MatrixDriver::pwm_lut &pwmLut);
void createPwmLutLinear(uint8_t bits, float brightness, MatrixDriver::pwm_lut &pwmLut);

#endif //UDP_RECEIVER_MATRIXDRIVER_H
