#ifndef PixelblazeStructs_h
#define PixelblazeStructs_h

#include <WString.h>
#include <Stream.h>

//Relevant websocket flags
#define FORMAT_TEXT 0x1
#define FORMAT_BINARY 0x2

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
    int dataSpeed = 0;
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
    //TODO
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

struct ExpanderConfig {
    //TODO
};

struct ClientConfig {
    size_t jsonBufferBytes = 4096;
    size_t binaryBufferBytes = 1024;
    size_t replyQueueSize = 100;
    size_t maxResponseWaitMs = 5000;
    size_t maxInboundCheckMs = 300;
    size_t textReadBufferBytes = 128;
    size_t syncPollWaitMs = 5;
    size_t controlLimit = 25;
    size_t peerLimit = 25;
    size_t playlistLimit = 150;
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
            return -1
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
