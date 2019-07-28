#include <unistd.h>
#include <cstdlib>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sysexits.h>
#include <pthread.h>
#include <csignal>
#include <cstring>
#include <sys/time.h>

#include "logging.h"

#define UNUSED __attribute__((unused))

struct frame_packet {
    uint32_t frameId;
    uint32_t subFrameId;
    uint64_t targetEpochUs;
    uint8_t pixelData[1024];
};

struct remote_panel {
    sockaddr_in addr;
    int xoff, yoff;
    int width, height;
};

bool isRunning = true;
int width = 64;
int height = 32;

uint8_t pixBuffAct[16384];

pthread_t threadUdpTx;
void * doUdpTx(void *obj);
void sendFrame(int socketUdp, uint32_t frameId, uint64_t frameEpoch, const remote_panel &rp);

long microtime();
void sig_ignore(UNUSED int sig) { }

void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *pixel = &(pixBuffAct[(y * width + x) * 3]);
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
}

int main(int argc, char **argv) {
    struct sigaction act = {};
    memset(&act, 0, sizeof(act));
    act.sa_handler = sig_ignore;
    sigaction(SIGUSR1, &act, nullptr);

    if(argc != 1) {
        log("Usage: udp-rgb-matrix");
        return EX_USAGE;
    }

    // start udp rx thread
    log("start udp tx thread");
    pthread_create(&threadUdpTx, nullptr, doUdpTx, nullptr);

    setPixel(0, 0, 255, 0, 0);
    setPixel(63, 0, 0, 255, 0);
    setPixel(0, 31, 0, 0, 255);
    setPixel(63, 31, 255, 255, 255);

    pause();

    return 0;
}

long microtime() {
    struct timeval now = {};
    gettimeofday(&now, nullptr);
    return now.tv_sec * 1000000l + now.tv_usec;
}

void * doUdpTx(UNUSED void *obj) {
    int socketUdp = socket(AF_INET, SOCK_DGRAM, 0);
    uint32_t frameId = 1;

    remote_panel test = {};
    bzero(&test, sizeof(test));
    test.addr.sin_port = htons(1234);
    test.addr.sin_addr.s_addr = htonl(0xc0a80319); // 192.168.3.25
    test.addr.sin_family = AF_INET;
    test.width = 64;
    test.height = 32;

    while(isRunning) {
        sendFrame(socketUdp, frameId++, microtime() + 100000, test);
        if(frameId == 0) frameId = 1;
        usleep(20000);
    }

    close(socketUdp);
    return nullptr;
}

void sendFrame(int socketUdp, uint32_t frameId, uint64_t frameEpoch, const remote_panel &rp) {
    int rowAdvance = (width - rp.width) * 3;
    int packOff = 0;

    frame_packet packet = {};
    packet.frameId = frameId;
    packet.targetEpochUs = frameEpoch;
    packet.subFrameId = 0;

    uint8_t pixOff = (rp.yoff * width + rp.xoff) * 3;
    for(int y = 0; y < rp.height; y++) {
        for(int x = 0; x < rp.width; x++) {
            for(int c = 0; c < 3; c++) {
                packet.pixelData[packOff++] = pixBuffAct[pixOff++];

                if (packOff == sizeof(packet.pixelData)) {
                    sendto(socketUdp, &packet, sizeof(packet), 0, (sockaddr *) &rp.addr, sizeof(rp.addr));
                    packOff = 0;
                    packet.subFrameId++;
                }
            }
        }
        pixOff += rowAdvance;
    }

    if(packOff != 0) {
        sendto(socketUdp, &packet, sizeof(packet), 0, (sockaddr *) &rp.addr, sizeof(rp.addr));
    }
}
