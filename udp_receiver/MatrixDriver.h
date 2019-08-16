//
// Created by robert on 8/15/19.
//

#ifndef UDP_RECEIVER_MATRIXDRIVER_H
#define UDP_RECEIVER_MATRIXDRIVER_H

#include <cstdint>
#include <cstddef>
#include <pthread.h>

uint16_t pwmMappingCie1931(uint8_t bits, uint8_t level, uint8_t intensity);
uint16_t pwmMappingStraight(uint8_t bits, uint8_t level, uint8_t intensity);

class MatrixDriver {
public:
    MatrixDriver(int rowLength, int rowsPerPanel, int pwmBits);
    ~MatrixDriver();

    void flipBuffer();

    void clearFrame();
    void setBrightness(uint8_t level);

    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
    void setPixel(int x, int y, uint8_t *rgb);
    void setPixel(int x, int y, uint32_t rgb);

    void setPixels(int &x, int &y, uint8_t *rgb, int pixelCount);

    typedef uint16_t (*pwm_mapping)(uint8_t bits, uint8_t level, uint8_t intensity);
    void setPwmMapping(pwm_mapping mapping);

private:
    uint32_t pixelsPerRow;
    uint32_t rowsPerScan;
    uint8_t pwmBits;
    uint8_t nextBuffer;
    uint8_t brightness;

    uint32_t sizeFrameBuffer;
    uint32_t *frameRaw;
    uint32_t *frameBuffer[2];
    uint32_t *gpioRegister;
    pthread_t threadGpio;

    pwm_mapping pwmMapping;

    void initFrameBuffer(uint32_t *fb);

    void setR0(uint32_t* pixel, uint32_t stride, uint16_t r, uint16_t g, uint16_t b);
    void setR1(uint32_t* pixel, uint32_t stride, uint16_t r, uint16_t g, uint16_t b);
    void setR2(uint32_t* pixel, uint32_t stride, uint16_t r, uint16_t g, uint16_t b);
    void setR3(uint32_t* pixel, uint32_t stride, uint16_t r, uint16_t g, uint16_t b);
};


#endif //UDP_RECEIVER_MATRIXDRIVER_H
