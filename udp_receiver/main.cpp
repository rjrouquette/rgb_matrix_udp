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
#include "../rpi-rgb-led-matrix/include/led-matrix.h"
#include "../rpi-rgb-led-matrix/include/graphics.h"

using namespace rgb_matrix;

#define UNUSED __attribute__((unused))

#define MATRIX_WIDTH (128)
#define MATRIX_HEIGHT (32)

#define PANEL_WIDTH (64)
#define PANEL_HEIGHT (32)
#define PANEL_CHAIN (2)

#define UDP_PORT (1234)
#define UDP_BUFFER_SIZE (2048)
#define RECVMMSG_CNT (64)

#define FRAME_MASK (0x3fu)      // 64 frame circular buffer
#define SUBFRAME_MASK (0xfu)    // 16 sub-frames per frame

bool isRunning = true;
int socketUdp = -1;
int height = 0;
int width = 0;

struct frame_packet {
    uint32_t frameId;
    uint32_t subFrameId;
    uint64_t targetEpochUs;
    uint8_t pixelData[1024];
};

uint8_t pixelBuffer[(SUBFRAME_MASK + 1) * sizeof(frame_packet::pixelData)];
frame_packet packetBuffer[FRAME_MASK + 1][SUBFRAME_MASK + 1];
pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

RGBMatrix *matrix;

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

    height = MATRIX_HEIGHT;
    width = MATRIX_WIDTH;

    const int fsize = height * width * 3;
    const int fmax = (fsize + 1023) / 1024;
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
    RGBMatrix::Options matrix_options;
    rgb_matrix::RuntimeOptions runtime_opt;

    matrix_options.rows = PANEL_HEIGHT;
    matrix_options.cols = PANEL_WIDTH;
    matrix_options.chain_length = PANEL_CHAIN;
    matrix_options.pwm_bits = 11;
    matrix_options.hardware_mapping = "adafruit-hat-pwm";

    runtime_opt.gpio_slowdown = 7;

    matrix = rgb_matrix::CreateMatrixFromOptions(
            matrix_options,
            runtime_opt
    );
    matrix->SetBrightness(20);
    FrameCanvas *offscreen = matrix->CreateFrameCanvas();
    log("initialized rgb matrix panel driver");

    // display ip address on panel
    Color color(255, 255, 0);
    rgb_matrix::Font font;
    font.LoadFont("5x8.bdf");
    rgb_matrix::DrawText(offscreen, font, 0, 8, color, nullptr, ethAddrHex, 0);
    offscreen = matrix->SwapOnVSync(offscreen);

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

            // concatenate packets
            uint8_t *buff = pixelBuffer;
            size_t rem = fsize;
            uint32_t sf = 0;
            while(rem > 0 && sf <= SUBFRAME_MASK) {
                size_t dlen = std::min(rem, sizeof(frame_packet::pixelData));
                memcpy(buff, frame[sf++].pixelData, dlen);
                buff += dlen;
                rem -= dlen;
            }
            frame[0].frameId = 0;

            // set pixels
            buff = pixelBuffer;
            for(int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    offscreen->SetPixel(x, y, buff[0], buff[1], buff[2]);
                    buff += 3;
                }
            }

            diff = frame->targetEpochUs - microtime();
            if(diff > 0) {
                usleep(diff);
            }

            // display frame
            offscreen = matrix->SwapOnVSync(offscreen);

            startOffset = (f + startOffset + 1) & FRAME_MASK;
            pthread_rwlock_wrlock(&rwlock);
            break;
        }
        pthread_rwlock_unlock(&rwlock);

        if(inActive) usleep(1000);
    }

    // Finished. Shut down the RGB matrix.
    matrix->Clear();
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
                matrix->SetBrightness(*(uint8_t*)data);
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
