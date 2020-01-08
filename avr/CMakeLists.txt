cmake_minimum_required(VERSION 3.0)

set(CMAKE_FIND_ROOT_PATH /usr/lib/avr/include)

# Use avr-gcc and avr-g++
set(CMAKE_C_COMPILER avr-gcc)
set(CMAKE_CXX_COMPILER avr-g++)

# don't test as they won't work
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

project("rgb_matrix_avr")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -Wall -mmcu=atxmega16a4u -D__AVR_ATxmega16A4U__ -DF_CPU=2000000 -DARCH_XMEGA")

add_executable(
        rgb-matrix-avr

        main.c
        leds.h
        gpio.c
        gpio.h
        matrix.h
)
