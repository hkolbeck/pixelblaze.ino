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

static ClientConfig defaultConfig = {};

class PixelblazeBuffer {
public:
    virtual CloseableStream *makeWriteStream(String key, bool append) {};

    virtual CloseableStream *makeReadStream(String key) {};

    virtual void deleteStreamResults(String key) {};

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

    static String humanizeVarName(String &camelCaseVar, int maxWords = 10);

private:
    void weedExpiredReplies();

    void seekingTextHasText();

    void seekingBinaryHasText();

    void seekingTextHasBinary();

    void seekingBinaryHasBinary();

    bool readBinaryToStream(String &bufferId, bool append);

    size_t queueLength();

    void dispatchTextReply(ReplyHandler *handler);

    void dispatchBinaryReply(ReplyHandler *handler);

    void handleUnrequestedJson();

    bool handleUnrequestedBinary(int frameType);

    bool enqueueReply(ReplyHandler *handler);

    bool enqueueReplies(int, ...);

    void dequeueReply();

    void compactQueue();

    void parseSequencerState();

    void sendJson(JsonDocument &doc);

private:
    WebSocketClient wsClient;
    PixelblazeBuffer &binaryBuffer;
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
