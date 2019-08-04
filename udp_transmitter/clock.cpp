//
// Created by robert on 7/28/19.
//

#include <cstdlib>
#include <unistd.h>
#include <ctime>
#include <cstdint>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <iostream>
#include <algorithm>
#include <sysexits.h>
#include <sys/time.h>

const uint64_t frameMagic = 0xebb29724dd5126acUL;

int width = 256;
int height = 64;
int frameSize = width * height * 3;

uint8_t frame[256 * 64 * 3];

long microtime();
void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void drawHex(int xoff, int yoff, uint8_t value);
void drawColon(int xoff, int yoff);
void drawTime(int xoff, int yoff);
void sendFrame(int sockOutput, uint8_t brightness, uint8_t *frame, size_t fsize);

ssize_t writeFully(int sockfd, const void* buffer, size_t len, size_t block = 65536);

int main(int argc, char **argv) {
    struct sockaddr_un addr = {};
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "rgb-matrix.sock", sizeof(addr.sun_path)-1);

    const int sockOutput = socket(PF_UNIX, SOCK_STREAM, 0);
    if(connect(sockOutput, (sockaddr *) &addr, sizeof(addr)) < 0) {
        std::cout << "failed to open unix domain socket: " << strerror(errno) << std::endl;
        return EX_IOERR;
    }

    int cnt = 0, xoff = 0, yoff = 0;
    for(;;) {
        if(cnt % 100 == 0) {
            xoff = rand() % (width - 71);
            yoff = rand() % (height - 16);
        }
        drawTime(xoff, yoff);
        sendFrame(sockOutput, 100, frame, frameSize);
        usleep(100000);
        cnt++;
    }

    close(sockOutput);
    return 0;
}

long microtime() {
    struct timeval now = {};
    gettimeofday(&now, nullptr);
    return now.tv_sec * 1000000l + now.tv_usec;
}

void sendFrame(int sockOutput, uint8_t brightness, uint8_t *frame, size_t fsize) {
    writeFully(sockOutput, &frameMagic, sizeof(frameMagic));
    writeFully(sockOutput, &brightness, sizeof(brightness));
    writeFully(sockOutput, frame, fsize);
}

void drawTime(int xoff, int yoff) {
    memset(frame, 255, frameSize);

    time_t epoch = time(nullptr);
    struct tm now = {};
    localtime_r(&epoch, &now);

    bool colon = (microtime() % 1000000) >= 500000;

    drawHex(xoff + 0, yoff + 0, now.tm_hour / 10);
    drawHex(xoff + 11, yoff + 0, now.tm_hour % 10);

    if(colon) drawColon(xoff + 22, yoff + 0);

    drawHex(xoff + 25, yoff + 0, now.tm_min / 10);
    drawHex(xoff + 36, yoff + 0, now.tm_min % 10);

    if(colon) drawColon(xoff + 47, yoff + 0);

    drawHex(xoff + 50, yoff + 0, now.tm_sec / 10);
    drawHex(xoff + 61, yoff + 0, now.tm_sec % 10);
}

void drawColon(int xoff, int yoff) {
    for(int y = 0; y < 2; y++) {
        for(int x = 0; x < 2; x++) {
            setPixel(x + xoff, y + yoff + 3, 0, 0, 0);
            setPixel(x + xoff, y + yoff + 11, 0, 0, 0);
        }
    }
}

void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *pixel = &(frame[(y * width + x) * 3]);
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
}

char hexChars[16][16][11] = {
        {
                "   ####   ",
                "  ######  ",
                " ###  ### ",
                "###    ###",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "###    ###",
                " ###  ### ",
                "  ######  ",
                "   ####   "
        },{
                "    ##    ",
                "   ###    ",
                "  ####    ",
                " #####    ",
                "### ##    ",
                "##  ##    ",
                "    ##    ",
                "    ##    ",
                "    ##    ",
                "    ##    ",
                "    ##    ",
                "    ##    ",
                "    ##    ",
                "    ##    ",
                "##########",
                "##########"
        },{
                "   ####   ",
                "  ######  ",
                " ###  ### ",
                "###    ###",
                "##      ##",
                "##      ##",
                "       ###",
                "      ### ",
                "     ###  ",
                "    ###   ",
                "   ###    ",
                "  ###     ",
                " ###      ",
                "###       ",
                "##########",
                "##########"
        },{
                "   ####   ",
                "  ######  ",
                " ###  ### ",
                "###    ###",
                "##      ##",
                "##     ###",
                "      ### ",
                "   #####  ",
                "   #####  ",
                "      ### ",
                "##     ###",
                "##      ##",
                "###    ###",
                " ###  ### ",
                "  ######  ",
                "   ####   "
        },{
                "      ##  ",
                "     ###  ",
                "    ####  ",
                "   #####  ",
                "  ### ##  ",
                " ###  ##  ",
                "###   ##  ",
                "##########",
                "##########",
                "      ##  ",
                "      ##  ",
                "      ##  ",
                "      ##  ",
                "      ##  ",
                "      ##  ",
                "      ##  "
        },{
                "##########",
                "##########",
                "##        ",
                "##        ",
                "##        ",
                "#######   ",
                "########  ",
                "      ### ",
                "       ###",
                "        ##",
                "        ##",
                "        ##",
                "###    ###",
                " ###  ### ",
                "  ######  ",
                "   ####   "
        },{
                "   ####   ",
                "  ######  ",
                " ###  ### ",
                "###    ###",
                "##        ",
                "##        ",
                "## ####   ",
                "########  ",
                "####  ### ",
                "###    ###",
                "##      ##",
                "##      ##",
                "###    ###",
                " ###  ### ",
                "  ######  ",
                "   ####   "
        },{
                "##########",
                "##########",
                "        ##",
                "       ###",
                "      ### ",
                "     ###  ",
                "    ###   ",
                "    ##    ",
                "   ##     ",
                "   ##     ",
                "  ##      ",
                "  ##      ",
                " ##       ",
                " ##       ",
                "##        ",
                "##        ",
        },{
                "   ####   ",
                "  ######  ",
                " ###  ### ",
                "###    ###",
                "##      ##",
                "###    ###",
                " ###  ### ",
                "  ######  ",
                "  ######  ",
                " ###  ### ",
                "###    ###",
                "##      ##",
                "###    ###",
                " ###  ### ",
                "  ######  ",
                "   ####   "
        },{
                "   ####   ",
                "  ######  ",
                " ###  ### ",
                "###    ###",
                "##      ##",
                "##      ##",
                "###    ###",
                " ###  ####",
                "  ########",
                "   #### ##",
                "        ##",
                "        ##",
                "###    ###",
                " ###  ### ",
                "  ######  ",
                "   ####   "
        },{
                "   ####   ",
                "  ######  ",
                " ###  ### ",
                "###    ###",
                "##      ##",
                "##      ##",
                "##      ##",
                "##########",
                "##########",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##"
        },{
                "#######   ",
                "########  ",
                "##    ### ",
                "##     ###",
                "##      ##",
                "##     ###",
                "##    ### ",
                "########  ",
                "########  ",
                "##    ### ",
                "##     ###",
                "##      ##",
                "##     ###",
                "##    ### ",
                "########  ",
                "#######   "
        },{
                "   ####   ",
                "  ######  ",
                " ###  ### ",
                "###    ###",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "###    ###",
                " ###  ### ",
                "  ######  ",
                "   ####   "
        },{
                "#######   ",
                "########  ",
                "##    ### ",
                "##     ###",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##      ##",
                "##     ###",
                "##    ### ",
                "########  ",
                "#######   "
        },{
                "##########",
                "##########",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "########  ",
                "########  ",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "##########",
                "##########"
        },{
                "##########",
                "##########",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "########  ",
                "########  ",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "##        ",
                "##        "
        }
};

void drawHex(int xoff, int yoff, uint8_t value) {
    auto pattern = hexChars[value & 0xfu];

    int x = 0;
    for(int y = 0; y < 16; y++) {
        for(x = 0; x < 11; x++) {
            if (pattern[y][x] == '#')
                setPixel(x + xoff, y + yoff, 0, 0, 0);
            else
                setPixel(x + xoff, y + yoff, 255, 255, 255);
        }
    }
}

ssize_t writeFully(int sockfd, const void* buffer, size_t len, size_t block) {
    ssize_t n, l;
    l = 0;
    while(l < (ssize_t)len) {
        n = ::send(sockfd, ((char*)buffer)+l, std::min(len-l, block), 0);
        if(n < 0) {
            if(errno == EAGAIN) {
                usleep(1000);
            }
            else {
                return n;
            }
        }
        else if(n == 0) {
            return l;
        }
        else {
            l += n;
        }
    }

    return l;
}
