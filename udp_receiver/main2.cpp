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

    std::cout << "sending first frame" << std::endl;
    matrixDriver.flipBuffer();

    return 0;
}
