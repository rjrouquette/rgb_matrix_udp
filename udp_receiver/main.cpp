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
#include "../rpi-rgb-led-matrix/include/led-matrix.h"
#include "../rpi-rgb-led-matrix/include/graphics.h"

using namespace rgb_matrix;

#define UNUSED __attribute__((unused))

#define UDP_BUFFER_SIZE (2048)
#define RECVMMSG_CNT (64)

bool isRunning = true;
int socketUdp = -1;
int udpPort = 0;
int rows = 0;
int cols = 0;

struct frame_packet {
    uint32_t frameId;
    uint32_t subFrameId;
    uint64_t targetEpochUs;
    uint8_t pixelData[1024];
};

frame_packet packetBuffer[16][16];
pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

pthread_t threadUdpRx;
void * doUdpRx(void *obj);
long microtime();
void sig_ignore(UNUSED int sig) { }

int main(int argc, char **argv) {
    struct sigaction act = {};
    memset(&act, 0, sizeof(act));
    act.sa_handler = sig_ignore;
    sigaction(SIGUSR1, &act, nullptr);

    if(argc != 4) {
        log("Usage: udp-rgb-matrix <udpPort> <rows> <cols> <bitsPerChannel>");
        return EX_USAGE;
    }

    udpPort = atoi(argv[1]);
    rows = atoi(argv[2]);
    cols = atoi(argv[3]);

    const int fsize = rows * cols * 3;
    const int fmax = (fsize + 1023) / 1024;
    if(fmax > 16) {
        log("panel dimensions requires too many sub frames: %d x %d -> %d sub frames", rows, cols, fmax);
        return EX_CONFIG;
    }

    struct sockaddr_in serv_addr = {};
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons((uint16_t)udpPort);

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
    log("eth0 ip: %s\n", ethAddr);

    if (bind(socketUdp, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        log("failed to bind to port %d", udpPort);
        return EX_OSERR;
    }
    log("listening on port %d", udpPort);

    // clear packet buffer
    bzero(packetBuffer, sizeof(packetBuffer));

    // start udp rx thread
    log("start udp rx thread");
    pthread_create(&threadUdpRx, nullptr, doUdpRx, nullptr);

    // configure rgb matrix panel driver
    RGBMatrix::Options matrix_options;
    rgb_matrix::RuntimeOptions runtime_opt;

    matrix_options.rows = rows;
    matrix_options.cols = cols;
    matrix_options.pwm_bits = 11;
    matrix_options.hardware_mapping = "adafruit-hat-pwm";

    RGBMatrix *matrix = rgb_matrix::CreateMatrixFromOptions(
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

    size_t plen;
    char *pixelData;
    offscreen->Serialize((const char **)&pixelData, &plen);
    log("frame size: %ld bytes", plen);

    log("waiting for frames");
    uint32_t startOffset = 0;
    // main processing loop
    while(isRunning) {
        bool inActive = true;
        uint64_t now = microtime();
        for(uint32_t f = 0; f < 16; f++) {
            auto frame = packetBuffer[(f + startOffset) & 0xfu];

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
                startOffset = (f + startOffset + 1) & 0xfu;
            }
            log("processing frame %d", fid);

            offscreen->Serialize((const char **)&pixelData, &plen);
            if(plen != fsize) {
                log("framebuffer size mismatch");
                abort();
            }

            int sf = 0;
            while(plen > 0 && sf < 16) {
                size_t dlen = std::min(plen, sizeof(frame_packet::pixelData));
                memcpy(pixelData, frame[sf].pixelData, dlen);
                pixelData += dlen;
                plen -= dlen;
                sf++;
            }

            // display frame
            offscreen = matrix->SwapOnVSync(offscreen);

            startOffset = (f + startOffset + 1) & 0xfu;
            break;
        }

        if(inActive) usleep(1000);
    }

    // Finished. Shut down the RGB matrix.
    matrix->Clear();
    delete matrix;

    return 0;
}

void * doUdpRx(UNUSED void *obj) {
    pthread_setname_np(pthread_self(), "udp_rx");


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

            // ignore packet if size is incorrect
            if(dlen != sizeof(frame_packet)) continue;

            auto packet = (frame_packet*) data;
            memcpy(&(packetBuffer[packet->frameId & 0xfu][packet->subFrameId & 0xfu]), data, sizeof(frame_packet));
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
