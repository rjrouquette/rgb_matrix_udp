//
// Created by robert on 8/15/19.
//

#ifndef UDP_RECEIVER_MATRIXDRIVER_H
#define UDP_RECEIVER_MATRIXDRIVER_H

#include <cstdint>
#include <cstddef>
#include <pthread.h>
#include <SDL/SDL_video.h>

class MatrixDriver {
public:
    MatrixDriver(int panelCols, int panelRows, int pwmBits);
    ~MatrixDriver();

    void flipBuffer();
    void clearFrame();

    void setPixel(int panel, int x, int y, uint8_t r, uint8_t g, uint8_t b);
    void setPixel(int panel, int x, int y, uint8_t *rgb);
    void setPixels(int panel, int &x, int &y, uint8_t *rgb, size_t pixelCount);

    typedef uint16_t pwm_lut[256];
    pwm_lut& getPwmMapping() { return pwmMapping; }

private:
    const int panelRows, panelCols, scanRowCnt, pwmBits;
    size_t pixBlock, rowBlock, pwmBlock;
    bool isRunning;

    size_t frameSize;
    uint32_t *currFrame;
    uint32_t *nextFrame;
    pthread_t threadOutput;
    pthread_mutex_t mutexBuffer;
    pthread_cond_t condBuffer;

    pwm_lut pwmMapping;
    SDL_Surface *screen;

    void blitFrame();
    static void* doRefresh(void *obj);

    static void die(const char *format, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
};

void createPwmLutCie1931(uint8_t bits, float brightness, MatrixDriver::pwm_lut &pwmLut);
void createPwmLutLinear(uint8_t bits, float brightness, MatrixDriver::pwm_lut &pwmLut);

#endif //UDP_RECEIVER_MATRIXDRIVER_H
