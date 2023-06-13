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

struct Stats {
    float fps;
    int vmerr;
    int vmerrpc;
    int memBytes;
    int expansions;
    int renderType;
    int uptimeMs;
    int storageBytesUsed;
    int storageBytesSize;
    int rr0;
    int rr1;
    int rebootCounter;
};

struct Control {
    String name;
    float value;
};

struct SequencerState {
    String name;
    String activeProgramId;
    Control* controls;
    usize_t controlCount;
    int sequencerMode;
    bool runSequencer;
    int playlistPos;
    String playlistId;
    int ttlMs;
    int remainingMs;
};

struct Settings {
    String name;
    String brandName;
    int pixelCount;
    float brightness;
    int maxBrightness;
    String colorOrder;
    int dataSpeed;
    int ledType;
    int sequenceTimer ms;
    int transitionDuration;
    int sequencerMode;
    bool runSequencer;
    bool simpleUiMode;
    bool learningUiMode;
    bool discoveryEnabled;
    String timezone;
    bool autoOffEnable;
    String autoOffStart;
    String autoOffEnd;
    int cpuSpeedMhz;
    bool networkPowerSave;
    int mapperFit;
    int leaderId;
    int nodeId;
    int soundSrc;
    int accelSrc;
    int lightSrc;
    int analogSrc;
    int exp;
    String version;
    int chipId;
};

struct Peer {
    //TODO
};

struct PlaylistItem {
    String id;
    int durationMs;
};

struct Playlist {
    String id;
    int position;
    int currentDurationMs;
    int remainingCurrentMs;
    PlaylistItem* items;
    int numItems;
};

struct PlaylistUpdate {
    String id;
    PlaylistItem* items;
    int numItems;
};

struct ExpanderConfig {
    //TODO
};

struct ClientConfig {
    usize_t jsonBufferBytes = 4096;
    usize_t binaryBufferBytes = 1024;
    usize_t replyQueueSize = 100;
    usize_t maxResponseWaitMs = 5000;
    usize_t maxInboundCheckMs = 300;
    usize_t framePreviewBufferBytes = 300;
    usize_t textReadBufferBytes = 128;
    usize_t syncPollWaitMs = 5;
    usize_t controlLimit = 25;
    usize_t peerLimit = 25;
    usize_t playlistLimit = 150;
};
static const ClientConfig defaultConfig = {};

class PixelblazeBuffer {
public:
    virtual Stream makeWriteStream(String key, bool append) = 0;
    virtual Stream makeReadStream(String key) = 0;
    virtual void cleanupStream(String key) = 0;

    virtual void garbageCollect(){};
};

class PixelblazeUnrequestedHandler {
public:
    virtual void handleStats(Stats& stats){};
    virtual void handlePatternChange(SequencerState& patternChange){};
    virtual void handlePreviewFrame(uint8_t[] previewPixelRGB, usize_t len){};
    virtual void handlePlaylistChange(PlaylistUpdate& playlistUpdate){};
};

class PixelblazeClient {
public:
    PixelblazeClient(WebSocketClient& wsClient, PixelblazeBuffer& binaryBuffer, PixelblazeUnrequestedHandler& unrequestedHandler, ClientConfig& clientConfig = defaultConfig);

    virtual ~PixelblazeClient();

    bool connected();
    bool connectionMaintenance();

    void checkForInbound();

    bool getPatterns(AllPatternsReplyHandler& replyHandler);
    bool getPlaylist(PlaylistReplyHandler& replyHandler);
    bool getPlaylistIndex(PlaylistIndexHandler& replyHandler);
    void setPlaylistIndex(int idx);
    void nextPattern();
    void prevPattern();
    void playSequence();
    void pauseSequence();
    void setSequencerMode(int sequencerMode);
    bool getPeers(PeersReplyHandler& replyHandler);
    void setBrightness(float brightness, bool save);
    void setCurrentPatternControl(String& controlName, float value, bool saveToFlash);
    void setCurrentPatternControls(Control* controls, int numControls, bool saveToFlash);
    bool getCurrentPatternControls(PatternControlReplyHandler& replyHandler);
    bool getPatternControls(String& patternId, PatternControlReplyHandler& replyHandler);
    bool getPreviewImage(String& patternId, PreviewImageReplyHandler& replyHandler);
    void setBrightnessLimit(float value, bool saveToFlash);
    void setPixelCount(uint32_t pixels, bool saveToFlash);
    //TODO: Note about seq replies still possibly hitting unrequested handler if excluded here, also note that expander is ignored by default
    bool getSettings(
            SettingsReplyHandler& settingsHandler, SequencerReplyHandler& seqHandler, ExpanderConfigReplyHandler& expanderHandler,
            int watchResponses = WATCH_CONFIG_REQ | WATCH_SEQ_REQ);
    bool ping(PingHandler& replyHandler);
    void sendFramePreviews(bool sendEm);

    bool getRawBinary(RawBinaryHandler& replyHandler, JsonDocument& request);
    bool getRawText(RawTextHandler& replyHandler, JsonDocument& request);

    String humanizeVarName(String& camelCaseVar);
    String humanizeDataSize(int bytes);

private:
    void weedExpiredReplies();
    int queueLength();
    void dispatchTextReply(ReplyHandler* handler);
    void dispatchBinaryReply(ReplyHandler* handler);
    void handleUnrequestedJson();
    bool enqueueReply();
    void parseSequencerState();
    void sendJson(JsonDocument& doc);

private:
    WebSocketClient wsClient;
    PixelblazeBuffer binaryBuffer;
    PixelblazeUnrequestedHandler unrequestedHandler;
    ClientConfig clientConfig;

    DynamicJsonDocument json;

    uint8_t* byteBuffer;
    uint8_t* previewFrameBuff;
    char* textReadBuffer;

    ReplyHandler* replyQueue;
    usize_t queueFront = 0;
    usize_t queueBack = 0;

    int binaryReadType = -1;

    SequencerState sequencerState;
    Stats statsEvent;
    Settings settings;
    ExpanderConfig expanderConfig;
    Playlist playlist;
    PlaylistUpdate playlistUpdate;

    Control* controls;
    usize_t controlCount = 0;

    Peer* peers;
    usize_t peerCount = 0;
}

#endif
