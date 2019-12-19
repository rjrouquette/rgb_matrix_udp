//
// Created by robert on 8/19/19.
//

#include "MatrixDriver.h"
#include <unistd.h>
#include <iostream>

long nanotime();

int main(int argc, char **argv) {
    std::cout << "initializing rgb matrix driver" << std::endl;
    MatrixDriver matrixDriver("/dev/fb0", "/dev/tty1",  5 * 64, 32, 11);

    std::cout << "initialize pwm mapping" << std::endl;
    //createPwmLutCie1931(11, 20, matrixDriver.getPwmMapping());
    createPwmLutLinear(11, 100, matrixDriver.getPwmMapping());

    long a, b;
    a = nanotime();
    for(int i = 0; i < 10000; i++) {
        matrixDriver.clearFrame();
        matrixDriver.setPixel(0, 0, 0, 255, 255, 255);
        matrixDriver.flipBuffer();
        b = nanotime();
        fprintf(stdout, "%9ld ns (%6.2lf Hz)\n", (b-a), 1000000000.0 / (double)(b-a));
        a = b;
        fflush(stdout);
    }

    return 0;
}

long nanotime() {
    timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

    return ts.tv_sec * 1000000000l + ts.tv_nsec;
}
