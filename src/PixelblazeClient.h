#ifndef PixelblazeClient_h
#define PixelblazeClient_h

#include "PixelblazeHandlers.h"

#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

#define FRAME_FIRST 1
#define FRAME_MIDDLE 2
#define FRAME_LAST 4

#define SEQ_MODE_OFF 0
#define SEQ_MODE_SHUFFLE_ALL 1
#define SEQ_MODE_PLAYLIST 2

#define WATCH_CONFIG_REQ 1
#define WATCH_SEQ_REQ 2
#define WATCH_EXPANDER_REQ 4

static String defaultPlaylist = String("_defaultplaylist_");

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
    size_t framePreviewBufferBytes = 300;
    size_t textReadBufferBytes = 128;
    size_t syncPollWaitMs = 5;
    size_t controlLimit = 25;
    size_t peerLimit = 25;
    size_t playlistLimit = 150;
};
static ClientConfig defaultConfig = {};

class PixelblazeBuffer {
public:
    virtual Stream makeWriteStream(String key, bool append) = 0;

    virtual Stream makeReadStream(String key) = 0;

    virtual void cleanupStream(String key) = 0;

    virtual void garbageCollect() {};
};

class PixelblazeUnrequestedHandler {
public:
    virtual void handleStats(Stats &stats) {};

    virtual void handlePatternChange(SequencerState &patternChange) {};

    virtual void handlePreviewFrame(uint8_t *previewPixelRGB, size_t len) {};

    virtual void handlePlaylistChange(PlaylistUpdate &playlistUpdate) {};
};

class PixelblazeClient {
public:
    PixelblazeClient(WebSocketClient &wsClient, PixelblazeBuffer &binaryBuffer,
                     PixelblazeUnrequestedHandler &unrequestedHandler, ClientConfig &clientConfig = defaultConfig);

    virtual ~PixelblazeClient();

    bool connected();

    bool connectionMaintenance();

    void checkForInbound();

    bool getPatterns(AllPatternsReplyHandler &replyHandler);

    bool getPlaylist(PlaylistReplyHandler &replyHandler, String &playlistName = defaultPlaylist);

    bool getPlaylistIndex(PlaylistIndexHandler &replyHandler);

    void setPlaylistIndex(int idx);

    void nextPattern();

    void prevPattern();

    void playSequence();

    void pauseSequence();

    void setSequencerMode(int sequencerMode);

    bool getPeers(PeersReplyHandler &replyHandler);

    void setBrightness(float brightness, bool save);

    void setCurrentPatternControl(String &controlName, float value, bool saveToFlash);

    void setCurrentPatternControls(Control *controls, int numControls, bool saveToFlash);

    bool getCurrentPatternControls(PatternControlReplyHandler &replyHandler);

    bool getPatternControls(String &patternId, PatternControlReplyHandler &replyHandler);

    bool getPreviewImage(String &patternId, PreviewImageReplyHandler &replyHandler);

    void setBrightnessLimit(int value, bool saveToFlash);

    void setPixelCount(uint32_t pixels, bool saveToFlash);

    //TODO: Note about seq replies still possibly hitting unrequested handler if excluded here, also note that expander is ignored by default
    bool getSettings(
            SettingsReplyHandler &settingsHandler, SequencerReplyHandler &seqHandler,
            ExpanderConfigReplyHandler &expanderHandler,
            int watchResponses = WATCH_CONFIG_REQ | WATCH_SEQ_REQ);

    bool ping(PingReplyHandler &replyHandler);

    void sendFramePreviews(bool sendEm);

    bool getRawBinary(RawBinaryHandler &replyHandler, JsonDocument &request);

    bool getRawText(RawTextHandler &replyHandler, JsonDocument &request);

    String humanizeVarName(String &camelCaseVar);

    String humanizeDataSize(int bytes);

private:
    void weedExpiredReplies();

    int queueLength();

    void dispatchTextReply(ReplyHandler *handler);

    void dispatchBinaryReply(ReplyHandler *handler);

    void handleUnrequestedJson();

    bool enqueueReply(ReplyHandler *handler);

    bool enqueueReplies(int, ...);

    void parseSequencerState();

    void sendJson(JsonDocument &doc);

private:
    WebSocketClient wsClient;
    PixelblazeBuffer& binaryBuffer;
    PixelblazeUnrequestedHandler unrequestedHandler;
    ClientConfig clientConfig;

    DynamicJsonDocument json;

    uint8_t *byteBuffer;
    uint8_t *previewFrameBuffer;
    char *textReadBuffer;

    ReplyHandler **replyQueue;
    size_t queueFront = 0;
    size_t queueBack = 0;

    int binaryReadType = -1;

    SequencerState sequencerState;
    Stats statsEvent;
    Settings settings;
    ExpanderConfig expanderConfig;
    Playlist playlist;
    PlaylistUpdate playlistUpdate;

    Control *controls;
    size_t controlCount = 0;

    Peer *peers;
    size_t peerCount = 0;
};

#endif
