//
// Created by robert on 8/15/19.
//

#ifndef UDP_RECEIVER_MATRIXDRIVER_H
#define UDP_RECEIVER_MATRIXDRIVER_H

#include <cstdint>
#include <cstddef>
#include <pthread.h>
#include <linux/fb.h>

class PixelMapping {
public:
    PixelMapping() = default;
    virtual ~PixelMapping() = default;

    virtual void remap(unsigned &x, unsigned &y);
};

class MatrixDriver : public PixelMapping {
public:
    enum RowFormat {
        HUB75,          // standard HUB75 4-bit row address
        HUB75E,         // extended HUB75 5-bit row address
        QIANGLI_Q3F32,  // shift register row selection
        HUB75R,         // reduced HUB75 3-bit row address
    };

    static MatrixDriver * createInstance(unsigned pwmBits, RowFormat rowFormat);
    ~MatrixDriver() override;

    void flipBuffer();
    void clearFrame() const;

    void setPixel(unsigned x, unsigned y, uint8_t r, uint8_t g, uint8_t b) const;
    void setPixel(unsigned x, unsigned y, uint8_t *rgb) const;
    void setPixels(unsigned &x, unsigned &y, uint8_t *rgb, size_t pixelCount) const;
    void drawHex(unsigned xoff, unsigned yoff, uint8_t hexValue, uint32_t rgbFore, uint32_t rgbBack) const;

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
    MatrixDriver(
            unsigned scanRowCnt,
            unsigned pwmRows,
            const unsigned *mapPwmBit,
            size_t rowBlock,
            size_t pwmBlock
    );

    const unsigned matrixWidth, matrixHeight, scanRowCnt, pwmRows, *mapPwmBit;
    const size_t rowBlock, pwmBlock;
    PixelMapping *pixelMapping;
    bool isRunning;

    size_t frameSize;
    uint8_t *frameRaw;
    uint32_t *currFrame;
    uint32_t *nextFrame;
    uint32_t *frameHeader;
    pthread_t threadOutput;
    pthread_mutex_t mutexBuffer;

    pwm_lut pwmMapping;

    // frame buffer
    int ttyfd;
    int fbfd;
    fb_fix_screeninfo finfo;
    fb_var_screeninfo vinfo;

    void start();
    void stop();
    static void* doRefresh(void *obj);

public:
    unsigned getWidth() const { return matrixWidth; }
    unsigned getHeight() const { return matrixHeight; }

    // allows for custom arrangement of panels
    void setPixelMapping(PixelMapping *pixelMap) { pixelMapping = pixelMap; }

};

void createPwmLutCie1931(uint8_t bits, float brightness, MatrixDriver::pwm_lut &pwmLut);
void createPwmLutLinear(uint8_t bits, float brightness, MatrixDriver::pwm_lut &pwmLut);

// perform row interleaving for 32 pixel wide panels
class RemapInterleaved32A  : public PixelMapping {
public:
    RemapInterleaved32A() = default;
    ~RemapInterleaved32A() override = default;

    void remap(unsigned &x, unsigned &y) override;
};

#endif //UDP_RECEIVER_MATRIXDRIVER_H
