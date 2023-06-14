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

// Flags to indicate what elements of the reply you care about in getSystemState(). Bitwise-OR them to select a subset
// By default only the config and sequencer replies are tracked
#define WATCH_SETTING_REQ 1
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
 *  - getSystemState() (Settings and sequencer state will be processed, expander config won't)
 *  - rawRequest()
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

/**
 * The client class.
 *
 * NOT THREADSAFE. DO NOT SHARE INSTANCES.
 */
class PixelblazeClient {
public:
    PixelblazeClient(WebSocketClient &wsClient, PixelblazeBuffer &streamBuffer,
                     PixelblazeWatcher &watcher, ClientConfig clientConfig = defaultConfig);

    virtual ~PixelblazeClient();

    /**
     * Initialize connection to the Pixelblaze
     *
     * @return true if successful, false otherwise
     */
    bool begin();

    /**
     * Check if the websocket client is connected, does not check more thoroughly
     *
     * @return true if connected, otherwise false
     */
    bool connected();

    /**
     * Call this on every loop() iteration or equivalent. If the client is receiving preview frames they can clog the
     * pipes very quickly and I recommend calling it at least every 100ms. If not receiving previews at least once a
     * second is recommended.
     *
     * Will go through received messages dispatching them to handlers or dropping them as appropriate until the message
     * queue is empty or clientConfig.maxInboundCheckMs has passed
     *
     * In addition, this also performs maintenance on the websocket connection if needed
     */
    void checkForInbound();

    /**
     * Get the most recent round-trip time to the pixelblaze. Can be very noisy.
     *
     * @return the most recent round-trip milliseconds
     */
    uint32_t getMostRecentPingMs() const {
        return lastPingRoundtripMs;
    }

    /**
     * Get the time since a ping sent at clientConfig.sendPingEveryMs intervals received an ack
     *
     * @return milliseconds since a response to a ping was received
     */
    uint32_t getMsSinceSuccessfulPing() const {
        return millis() - lastSuccessfulPingAtMs;
    }

    /**
     * Get a list of all patterns on the device
     *
     * @param replyHandler handler will receive an iterator of the (id, name) pairs of all patterns on the device
     * @return true if the request was dispatched, false otherwise
     */
    bool getPatterns(void (*handler)(AllPatternIterator &), void (*onError)(int) = ignoreError);

    /**
     * Get the contents of a playlist, along with some metadata about it and its current state
     *
     * @param replyHandler handler will receive a Playlist object, which may be overwritten after handle() returns
     * @param playlistName The playlist to fetch, presently only the default is supported
     * @return true if the request was dispatched, false otherwise.
     */
    bool getPlaylist(void (*handler)(Playlist &), String &playlistName = defaultPlaylist, void (*onError)(int) = ignoreError);

    /**
     * Get the index on the playlist of the current pattern
     *
     * @param replyHandler handler will receive an int indicating the 0-based index
     * @return true if the request was dispatched, false otherwise.
     */
    bool getPlaylistIndex(void (*handler)(size_t), void (*onError)(int) = ignoreError);

    /**
     * Set the current pattern by its index on the active playlist
     *
     * @param idx the playlist index to switch to
     * @return true if the request was dispatched, false otherwise.
     */
    bool setPlaylistIndex(int idx);

    /**
     * Advance the pattern forward one index
     *
     * TODO: Does it wrap? The UI prev button doesn't
     * @return true if the request was dispatched, false otherwise.
     */
    bool nextPattern();

    /**
     * Step the current pattern back one, wrapping if needed
     *
     * @return true if the request was dispatched, false otherwise.
     */
    bool prevPattern();

    /**
     * Set the sequencer state to "play"
     *
     * @return true if the request was dispatched, false otherwise.
     */
    bool playSequence();

    /**
     * Set the sequencer state to "pause"
     *
     * @return true if the request was dispatched, false otherwise.
     */
    bool pauseSequence();

    /**
     * Possible modes:
     *
     * SEQ_MODE_OFF
     * SEQ_MODE_SHUFFLE_ALL
     * SEQ_MODE_PLAYLIST
     *
     * @return true if the request was dispatched, false otherwise.
     */
    bool setSequencerMode(int sequencerMode);

    /**
     * TODO: Not yet implemented
     *
     * @return true if the request was dispatched, false otherwise.
     */
    bool getPeers(void (*handler)(Peer *, size_t), void (*onError)(int) = ignoreError);

    /**
     * Set the active brightness
     *
     * @param brightness clamped to [0, 1.0], with 0 being fully off and 1.0 indicating full brightness
     * @param saveToFlash whether to persist the value through restarts. While you can send these at high volume
     *                    for smooth dimming, only save when the value settles.
     * @return true if the request was dispatched, false otherwise.
     */
    bool setBrightness(float brightness, bool saveToFlash);

    /**
     * Set the value of a controller for the current pattern
     *
     * @param controlName name of the control to set, for instance "sliderMyControl"
     * @param value clamped to [0, 1]
     * @param saveToFlash whether to persist the value through restarts. While you can send these at high volume
     *                    for smooth changes, only save when the value settles.
     * @return true if the request was dispatched, false otherwise.
     */
    bool setCurrentPatternControl(String &controlName, float value, bool saveToFlash);

    /**
     * Set the value of a set of controllers for the current pattern
     *
     * @param controls {name, value} of the control to set, for instance {"sliderMyControl", 0.5}
     * @param numControls controls in the provided array
     * @param saveToFlash whether to persist the values through restarts. While you can send these at high volume
     *                    for smooth changes, only save when the value settles.
     * @return true if the request was dispatched, false otherwise.
     */
    bool setCurrentPatternControls(Control *controls, int numControls, bool saveToFlash);

    /**
     * Fetch the state of all controls for the current pattern
     *
     * @param replyHandler Handler that will receive an array of Controls and the patternId they're for
     * @return true if the request was dispatched, false otherwise.
     */
    bool getCurrentPatternControls(void (*handler)(Control*, size_t), void (*onError)(int) = ignoreError);

    /**
     * Get controls for a specific pattern
     *
     * @param patternId the pattern to fetch controls for
     * @param replyHandler the handler that will receive those controls
     * @return true if the request was dispatched, false otherwise.
     */
    bool getPatternControls(String &patternId, void (*handler)(String &, Control *, size_t), void (*onError)(int) = ignoreError);

    /**
     * Gets a preview image for a specified pattern. The returned stream is a 100px wide by 150px tall 8-bit JPEG image.
     * Note that many modern TFT libraries for displaying images do not support 8-bit JPEGs.
     *
     * @param patternId the pattern to fetch a preview for
     * @param replyHandler handler to ingest the image stream
     * @return true if the request was dispatched, false otherwise.
     */
    bool getPreviewImage(String &patternId, void (*handlerFn)(String &, CloseableStream *), bool clean = true, void (*onError)(int) = ignoreError);

    /**
     * Set the global brightness limit
     *
     * TODO: Do we hide the fact that the API disagrees on format for the brightness slider vs global?
     *
     * @param value clamped to [0, 100]
     * @param saveToFlash whether to persist the value through restarts. While you can send these at high volume
     *                    for smooth dimming, only save when the value settles.
     * @return true if the request was dispatched, false otherwise.
     */
    bool setBrightnessLimit(int value, bool saveToFlash);

    /**
     * Set the number of pixels controlled
     *
     * @param pixels Number of pixels
     * @param saveToFlash whether to persist the value through restarts. While you can send these at high volume
     *                    for smooth effects, only save when the value settles.
     * @return true if the request was dispatched, false otherwise.
     */
    bool setPixelCount(uint32_t pixels, bool saveToFlash);

    /**
     * Request the general state of the system, which comes back in three parts:
     *  - Settings:
     *      More or less the contents of the settings page, plus some hidden settings. It's unclear what some fields
     *      mean but they're still returned.
     *  - Sequence:
     *      The current state of the pattern playing parts of the system.
     *  - Expander Configuration:
     *      //TODO: Not yet implemented
     *      The configuration of the output expander.
     *
     *  IN NEED OF HELP: I don't have a system with an output expander to look at what exactly the traffic looks like
     *
     * Because you frequently only care about one of the three, you can specify which responses to actually watch for.
     * Set the watchResponses arg to a bitwise-OR'd combination of:
     *   WATCH_SETTING_REQ
     *   WATCH_SEQ_REQ
     *   WATCH_EXPANDER_REQ
     * Note that the default drops WATCH_EXPANDER_REQ, as they can come in out-of-order and cause issues
     *
     * Note that because the sequencer message is identical to the pattern change message, it may get picked up by
     * the unrequested message handler even if it's ignored here.
     *
     * @param settingsHandler handler for the settings response, use NoopSettingsReplyHandler if ignoring
     * @param seqHandler handler for the sequencer response, use NoopSequencerReplyHandler if ignoring
     *
     * @return true if the request was dispatched, false otherwise.
     */
    bool getSystemState(
            void (*settingsHandler)(Settings &),
            void (*seqHandler)(SequencerState &),
            void (*expanderHandler)(ExpanderConfig &),
            int watchResponses = WATCH_SETTING_REQ | WATCH_SEQ_REQ,
            void (*onError)(int) = ignoreError);

    /**
     * Utility wrapper around getSystemState()
     *
     * @param settingsHandler handler for the non-ignored response
     * @return true if the request was dispatched, false otherwise.
     */
    bool getSettings(void (*settingsHandler)(Settings &), void (*onError)(int) = ignoreError);

    /**
     * Utility wrapper around getSystemState()
     *
     * @param seqHandler handler for the non-ignored response
     * @return true if the request was dispatched, false otherwise.
     */
    bool getSequencerState(void (*seqHandler)(SequencerState &), void (*onError)(int) = ignoreError);

    /**
     * Utility wrapper around getSystemState()
     *
     * @param expanderHandler handler for the non-ignored response
     * @return true if the request was dispatched, false otherwise.
     */
    bool getExpanderConfig(void (*expanderHandler)(ExpanderConfig &), void (*onError)(int) = ignoreError);

    /**
     * Send a ping to the controller
     *
     * Note that this prompts a response that's identical to other requests, so if they overlap the round trip time will
     * be nonsense as there's no way to tell which ack is for which message.
     *
     * @param replyHandler handler will receive the approximate round trip time
     * @return true if the request was dispatched, false otherwise.
     */
    bool ping(void (*handler)(uint32_t), void (*onError)(int) = ignoreError);

    /**
     * Specify whether the controller should send a preview of each render cycle. If sent they're handled in the
     * unrequested message handler.
     *
     * TODO: What's the default?
     *
     * @param sendEm
     * @return true if the request was dispatched, false otherwise.
     */
    bool sendFramePreviews(bool sendEm);

    /**
     * Utility function for interacting with the backend in arbitrary ways if they're not implemented in this library
     *
     * @param replyHandler handler to deal with the resulting message
     * @param request json to send to the backend
     * @return true if the request was dispatched, false otherwise.
     */
    bool rawRequest(RawBinaryHandler &replyHandler, JsonDocument &request);

    /**
     * Utility function for interacting with the backend in arbitrary ways if they're not implemented in this library
     *
     * @param replyHandler handler to deal with the resulting message
     * @param request json to send to the backend
     * @return true if the request was dispatched, false otherwise.
     */
    bool rawRequest(RawTextHandler &replyHandler, JsonDocument &request);

    /**
     * Utility function for interacting with the backend in arbitrary ways if they're not implemented in this library
     *
     * Note that the maximum chunk size is bounded by binaryBufferBytes
     *
     * @param replyHandler handler to deal with the resulting message
     * @param request Binary stream to send to the backend
     * @return true if the request was dispatched, false otherwise.
     */
    bool rawRequest(RawBinaryHandler &replyHandler, int binType, Stream &request);

    /**
     * Utility function for interacting with the backend in arbitrary ways if they're not implemented in this library
     *
     * Note that the maximum chunk size is bounded by binaryBufferBytes
     *
     * @param replyHandler handler to deal with the resulting message
     * @param request Binary stream to send to the backend
     * @return true if the request was dispatched, false otherwise.
     */
    bool rawRequest(RawTextHandler &replyHandler, int binType, Stream &request);

    /**
     * Default handler for reply error reporting. Error codes are represented by the FAILURE_
     * #defines in PixelblazeHandlers.h
     *
     * @param errorCode A code indicating rough failure reasons
     */
    static void ignoreError(int ignored) {
    }

    /**
     * Utility function for transforming a CamelCase variable name to human readable.
     *
     * "sliderMyControl" => "My Control"
     *
     * @param camelCaseVar the variable name to humanize
     * @param maxWords How many words to split it into max. humanizeVarName("sliderThinkLOLIDK", 2) => "Think LOLIDK"
     * @return The humanized variable name
     */
    static String humanizeVarName(String &camelCaseVar, int maxWords = 10);

private:
    bool connectionMaintenance();

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

    void evictQueue(int reason);

    void parseSequencerState();

    bool sendJson(JsonDocument &doc);

    bool sendBinary(int binType, Stream &stream);

    static void noopSettings(Settings &s) {};

    static void noopSequencer(SequencerState &s) {};

    static void noopExpander(ExpanderConfig &e) {};

private:
    WebSocketClient& wsClient;
    PixelblazeBuffer& streamBuffer;
    PixelblazeWatcher& watcher;
    ClientConfig clientConfig;

    ReplyHandler **replyQueue;
    size_t queueFront = 0;
    size_t queueBack = 0;

    SequencerState sequencerState;
    Stats statsEvent;
    Settings settings;
    ExpanderConfig expanderConfig;
    Playlist playlist;
    PlaylistUpdate playlistUpdate;
    Peer *peers;
    size_t peerCount = 0;
    Control *controls;
    size_t controlCount = 0;

    uint8_t *byteBuffer;
    char *textReadBuffer;
    DynamicJsonDocument json;

    int binaryReadType = -1;

    uint32_t lastPingAtMs = 0;
    uint32_t lastSuccessfulPingAtMs = 0;
    uint32_t lastPingRoundtripMs = 0;
};

#endif
