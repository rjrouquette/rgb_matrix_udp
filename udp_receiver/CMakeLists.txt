cmake_minimum_required(VERSION 3.0)
project(udp_receiver)

set(CMAKE_CXX_STANDARD 11)

link_directories(../rpi-rgb-led-matrix/lib)

add_executable(
        udp_receiver
        main.cpp
        logging.cpp
        logging.h
        MatrixDriver.cpp
        MatrixDriver.h
)

target_link_libraries(udp_receiver -lrgbmatrix -pthread)
