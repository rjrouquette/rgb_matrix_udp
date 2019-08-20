//
// Created by robert on 8/19/19.
//

#include "MatrixDriver.h"
#include <cstdlib>
#include <unistd.h>
#include <iostream>

int main(int argc, char **argv) {
    std::cout << "initializing rgb matrix driver" << std::endl;
    MatrixDriver matrixDriver(MatrixDriver::gpio_rpi3, 64, 32, 11);

    std::cout << "initialize pwm mapping" << std::endl;
    createPwmLutCie1931(11, 20, matrixDriver.getPwmMapping());

    for(int i = 0; i < 60; i++) {
        std::cout << "sending frame" << std::endl;
        matrixDriver.flipBuffer();
        sleep(1);
    }

    return 0;
}
