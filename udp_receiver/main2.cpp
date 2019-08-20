//
// Created by robert on 8/19/19.
//

#include "MatrixDriver.h"
#include <cstdlib>
#include <unistd.h>

int main(int argc, char **argv) {
    MatrixDriver matrixDriver(MatrixDriver::gpio_rpi3, 64, 64, 11);

    matrixDriver.flipBuffer();

    return 0;
}
