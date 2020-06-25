//
// Created by robert on 6/25/20.
//

#include <cstdint>
#include <cstdlib>
#include <sysexits.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "logging.h"

const uint64_t frameMagic = 0xebb29724dd5126acUL;

int connect(const char *unix_socket);
void setPixel(uint8_t *frame, unsigned width, unsigned x, unsigned y, uint8_t r, uint8_t g, uint8_t b);

int main(int argc, char **argv) {
    const unsigned width = atoi(argv[1]);
    const unsigned height = atoi(argv[2]);
    const uint8_t brightness = atoi(argv[3]);
    const size_t frameSize = width * height * 3;

    int sockfd = connect(argv[4]);
    if(sockfd < 0) {
        log("failed to connect to socket");
        return EX_IOERR;
    }

    // generate test pattern
    auto frame = new uint8_t[frameSize];

    // red-green swath
    for(unsigned x = 0; x < 64; x++) {
        for(unsigned y = 0; y < 64; y++) {
            setPixel(frame, width, x, y, x * 4, y * 4, 0);
        }
    }

    // red-blue swath
    for(unsigned x = 0; x < 64; x++) {
        for(unsigned y = 0; y < 64; y++) {
            setPixel(frame, width, x + 64, y, x * 4, 0, y * 4);
        }
    }

    // green-blue swath
    for(unsigned x = 0; x < 64; x++) {
        for(unsigned y = 0; y < 64; y++) {
            setPixel(frame, width, x + 128, y, 0, y * 4, x * 4);
        }
    }

    // grey swath
    for(unsigned x = 0; x < 64; x++) {
        for(unsigned y = 0; y < 64; y++) {
            setPixel(frame, width, x + 192, y, x * 4, x * 4, x * 4);
        }
    }

    // send test pattern
    write(sockfd, &frameMagic, sizeof(frameMagic));
    write(sockfd, &brightness, sizeof(brightness));
    write(sockfd, frame, frameSize);
    pause();
    close(sockfd);
    delete[] frame;
}

int connect(const char *unix_socket) {
    // try until it works
    for(;;) {
        int sockfd = socket(PF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0) {
            usleep(1000);
            continue;
        }

        struct sockaddr_un addr = {};
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, unix_socket, sizeof(addr.sun_path) - 1);

        if (::connect(sockfd, (sockaddr *) &addr, sizeof(struct sockaddr_un)) < 0) {
            close(sockfd);
            usleep(1000);
            continue;
        }

        return sockfd;
    }
}

void setPixel(uint8_t *frame, unsigned width, unsigned x, unsigned y, uint8_t r, uint8_t g, uint8_t b) {
    auto p = frame + ((y * width) + x) * 3;
    p[0] = r;
    p[1] = g;
    p[2] = b;
}
