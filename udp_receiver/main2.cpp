//
// Created by robert on 8/19/19.
//

#include "MatrixDriver.h"
#include <unistd.h>
#include <iostream>

int main(int argc, char **argv) {
    std::cout << "initializing rgb matrix driver" << std::endl;
    MatrixDriver matrixDriver("/dev/fb0",  5 * 64, 32, 11);

    std::cout << "initialize pwm mapping" << std::endl;
    createPwmLutCie1931(11, 20, matrixDriver.getPwmMapping());

    for(int i = 0; i < 1000; i++) {
        matrixDriver.flipBuffer();
        sleep(1);
    }

    return 0;
}
