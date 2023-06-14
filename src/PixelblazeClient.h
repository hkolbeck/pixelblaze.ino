#ifndef PixelblazeClient_h
#define PixelblazeClient_h

#include "PixelblazeHandlers.h"

#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

/**
 * A client for the Pixelblaze LED controller's websocket API
 *
 * While the API is public, documentation is very scarce. This library was primarily written by reviewing
 * @zranger1's Python library https://github.com/zranger1/pixelblaze-client and watching the connection between
 * the web controller UI and the controller, as well as chatting with the Pixelblaze's creator Ben Hencke
 * (electromage.com).
 *
 * All writes to the Pixelblaze connection are synchronous, but effects may not be. Any data requested is returned
 * asynchronously, and so requires providing a handler for the eventual result. In general this will look like extending
 * an indicated class and defining a handle() function.
 *
 * TODO: This library implements only a subset of the functions supported by the websocket API, though they are the
 * TODO: primary functions for everyday usage. If there's a need for scaling out that set we'll burn that bridge
 * TODO: when we come to it.
 */

// Flags used to indicate a binary message's position in a message series
#define FRAME_FIRST 1
#define FRAME_MIDDLE 2
#define FRAME_LAST 4

// Flags to indicate the sequencer mode
#define SEQ_MODE_OFF 0
#define SEQ_MODE_SHUFFLE_ALL 1
#define SEQ_MODE_PLAYLIST 2

// Flags to indicate what elements of the reply you care about in getSettings(). Bitwise-OR them to select a subset
// By default only the config and sequencer replies are tracked
#define WATCH_CONFIG_REQ 1
#define WATCH_SEQ_REQ 2
#define WATCH_EXPANDER_REQ 4

static String defaultPlaylist = String("_defaultplaylist_");
static ClientConfig defaultConfig = {};

/**
 * Some reads involve buffering data across multiple messages. Implementations are available for using local memory
 * and an attached SD card. Others can be implemented as needed. Using this base implementation will function,
 * but no result will ever be returned for the following operations:
 *  - getPatterns()
 *  - getPreviewImage()
 *  - getSettings() (Settings and sequencer state will be processed, expander config won't)
 *  - getRawBinary()
 *
 * Because data can be split across multiple messages, we frequently need to re-open a write stream and append data,
 * then eventually open the completed buffer for reading. This means that close()-ing and free()-ing the returned
 * CloseableStreams does not clean up the buffered data, only deleteStreamResults() does.
 *
 * If the system attempts to allocate a buffer and fails, it will call garbageCollect() and then make another attempt
 * before discarding the enqueued request handler. If this happens it will call requestHandler.replyFailed(errorCode).
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

    virtual void garbageCollect() {};
};
/**
 * Pixelblaze sends several message types unprompted, some of them ~100/s unless they're shut off. Extend this class
 * and implement any or all methods to handle those unprompted messages, otherwise they're dropped on the ground.
 *
 * TODO: What happens if a pattern's code is edited?
 */
class PixelblazeUnrequestedHandler {
public:
    /**
     * Pixelblaze sends a stats packet once per second, all included info is repackaged into the provided struct.
     * Note that the provided stats object is no longer valid after this returns.
     */
    virtual void handleStats(Stats &stats) {};

    /**
     * Pixelblaze sends a packet every time the active pattern changes.
     */
    virtual void handlePatternChange(SequencerState &patternChange) {};

    /**
     * Every time Pixelblaze completes a render cycle it can ship a binary representation of the first 100 pixels
     * TODO: Is 100 correct?
     * Preview frames can be enabled/disabled by calling sendFramePreviews(bool). It's unclear what the default is.
     * TODO: If the default is to send em, do we flip that?
     * @param previewPixelRGB Packed RGB data, with the first pixel r = preview[0], g = preview[1], b = preview[2]
     * @param len the length of the preview buffer in bytes. Note that if clientConfig.framePreviewBufferBytes is
     *            not divisible by three and a frame exceeded that length a partial pixel would be included at the end
     */
    virtual void handlePreviewFrame(uint8_t *previewPixelRGB, size_t len) {};

    /**
     * TODO: Currently not dispatched
     *
     * Every time a pattern is added or removed from the active playlist, a copy of that playlist is sent back
     */
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

    bool readBinaryToStream(ReplyHandler *handler, String &bufferId, bool append);

    size_t queueLength() const;

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
