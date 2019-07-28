cmake_minimum_required(VERSION 3.0)
project(udp_transmitter)

set(CMAKE_CXX_STANDARD 11)

link_directories(../rpi-rgb-led-matrix/lib)

add_executable(
        udp_transmitter
        main.cpp
        logging.cpp
        logging.h
)

target_link_libraries(udp_transmitter -pthread)
