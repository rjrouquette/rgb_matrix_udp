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
    MatrixDriver();
    ~MatrixDriver();

    void flipBuffer();
    void clearFrame();

    void setPixel(int panel, int x, int y, uint8_t r, uint8_t g, uint8_t b);
    void setPixel(int panel, int x, int y, uint8_t *rgb);
    void setPixels(int &panel, int &x, int &y, uint8_t *rgb, size_t pixelCount);
    void drawHex(int panel, int xoff, int yoff, uint8_t hexValue, uint32_t rgbFore, uint32_t rgbBack);
    void enumeratePanels();
    void testPattern();

    typedef uint16_t pwm_lut[256];
    pwm_lut& getPwmMapping() { return pwmMapping; }

    enum PeripheralBase : uint32_t {
        gpio_rpi1 = 0x20000000, // BCM2708
        gpio_rpi2 = 0x3F000000, // BCM2709
        gpio_rpi3 = 0x3F000000, // BCM2709
        gpio_rpi4 = 0xFE000000, // BCM2711
    };
    static void initGpio(PeripheralBase peripheralBase);

private:
    const int panelRows, panelCols, scanRowCnt, pwmBits;
    size_t rowBlock, pwmBlock;
    int testIndex;
    uint8_t currOffset;
    bool isRunning;

    size_t frameSize;
    uint8_t *frameRaw;
    uint32_t *currFrame;
    uint32_t *nextFrame;
    pthread_t threadOutput;
    pthread_mutex_t mutexBuffer;
    pthread_cond_t condBuffer;

    pwm_lut pwmMapping;

    // frame buffer
    int fbfd;
    fb_fix_screeninfo finfo;
    fb_var_screeninfo vinfo;

    void blitFrame();
    static void* doRefresh(void *obj);

    static void die(const char *format, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
};

void createPwmLutCie1931(uint8_t bits, float brightness, MatrixDriver::pwm_lut &pwmLut);
void createPwmLutLinear(uint8_t bits, float brightness, MatrixDriver::pwm_lut &pwmLut);

#endif //UDP_RECEIVER_MATRIXDRIVER_H
