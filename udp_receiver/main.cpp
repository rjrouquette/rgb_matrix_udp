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
#include <sys/mman.h>
#include <cstdlib>

#include "logging.h"
#include "MatrixDriver.h"

#define UNUSED __attribute__((unused))

#define PWM_BITS (11)

#define UDP_PORT (1234)
#define UDP_BUFFER_SIZE (2048)
#define RECVMMSG_CNT (64)

#define FRAME_MASK (0x0fu)      // 16 frame circular buffer
#define SUBFRAME_PIXELS (400)   // 400 pixels per sub-frame

// do not sleep for more than 1 second
// prevents hangs on NTP adjustments
#define MAX_SLEEP (1000000l)

bool isRunning = true;
int socketUdp = -1;

struct frame_packet {
    uint32_t frameId;
    uint32_t subFrameId;
    uint64_t targetEpochUs;
    uint8_t pixelData[SUBFRAME_PIXELS * 3];
};

frame_packet *packetBuffer;
size_t packetBufferSize = 0;
unsigned matrixSubframes = 0;
unsigned maskSubframes = 0;
frame_packet* getPacket(unsigned frame, unsigned subframe);

pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

uint8_t brightness = 100;
uint8_t currBrightness = 100;
MatrixDriver *matrix;

pthread_t threadUdpRx;
void * doUdpRx(void *obj);
long microtime();
void sig_ignore(UNUSED int sig) { }
void sig_exit(UNUSED int sig) { isRunning = false; }
void displayAddress(uint32_t addr);
void initPacketBuffer(unsigned framePixels);
/*
class PixelMapDoubleWide : public PixelMapping {
private:
    const MatrixDriver &matrix;

public:
    explicit PixelMapDoubleWide(const MatrixDriver &matrix);
    ~PixelMapDoubleWide() override = default;

    void remap(unsigned int &x, unsigned int &y) override;
};
*/
int main(int argc, char **argv) {
    struct sigaction act = {};
    memset(&act, 0, sizeof(act));
    act.sa_handler = sig_ignore;
    sigaction(SIGUSR1, &act, nullptr);

    act.sa_handler = sig_exit;
    sigaction(SIGINT, &act, nullptr);
    sigaction(SIGTERM, &act, nullptr);
    sigaction(SIGKILL, &act, nullptr);

    // set cpu affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

    struct sockaddr_in serv_addr = {};
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(UDP_PORT);

    socketUdp = socket(AF_INET, SOCK_DGRAM, 0);

    // get eth0 address
    char ethAddr[32];
    struct ifreq ifr = {};
    bzero(&ifr, sizeof(ifr));
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
    ioctl(socketUdp, SIOCGIFADDR, &ifr);
    strcpy(ethAddr, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    log("eth0 ip: %s", ethAddr);

    if (bind(socketUdp, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        log("failed to bind to port %d", UDP_PORT);
        return EX_OSERR;
    }
    log("listening on port %d", UDP_PORT);

    // configure rgb matrix panel driver
    MatrixDriver::initGpio(MatrixDriver::gpio_rpi3);
    matrix = MatrixDriver::createInstance(PWM_BITS, MatrixDriver::HUB75AB, MatrixDriver::Z08AB);
    createPwmLutCie1931(PWM_BITS, brightness, matrix->getPwmMapping());
    log("instantiated matrix driver");
    log("matrix canvas is %d x %d", matrix->getWidth(), matrix->getHeight());

    // set panel remapping
    //PixelMapDoubleWide pixMap(*matrix);
    //matrix->setPixelMapping(&pixMap);
    log("matrix canvas remapped as %d x %d", matrix->getCanvasWidth(), matrix->getCanvasHeight());

    // initialize packet buffer
    initPacketBuffer(matrix->getWidth() * matrix->getHeight());
    log("%d subframes per frame", matrixSubframes);
    log("frame packet buffer is %ld bytes", (FRAME_MASK + 1) * (maskSubframes + 1) * sizeof(frame_packet));

    usleep(250000);

    // display ethernet address on panel
    displayAddress(ntohl(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr));
    sleep(3);

    // start udp rx thread
    log("start udp rx thread");
    pthread_create(&threadUdpRx, nullptr, doUdpRx, nullptr);

    log("waiting for frames");
    uint32_t startOffset = 0;
    // main processing loop
    while(isRunning) {
        bool inActive = true;
        uint64_t now = microtime();
        pthread_rwlock_wrlock(&rwlock);
        for(uint32_t f = 0; f <= FRAME_MASK; f++) {
            auto frame = getPacket(f + startOffset, 0);

            // look for pending frames
            if(frame[0].frameId == 0) continue;

            // verify that frame is complete
            uint32_t fid = frame[0].frameId;
            for(uint32_t i = 1; i < matrixSubframes; i++) {
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
            unsigned x = 0, y = 0;
            for(size_t sf = 0; sf < matrixSubframes; sf++) {
                matrix->setPixels(x, y, frame[sf].pixelData, SUBFRAME_PIXELS);
            }
            frame[0].frameId = 0;

            // wait for scheduled frame boundary
            diff = frame->targetEpochUs - microtime();
            if(diff > MAX_SLEEP) diff = MAX_SLEEP;
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
    delete matrix;
    munmap(packetBuffer, packetBufferSize);
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
            memcpy(getPacket(packet->frameId, packet->subFrameId), data, sizeof(frame_packet));
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

void displayAddress(uint32_t addr) {
    // display ethernet address
    matrix->clearFrame();
    auto x = matrix->getWidth() - 90;
    for(int i = 0; i < 4; i++) {
        auto octet = (addr >> ((3u - i) * 8u)) & 0xffu;
        unsigned pow = 100;
        for(int j = 0; j < 3; j++) {
            auto digit = (octet / pow) % 10;
            pow /= 10;

            matrix->drawHex(x, 0, digit, 0x00ffffu, 0x000000u);
            x += 6;
        }
        x += 6;
    }
    matrix->flipBuffer();
}

void initPacketBuffer(unsigned framePixels) {
    matrixSubframes = (framePixels + SUBFRAME_PIXELS - 1) / SUBFRAME_PIXELS;

    // compute subframe mask
    maskSubframes = 1;
    while(maskSubframes < matrixSubframes)
        maskSubframes <<= 1u;
    maskSubframes--;

    packetBufferSize = (FRAME_MASK + 1) * (maskSubframes + 1) * sizeof(frame_packet);
    packetBuffer = (frame_packet *) mmap(nullptr, packetBufferSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(packetBuffer == MAP_FAILED) {
        log("failed to allocate packet buffer");
        abort();
    }

    if(madvise(packetBuffer, packetBufferSize, MADV_HUGEPAGE) != 0) {
        log("transparent huge pages are not available");
    }

    bzero(packetBuffer, packetBufferSize);
}

frame_packet* getPacket(unsigned frame, unsigned subframe) {
    frame &= FRAME_MASK;
    subframe &= maskSubframes;
    return packetBuffer + (frame * (maskSubframes + 1)) + subframe;
}
/*
PixelMapDoubleWide::PixelMapDoubleWide(const MatrixDriver &_matrix) :
    matrix(_matrix)
{}

// double wide panel arrangement
// ---------------------------
// | <- chain 0 | chain 3 -> |
// ---------------------------
// | <- chain 1 | chain 2 -> |
// ---------------------------
void PixelMapDoubleWide::remap(unsigned int &x, unsigned int &y) {
    if(x >= matrix.getWidth()) {
        x = 2 * matrix.getWidth() - x - 1;
        y = matrix.getHeight() - y - 1;
    }
}
*/
