#ifndef PixelblazeStructs_h
#define PixelblazeStructs_h

#include <WString.h>
#include <Stream.h>

//Relevant websocket flags
#define FORMAT_TEXT 0x1
#define FORMAT_BINARY 0x2

#define LED_TYPE_NONE 0
#define LED_TYPE_APA102_SK9822_DOTSTAR 1
#define LED_TYPE_WS2812_SK6812_NEOPIXEL 2
#define LED_TYPE_WS2801 3
#define LED_TYPE_OUTPUT_EXPANDER 5

#define CHANNEL_TYPE_WS2812 1
#define CHANNEL_TYPE_APA102_DATA 3
#define CHANNEL_TYPE_APA102_CLOCK 4

#define RENDER_TYPE_INVALID 0
#define RENDER_TYPE_1D 1
#define RENDER_TYPE_2D 2
#define RENDER_TYPE_3D 3

#define SOURCE_PREFER_REMOTE 0
#define SOURCE_PREFER_LOCAL 1

struct Stats {
    float fps = 0;
    int vmerr = 0;
    int vmerrpc = 0;
    int memBytes = 0;
    int expansions = 0;
    int renderType = 0;
    int uptimeMs = 0;
    int storageBytesUsed = 0;
    int storageBytesSize = 0;
    int rr0 = 0;
    int rr1 = 0;
    int rebootCounter = 0;
};

struct Control {
    String name = "";
    float value = 0;
};

struct SequencerState {
    String name = "";
    String activeProgramId = "";
    Control *controls = nullptr;
    size_t controlCount = 0;
    int sequencerMode = 0;
    bool runSequencer = false;
    int playlistPos = 0;
    String playlistId = "";
    int ttlMs = 0;
    int remainingMs = 0;
};

struct Settings {
    String name = "";
    String brandName = "";
    int pixelCount = 0;
    float brightness = 0;
    int maxBrightness = 0;
    String colorOrder = "";
    int dataSpeedHz = 0;
    int ledType = 0;
    int sequenceTimerMs = 0;
    int transitionDurationMs = 0;
    int sequencerMode = 0;
    bool runSequencer = false;
    bool simpleUiMode = false;
    bool learningUiMode = false;
    bool discoveryEnabled = false;
    String timezone = "";
    bool autoOffEnable = false;
    String autoOffStart = "";
    String autoOffEnd = "";
    int cpuSpeedMhz = 0;
    bool networkPowerSave = false;
    int mapperFit = 0;
    int leaderId = 0;
    int nodeId = 0;
    int soundSrc = 0;
    int accelSrc = 0;
    int lightSrc = 0;
    int analogSrc = 0;
    int exp = 0;
    String version = "";
    int chipId = 0;
};

struct Peer {
    int id;
    String ipAddress;
    String name;
    String version;
    bool isFollowing;
    int nodeId;
    size_t followerCount;
};

struct PlaylistItem {
    String id = "";
    int durationMs = 0;
};

struct Playlist {
    String id = "";
    int position = 0;
    int currentDurationMs = 0;
    int remainingCurrentMs = 0;
    PlaylistItem *items = nullptr;
    int numItems = 0;
};

struct PlaylistUpdate {
    String id = "";
    PlaylistItem *items = nullptr;
    int numItems = 0;
};

struct ExpanderChannel {
    uint8_t channelId;
    uint8_t ledType;
    uint8_t numElements;
    String* colorOrder;
    uint16_t pixels;
    uint16_t startIndex;
    uint32_t frequency;
};

struct ClientConfig {
    size_t jsonBufferBytes = 4096;
    size_t binaryBufferBytes = 1024 * 3; //Per the Wizard, frame previews could have up to 1024 pixels * 3 bytes
    size_t replyQueueSize = 100;
    size_t maxResponseWaitMs = 5000;
    size_t maxInboundCheckMs = 300;
    size_t textReadBufferBytes = 128;
    size_t syncPollWaitMs = 5;
    size_t expanderChannelLimit = 64;
    size_t controlLimit = 25;
    size_t peerLimit = 25;
    size_t playlistLimit = 150;
    size_t maxConnRepairMs = 300;
    size_t connRepairRetryDelayMs = 50;
    size_t sendPingEveryMs = 3000;
};

class CloseableStream : public Stream {
public:
    explicit CloseableStream(Stream *wrapped,
                             size_t (*bulk)(Stream *, const uint8_t *, size_t) = nullptr,
                             void (*closer)(Stream *) = nullptr
    ) : wrapped(wrapped), closer(closer), bulk(bulk) {}

    size_t write(uint8_t v) override {
        return wrapped->write(v);
    }

    size_t write(const uint8_t *buffer, size_t size) override {
        if (bulk) {
            return bulk(wrapped, buffer, size);
        } else if (buffer) {
            size_t idx = 0;
            while (idx < size) {
                if (write(buffer[idx])) {
                    idx++;
                } else {
                    break;
                }
            }

            return idx;
        } else if (size == 0) {
            return 0;
        } else {
            return -1;
        }
    }

    int available() override {
        return wrapped->available();
    }

    int read() override {
        return wrapped->read();
    }

    int peek() override {
        return wrapped->peek();
    }

    virtual ~CloseableStream() {
        if (closer) {
            closer(wrapped);
        }

        delete wrapped;
    }

    void close() {
        if (closer) {
            closer(wrapped);
        }

        closer = nullptr;
    }

private:
    Stream *wrapped;

    size_t (*bulk)(Stream *, const uint8_t *, size_t);

    void (*closer)(Stream *);
};

#endif //PixelblazeStructs_h
