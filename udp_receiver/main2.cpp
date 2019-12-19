//
// Created by robert on 8/19/19.
//

#include "MatrixDriver.h"
#include <unistd.h>
#include <iostream>

long nanotime();

int main(int argc, char **argv) {
    std::cout << "initializing rgb matrix driver" << std::endl;
    MatrixDriver matrixDriver(64, 64, 11);

    std::cout << "initialize pwm mapping" << std::endl;
    createPwmLutCie1931(11, 100, matrixDriver.getPwmMapping());
    //createPwmLutLinear(11, 100, matrixDriver.getPwmMapping());

    sleep(1);
    matrixDriver.enumeratePanels();
    sleep(3);

    /*
    long a, b;
    a = nanotime();
    for(int i = 0; i < 255; i++) {
        usleep(50000);
        matrixDriver.clearFrame();
        for(int y = 0; y < 32; y++) {
            for(int x = 0; x < 64; x++) {
                for(int p = 0; p < 24; p++) {
                    matrixDriver.setPixel(p, x, y, i, i / 2, i / 3);
                }
            }
        }
        matrixDriver.flipBuffer();
        b = nanotime();
        fprintf(stdout, "%9ld ns (%6.2lf Hz)\n", (b-a), 1000000000.0 / (double)(b-a));
        a = b;
        fflush(stdout);
    }*/

    return 0;
}

long nanotime() {
    timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1000000000l + ts.tv_nsec;
}
