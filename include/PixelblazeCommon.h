#ifndef PixelblazeCommon_h
#define PixelblazeCommon_h

#include <WString.h>
#include <Stream.h>
#include "Arduino.h"

enum class WebsocketFormat : uint8_t {
    Text = 1,
    Binary = 2,
    Unknown = 255
};

inline WebsocketFormat websocketFormatFromInt(int v) {
    switch (v) {
        case 1:
            return WebsocketFormat::Text;
        case 2:
            return WebsocketFormat::Binary;
        default:
            Serial.print(F("Unexpected value: "));
            Serial.println(v);
            return WebsocketFormat::Unknown;
    }
}

enum class ReplyHandlerType : uint8_t {
    Sync = 0,
    RawBinary = 1,
    RawText = 2,
    AllPatterns = 3,
    Playlist = 4,
    Peers = 5,
    PreviewImage = 6,
    Settings = 7,
    Sequencer = 8,
    Expander = 9,
    Ping = 10,
    PatternControls = 11,
};

enum class LedType : uint8_t {
    None = 0,
    APA102_SK9822_DOTSTAR = 1,
    WS2812_SK6812_NEOPIXEL = 2,
    WS2801 = 3,
    OutputExpander = 5,
    Unknown = 255
};

inline LedType ledTypeFromInt(int v) {
    switch (v) {
        case 0:
            return LedType::None;
        case 1:
            return LedType::APA102_SK9822_DOTSTAR;
        case 2:
            return LedType::WS2812_SK6812_NEOPIXEL;
        case 3:
            return LedType::WS2801;
        case 5:
            return LedType::OutputExpander;
        default:
            Serial.print(F("Unexpected LED type value: "));
            Serial.println(v);
            return LedType::Unknown;
    }
}

enum class ChannelType : uint8_t {
    Unknown = 0,
    WS2812 = 1,
    APA102Data = 3,
    APA102Clock = 4
};

inline ChannelType channelTypeFromInt(int v) {
    switch (v) {
        case 1:
            return ChannelType::WS2812;
        case 3:
            return ChannelType::APA102Data;
        case 4:
            return ChannelType::APA102Clock;
        default:
            Serial.print(F("Unexpected channel type value: "));
            Serial.println(v);
            return ChannelType::Unknown;
    }
}

enum class RenderType : uint8_t {
    Invalid = 0,
    _1D = 1,
    _2D = 2,
    _3D = 3,
    Unknown = 255
};

inline RenderType renderTypeFromInt(int v) {
    switch (v) {
        case 0:
            return RenderType::Invalid;
        case 1:
            return RenderType::_1D;
        case 2:
            return RenderType::_2D;
        case 3:
            return RenderType::_3D;
        default:
            Serial.print(F("Unexpected render type value: "));
            Serial.println(v);
            return RenderType::Unknown;
    }
}

enum class InputSource : uint8_t {
    Remote = 0,
    Local = 1,
    Unknown = 255
};

inline InputSource inputSourceFromInt(int v) {
    switch (v) {
        case 0:
            return InputSource::Remote;
        case 1:
            return InputSource::Local;
        default:
            Serial.print(F("Unexpected input source value: "));
            Serial.println(v);
            return InputSource::Unknown;
    }
}

enum class FramePosition : uint8_t {
    First = 1,
    Middle = 2,
    Last = 4,
    Unknown = 255
};

inline FramePosition framePositionFromInt(int v) {
    switch (v) {
        case 1:
            return FramePosition::First;
        case 2:
            return FramePosition::Middle;
        case 4:
            return FramePosition::Last;
        default:
            Serial.print(F("Unexpected frame position value: "));
            Serial.println(v);
            return FramePosition::Unknown;
    }
}

enum class SequencerMode : uint8_t {
    Off = 0,
    ShuffleAll = 1,
    Playlist = 2,
    Unknown = 255
};

inline SequencerMode sequencerModeFromInt(int v) {
    switch (v) {
        case 0:
            return SequencerMode::Off;
        case 1:
            return SequencerMode::ShuffleAll;
        case 2:
            return SequencerMode::Playlist;
        default:
            Serial.print(F("Unexpected sequencer mode value: "));
            Serial.println(v);
            return SequencerMode::Unknown;
    }
}

enum class SettingReply : uint8_t {
    Settings = 1,
    Sequencer = 2,
    Expander = 4
};

enum class BinaryMsgType : uint8_t {
    PutSource = 1,
    PutByteCode = 3,
    PreviewImage = 4,
    PreviewFrame = 5,
    GetSource = 6,
    GetProgramList = 7,
    PutPixelMap = 8,
    ExpanderChannels = 9
};

enum class FailureCause : uint8_t {
    TimedOut = 1,
    BufferAllocFail = 2,
    MultipartReadInterrupted = 3,
    StreamWriteFailure = 4,
    MalformedHandler = 5,
    ConnectionLost = 6,
    ClientDestructorCalled = 7
};

struct Stats {
    float fps = 0;
    int vmerr = 0;
    int vmerrpc = 0;
    int memBytes = 0;
    int expansions = 0;
    RenderType renderType = RenderType::Invalid;
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
    SequencerMode sequencerMode = SequencerMode::Off;
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
    LedType ledType = LedType::None;
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
    InputSource soundSrc = InputSource::Remote;
    InputSource accelSrc = InputSource::Remote;
    InputSource lightSrc = InputSource::Remote;
    InputSource analogSrc = InputSource::Remote;
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
    LedType ledType;
    uint8_t numElements;
    String *colorOrder;
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

/**
 * Some reads involve buffering data across multiple messages. Implementations are available for using local memory
 * and an attached SD card. Others can be implemented as needed. Using this base implementation will function,
 * but no result will ever be returned for the following operations:
 *  - getPatterns()
 *  - getPreviewImage()
 *  - getSystemState() (Settings and sequencer state will be processed, expander config won't)
 *  - rawRequest()
 *
 * Because data can be split across multiple messages, we frequently need to re-open a write stream and append data,
 * then eventually open the completed buffer for reading. This means that close()-ing and free()-ing the returned
 * CloseableStreams does not clean up the buffered data, only deleteStreamResults() does.
 *
 * If the system attempts to allocate a buffer and fails, it will call garbageCollect() and then make another attempt
 * before discarding the enqueued request handler. If this happens it will call requestHandler.reportFailure(errorCode).
 * Codes are defined in PixelblazeHandlers.h
 *
 * Note that PixelblazeBuffer implementations are not responsible for managing memory around CloseableStreams, that's
 * handled in the client.
 */
class PixelblazeBuffer {
public:
    /**
     * Get a stream writing to a named buffer
     *
     * @param key the name of the buffer
     * @param append If true, writes will be added to the end of any existing data, otherwise existing buffer contents
     *               will be overwritten.
     * @return A CloseableStream pointer if a buffer was available, or nullptr otherwise
     */
    virtual CloseableStream *makeWriteStream(String &key, bool append) {
        return nullptr;
    };

    /**
     * Get a stream reading from a named buffer
     *
     * @param key the name of the buffer
     * @return A CloseableStream pointer if a buffer was available, or nullptr otherwise
     */
    virtual CloseableStream *makeReadStream(String &key) {
        return nullptr;
    };

    /**
     * Delete any stored state for a given buffer
     * @param key the name of the buffer
     */
    virtual void deleteStreamResults(String &key) {};

    /**
     * Attempts to release any resources where possible to allow more write streams to be returned. Only called if
     * makeWriteStream() fails.
     */
    virtual void garbageCollect() {};
};

/**
 * Pixelblaze sends several message types unprompted, some of them ~100/s unless they're shut off. Extend this class
 * and implement any or all methods to handle those unprompted messages, otherwise they're dropped.
 *
 * TODO: What happens if a pattern's code is edited?
 */
class PixelblazeWatcher {
public:
    /**
     * Pixelblaze sends a stats packet once per second, all included info is repackaged into the provided struct.
     */
    virtual void handleStats(Stats &stats) {};

    /**
     * Pixelblaze sends a packet every time the active pattern changes.
     */
    virtual void handlePatternChange(SequencerState &patternChange) {};

    /**
     * Every time Pixelblaze completes a render cycle it can ship a binary representation of a possibly cross-fuzzed
     * view of the entire strip up to 1024 (r,g,b) pixels where each channel is aa byte.
     * Preview frames can be enabled/disabled by calling sendFramePreviews(bool). It's unclear what the default is.
     *
     * TODO: If the default is to send em, do we flip that? It looks like the web re-enables them every time the
     * TODO: pattern changes.
     *
     * @param previewPixelRGB Packed RGB data, with the first pixel r = preview[0], g = preview[1], b = preview[2]
     * @param len the length of the preview buffer in bytes.
     */
    virtual void handlePreviewFrame(uint8_t *previewPixelRGB, size_t len) {};

    /**
     * TODO: Currently not dispatched
     *
     * Every time a pattern is added or removed from the active playlist, a copy of that playlist is sent back to
     * connected clients.
     */
    virtual void handlePlaylistChange(PlaylistUpdate &playlistUpdate) {};
};

#endif
