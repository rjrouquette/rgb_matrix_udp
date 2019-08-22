//
// Created by robert on 8/19/19.
//

#include "MatrixDriver.h"
#include <unistd.h>
#include <iostream>

long nanotime();

int main(int argc, char **argv) {
    std::cout << "initializing rgb matrix driver" << std::endl;
    MatrixDriver matrixDriver("/dev/fb0",  5 * 64, 32, 11);

    std::cout << "initialize pwm mapping" << std::endl;
    createPwmLutCie1931(11, 20, matrixDriver.getPwmMapping());

    long a, b;
    a = nanotime();
    for(int i = 0; i < 1000; i++) {
        matrixDriver.flipBuffer();
        b = nanotime();
        std::cout << (b-a) << " ns (" << 1000000000.0 / (b-a) << " Hz)"  << std::endl;
        a = b;
    }

    return 0;
}

long nanotime() {
    timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

    return ts.tv_sec * 1000000000l + ts.tv_nsec;
}
