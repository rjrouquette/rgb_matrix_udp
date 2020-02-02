#include <unistd.h>
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
#include "MatrixDriver.h"

#define UNUSED __attribute__((unused))

#define PANEL_COUNT (16)
#define PANEL_WIDTH (64)
#define PANEL_HEIGHT (64)
#define PWM_BITS (11)

#define UDP_PORT (1234)
#define UDP_BUFFER_SIZE (2048)
#define RECVMMSG_CNT (64)

#define FRAME_MASK (0x0fu)      // 16 frame circular buffer
#define SUBFRAME_MASK (0xffu)   // 256 sub-frames per frame
#define SUBFRAME_PIXELS (400)   // 400 pixels per sub-frame
#define SUBFRAME_MATRIX ((PANEL_COUNT * PANEL_WIDTH * PANEL_WIDTH + SUBFRAME_PIXELS - 1) / SUBFRAME_PIXELS)

bool isRunning = true;
int socketUdp = -1;
int height = 0;
int width = 0;

struct frame_packet {
    uint32_t frameId;
    uint32_t subFrameId;
    uint64_t targetEpochUs;
    uint8_t pixelData[SUBFRAME_PIXELS * 3];
};

frame_packet packetBuffer[FRAME_MASK + 1][SUBFRAME_MASK + 1];
pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

uint8_t brightness = 20;
uint8_t currBrightness = 20;
MatrixDriver *matrix;

pthread_t threadUdpRx;
void * doUdpRx(void *obj);
long microtime();
void sig_ignore(UNUSED int sig) { }

int main(int argc, char **argv) {
    struct sigaction act = {};
    memset(&act, 0, sizeof(act));
    act.sa_handler = sig_ignore;
    sigaction(SIGUSR1, &act, nullptr);

    // set cpu affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

    const uint32_t fsize = height * width * 3;
    const uint32_t fmax = ((fsize + sizeof(frame_packet::pixelData) - 1) / sizeof(frame_packet::pixelData));
    if(fmax > (SUBFRAME_MASK + 1)) {
        log("panel dimensions requires too many sub frames: %d x %d -> %d sub frames", height, width, fmax);
        return EX_CONFIG;
    }

    struct sockaddr_in serv_addr = {};
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(UDP_PORT);

    socketUdp = socket(AF_INET, SOCK_DGRAM, 0);

    // get eth0 address
    char ethAddr[32];
    char ethAddrHex[16];
    struct ifreq ifr = {};
    bzero(&ifr, sizeof(ifr));
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
    ioctl(socketUdp, SIOCGIFADDR, &ifr);
    strcpy(ethAddr, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    sprintf(ethAddrHex, "%08X", ntohl(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr));
    log("eth0 ip: %s", ethAddr);

    if (bind(socketUdp, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        log("failed to bind to port %d", UDP_PORT);
        return EX_OSERR;
    }
    log("listening on port %d", UDP_PORT);

    // clear packet buffer
    bzero(packetBuffer, sizeof(packetBuffer));

    // start udp rx thread
    log("start udp rx thread");
    pthread_create(&threadUdpRx, nullptr, doUdpRx, nullptr);

    // configure rgb matrix panel driver
    matrix = new MatrixDriver();
    createPwmLutCie1931(PWM_BITS, brightness, matrix->getPwmMapping());
    log("instantiated matrix driver");

    usleep(250000);
    matrix->enumeratePanels();
    sleep(3);

    log("waiting for frames");
    uint32_t startOffset = 0;
    // main processing loop
    while(isRunning) {
        bool inActive = true;
        uint64_t now = microtime();
        pthread_rwlock_wrlock(&rwlock);
        for(uint32_t f = 0; f <= FRAME_MASK; f++) {
            auto frame = packetBuffer[(f + startOffset) & FRAME_MASK];

            // look for pending frames
            if(frame[0].frameId == 0) continue;

            // verify that frame is complete
            uint32_t fid = frame[0].frameId;
            for(uint32_t i = 1; i < fmax; i++) {
                if(frame[i].frameId != fid) {
                    fid = 0;
                    break;
                }
            }
            if(fid == 0) continue;
            inActive = false;

            // process frame
            int64_t diff = now - frame->targetEpochUs;
            if(diff > 0) {
                log("drop frame %d", fid);
                frame[0].frameId = 0;
                startOffset = (f + startOffset + 1) & FRAME_MASK;
                break;
            }

            pthread_rwlock_unlock(&rwlock);

            // adjust matrix brightness
            if(brightness != currBrightness) {
                currBrightness = brightness;
                createPwmLutCie1931(PWM_BITS, brightness, matrix->getPwmMapping());
            }

            // concatenate packets
            int p = 0, x = 0, y = 0;
            for(size_t sf = 0; sf <= SUBFRAME_MATRIX; sf++) {
                matrix->setPixels(p, x, y, frame[sf].pixelData, SUBFRAME_PIXELS);
            }
            frame[0].frameId = 0;

            // wait for scheduled frame boundary
            diff = frame->targetEpochUs - microtime();
            if(diff > 0) {
                usleep(diff);
            }
            // display new frame
            matrix->flipBuffer();

            startOffset = (f + startOffset + 1) & FRAME_MASK;
            pthread_rwlock_wrlock(&rwlock);
            break;
        }
        pthread_rwlock_unlock(&rwlock);

        if(inActive) usleep(1000);
    }

    // Finished. Shut down the RGB matrix.
    matrix->clearFrame();
    matrix->flipBuffer();
    delete matrix;

    return 0;
}

void * doUdpRx(UNUSED void *obj) {
    pthread_setname_np(pthread_self(), "udp_rx");

    // set cpu affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

    // buffer structures
    struct sockaddr_in addr[RECVMMSG_CNT];
    struct mmsghdr msgs[RECVMMSG_CNT];
    struct iovec iovecs[RECVMMSG_CNT];
    char bufs[RECVMMSG_CNT][UDP_BUFFER_SIZE];

    // init buffer structures
    memset(msgs, 0, sizeof(msgs));
    for (int i = 0; i < RECVMMSG_CNT; i++) {
        iovecs[i].iov_base         = bufs[i];
        iovecs[i].iov_len          = UDP_BUFFER_SIZE;
        msgs[i].msg_hdr.msg_iov    = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name   = &addr[i];
    }

    while(isRunning > 0) {
        for (auto &m : msgs) {
            m.msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
        }

        int retval = recvmmsg(socketUdp, msgs, RECVMMSG_CNT, MSG_WAITFORONE, nullptr);
        if(retval < 0) continue;

        pthread_rwlock_rdlock(&rwlock);
        for(int i = 0; i < retval; i++) {
            const auto dlen = msgs[i].msg_len;
            const auto data = bufs[i];

            // brightness packet
            if(dlen == sizeof(uint8_t)) {
                brightness = *(uint8_t*)data;
                continue;
            }
            // ignore packet if size is incorrect
            if(dlen != sizeof(frame_packet)) continue;

            auto packet = (frame_packet*) data;
            memcpy(&(packetBuffer[packet->frameId & FRAME_MASK][packet->subFrameId & SUBFRAME_MASK]), data, sizeof(frame_packet));
        }
        pthread_rwlock_unlock(&rwlock);
    }

    return nullptr;
}

long microtime() {
    struct timeval now = {};
    gettimeofday(&now, nullptr);
    return now.tv_sec * 1000000l + now.tv_usec;
}
