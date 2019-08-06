#include <unistd.h>
#include <cstdlib>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sysexits.h>
#include <pthread.h>
#include <csignal>
#include <cstring>
#include <sys/time.h>
#include <json-c/json.h>
#include <sys/mman.h>
#include <cerrno>
#include <algorithm>
#include <sys/stat.h>

#include "logging.h"

#define UNUSED __attribute__((unused))

const uint64_t frameMagic = 0xebb29724dd5126acUL;

struct frame_packet {
    uint32_t frameId;
    uint32_t subFrameId;
    uint64_t targetEpochUs;
    uint8_t pixelData[1200]; // 400 RGB pixels
};

struct remote_panel {
    sockaddr_in addr;
    int xoff, yoff;
    int width, height;
};

remote_panel panels[256];

bool isRunning = true;
uint8_t brightness = 100;
int width = 0;
int height = 0;
int propagationDelay = 250000;
int updateInterval = 50000;
char socketPath[4096];

void *pixelBuffer;
uint8_t *pixBuffAct, *pixBuffNext;
pthread_mutex_t mutexBuff = PTHREAD_MUTEX_INITIALIZER;

pthread_t threadUdpTx;
void * doUdpTx(void *obj);
void sendFrame(int socketUdp, uint32_t frameId, uint64_t frameEpoch, const remote_panel &rp);

long microtime();
void sig_quit(UNUSED int sig) { isRunning = false; }

bool loadConfiguration(const char *fileName);
void labelPanels();
void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void drawHex(int xoff, int yoff, uint8_t value);
void drawHex2(int xoff, int yoff, uint8_t value);
void flipBuffer(uint8_t newBrightness = brightness);

ssize_t readFully(int sockfd, void* buffer, size_t len, size_t block = 65536);

int main(int argc, char **argv) {
    struct sigaction act = {};
    memset(&act, 0, sizeof(act));
    act.sa_handler = sig_quit;
    sigaction(SIGINT, &act, nullptr);
    sigaction(SIGTERM, &act, nullptr);

    bzero(panels, sizeof(panels));
    strcpy(socketPath, "rgb-matrix.sock");

    if(argc != 2) {
        log("Usage: udp-rgb-matrix <config.json>");
        return EX_USAGE;
    }

    if(!loadConfiguration(argv[1])) {
        log("failed to load configuration file: %s", argv[1]);
        return EX_CONFIG;
    }

    size_t frameSize = width * height * 3;
    pixelBuffer = mmap(nullptr, frameSize * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(madvise(pixelBuffer, frameSize * 2, MADV_HUGEPAGE) != 0) ::abort();
    pixBuffAct = (uint8_t *) pixelBuffer;
    pixBuffNext = pixBuffAct + frameSize;

    labelPanels();

    // start udp rx thread
    log("start udp tx thread");
    pthread_create(&threadUdpTx, nullptr, doUdpTx, nullptr);

    // wait three seconds before processing frame data
    sleep(3);

    bzero(pixBuffNext, frameSize);
    flipBuffer();

    const int sockListen = socket(PF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {};
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path)-1);

    unlink(socketPath);
    if(bind(sockListen, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log("ERROR on socket binding! %s (%d)", strerror(errno), errno);
        log("failed to bind listening socket");

        close(sockListen);
        log("socket closed");

        log("exiting");
        return EX_CANTCREAT;
    }
    chmod(socketPath, 0666);
    listen(sockListen, 1);
    log("unix socket opened");

    sockaddr clientAddr = {};
    socklen_t addrLen;
    const uint8_t *magic = (uint8_t *)&frameMagic;
    uint8_t rbuff[16];
    while(isRunning) {
        addrLen = sizeof(clientAddr);
        int connFd = accept(sockListen, &clientAddr, &addrLen);
        if(connFd < 0) continue;
        log("new data stream");

        int moff = 0;
        while(isRunning) {
            // check for frame magic number
            if(moff < sizeof(uint64_t)) {
                auto r = read(connFd, rbuff, 1);
                if(r <= 0) {
                    log("end of data stream");
                    break;
                }

                if(rbuff[0] == magic[moff]) {
                    moff++;
                } else {
                    log("re-syncing data stream");
                    moff = 0;
                }
                continue;
            }
            moff = 0;

            auto r = read(connFd, rbuff, 1);
            if(r <= 0) {
                log("end of data stream");
                break;
            }

            // read frame data
            r = readFully(connFd, pixBuffNext, frameSize);
            if(r != (ssize_t) frameSize) {
                log("end of data stream");
                break;
            }

            flipBuffer(rbuff[0]);
        }
        // close client connection
        close(connFd);

        // clear display
        bzero(pixBuffNext, frameSize);
        flipBuffer();
    }
    close(sockListen);

    pthread_join(threadUdpTx, nullptr);
    munmap(pixelBuffer, frameSize * 2);
    log("shutdown");
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

    while(isRunning) {
        // transmit frame data
        pthread_mutex_lock(&mutexBuff);
        for(const auto &panel : panels) {
            if(panel.width == 0) continue;
            sendFrame(socketUdp, frameId, microtime() + propagationDelay, panel);
        }
        pthread_mutex_unlock(&mutexBuff);
        if(++frameId == 0) frameId = 1;

        // sleep until next update
        uint32_t now = microtime();
        usleep(updateInterval - (now % updateInterval));
    }

    close(socketUdp);
    return nullptr;
}

void sendFrame(int socketUdp, uint32_t frameId, uint64_t frameEpoch, const remote_panel &rp) {
    int rowAdvance = (width - rp.width) * 3;
    int packOff = 0;

    sendto(socketUdp, &brightness, sizeof(brightness), 0, (sockaddr *) &rp.addr, sizeof(rp.addr));

    frame_packet packet = {};
    packet.frameId = frameId;
    packet.targetEpochUs = frameEpoch;
    packet.subFrameId = 0;

    uint8_t *buff = &pixBuffAct[(rp.yoff * width + rp.xoff) * 3];
    for(int y = 0; y < rp.height; y++) {
        for(int x = 0; x < rp.width; x++) {
            for(int c = 0; c < 3; c++) {
                packet.pixelData[packOff++] = *(buff++);

                if (packOff == sizeof(packet.pixelData)) {
                    sendto(socketUdp, &packet, sizeof(packet), 0, (sockaddr *) &rp.addr, sizeof(rp.addr));
                    packOff = 0;
                    packet.subFrameId++;
                }
            }
        }
        buff += rowAdvance;
    }

    if(packOff != 0) {
        sendto(socketUdp, &packet, sizeof(packet), 0, (sockaddr *) &rp.addr, sizeof(rp.addr));
    }
}

bool loadConfiguration(const char *fileName) {
    json_object *jtmp, *jpanels;
    auto config = json_object_from_file(fileName);
    if(config == nullptr) {
        log("failed to parse configuration file");
        return false;
    }

    if(json_object_object_get_ex(config, "socket", &jtmp)) {
        strcpy(socketPath, json_object_get_string(jtmp));
    }

    if(json_object_object_get_ex(config, "brightness", &jtmp)) {
        brightness = (uint8_t) json_object_get_int(jtmp);
    }

    if(json_object_object_get_ex(config, "width", &jtmp)) {
        width = json_object_get_int(jtmp);
    }
    else {
        log("configuration file is missing `width`");
        return false;
    }

    if(json_object_object_get_ex(config, "height", &jtmp)) {
        height = json_object_get_int(jtmp);
    }
    else {
        log("configuration file is missing `height`");
        return false;
    }

    if(json_object_object_get_ex(config, "propagationDelay", &jtmp)) {
        propagationDelay = json_object_get_int(jtmp);
    }
    else {
        log("configuration file is missing `propagationDelay`");
        return false;
    }

    if(json_object_object_get_ex(config, "updateInterval", &jtmp)) {
        updateInterval = json_object_get_int(jtmp);
    }
    else {
        log("configuration file is missing `updateInterval`");
        return false;
    }

    if(!json_object_object_get_ex(config, "panels", &jpanels)) {
        log("configuration file is missing `panels`");
        return false;
    }

    int len = json_object_array_length(jpanels);
    for(int i = 0; i < len; i++) {
        auto jpanel = json_object_array_get_idx(jpanels, i);

        if(!json_object_object_get_ex(jpanel, "address", &jtmp)) {
            log("panels is missing `address`");
            return false;
        }
        inet_aton(json_object_get_string(jtmp), &panels[i].addr.sin_addr);

        if(!json_object_object_get_ex(jpanel, "port", &jtmp)) {
            log("panels is missing `port`");
            return false;
        }
        panels[i].addr.sin_port = htons(json_object_get_int(jtmp));
        panels[i].addr.sin_family = AF_INET;

        json_object_object_get_ex(jpanel, "horizontalOffset", &jtmp);
        panels[i].xoff = json_object_get_int(jtmp);

        json_object_object_get_ex(jpanel, "verticalOffset", &jtmp);
        panels[i].yoff = json_object_get_int(jtmp);

        json_object_object_get_ex(jpanel, "width", &jtmp);
        panels[i].width = json_object_get_int(jtmp);

        json_object_object_get_ex(jpanel, "height", &jtmp);
        panels[i].height = json_object_get_int(jtmp);
    }

    return true;
}

void flipBuffer(uint8_t newBrightness) {
    // atomically flip pixel buffer
    pthread_mutex_lock(&mutexBuff);
    auto temp = pixBuffAct;
    pixBuffAct = pixBuffNext;
    pixBuffNext = temp;
    brightness = newBrightness;
    pthread_mutex_unlock(&mutexBuff);
}

void labelPanels() {
    int i = 0;
    for(const auto &panel : panels) {
        if(panel.width == 0) continue;
        drawHex2(panel.xoff, panel.yoff, i++);
    }

    flipBuffer();
}

void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *pixel = &(pixBuffNext[(y * width + x) * 3]);
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
}

void drawHex2(int xoff, int yoff, uint8_t value) {
    uint8_t hi = (value >> 4u) & 0xfu;
    uint8_t lo = value & 0xfu;

    drawHex(xoff, yoff, hi);
    drawHex(xoff + 11, yoff, lo);
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
                setPixel(x + xoff, y + yoff, 255, 255, 0);
            else
                setPixel(x + xoff, y + yoff, 0, 0, 0);
        }
    }
}

ssize_t readFully(int sockfd, void* buffer, size_t len, size_t block) {
    ssize_t n, l;
    l = 0;
    while(l < (ssize_t)len) {
        n = ::recv(sockfd, ((char*)buffer)+l, std::min(len-l, block), 0);
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
