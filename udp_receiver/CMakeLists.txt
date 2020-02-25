cmake_minimum_required(VERSION 3.0)
project(udp_receiver)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CC_STANDARD 11)

# Use 2011 standard for C and C++
set(CMAKE_CC_FLAGS "${CMAKE_CC_FLAGS} -Wall -ggdb3")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -ggdb3")

#link_directories(../rpi-rgb-led-matrix/lib)

#add_executable(
#        udp_receiver
#        main.cpp
#        logging.cpp
#        logging.h
#)

#target_link_libraries(udp_receiver -lrgbmatrix -pthread)

add_executable(
        udp_receiver2
        main2.cpp
        logging.cpp
        logging.h
        MatrixDriver.cpp
        MatrixDriver.h
        RowEncoding.cpp RowEncoding.h)

target_link_libraries(udp_receiver2 -pthread)
