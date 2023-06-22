#ifndef PixelblazeClient_h
#define PixelblazeClient_h

#include "ArduinoHttpClient.h"
#include "ArduinoJson.h"

#include "PixelblazeHandlers.h"
#include "PixelblazeCommon.h"

static String defaultPlaylist = String("_defaultplaylist_");
static ClientConfig defaultConfig = {};

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
 * NOT THREADSAFE. DO NOT SHARE INSTANCES.
 *
 * TODO: This library implements only a subset of the functions supported by the websocket API, though they are the
 * TODO: primary functions for everyday usage. If there's a need for scaling out that set we'll burn that bridge
 * TODO: when we come to it.
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
     * In addition, this also performs maintenance on the websocket connection if needed.
     *
     * @return true if the client was able to poll the connection, false otherwise
     */
    bool checkForInbound();

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
    bool getPatterns(void (*handler)(AllPatternIterator &), void (*onError)(FailureCause) = logError);

    #ifdef CLOSURES_SUPPORTED
    bool getPatternsSync(void (*handler)(AllPatternIterator &), void (*onError)(FailureCause) = logError);
    #endif

    /**
     * Get the contents of a playlist, along with some metadata about it and its current state
     *
     * @param replyHandler handler will receive a Playlist object, which may be overwritten after handle() returns
     * @param playlistName The playlist to fetch, presently only the default is supported
     * @return true if the request was dispatched, false otherwise.
     */
    bool getPlaylist(void (*handler)(Playlist &), String &playlistName = defaultPlaylist,
                     void (*onError)(FailureCause) = logError);

    /**
     * Get the index on the playlist of the current pattern
     *
     * @param replyHandler handler will receive an int indicating the 0-based index
     * @return true if the request was dispatched, false otherwise.
     */
    bool getPlaylistIndex(void (*handler)(size_t), void (*onError)(FailureCause) = logError);

    /**
     * Set the current pattern by its index on the active playlist
     *
     * @param idx the playlist index to switch to
     * @return true if the request was dispatched, false otherwise.
     */
    bool setPlaylistIndex(int idx);

    /**
     * Advance the pattern forward one index, wrapping if needed
     *
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
    bool setSequencerMode(SequencerMode sequencerMode);

    /**
     * TODO: Not yet implemented
     *
     * @return true if the request was dispatched, false otherwise.
     */
    bool getPeers(void (*handler)(Peer *, size_t), void (*onError)(FailureCause) = logError);

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
    bool getCurrentPatternControls(void (*handler)(Control *, size_t), void (*onError)(FailureCause) = logError);

    /**
     * Get controls for a specific pattern
     *
     * @param patternId the pattern to fetch controls for
     * @param replyHandler the handler that will receive those controls
     * @return true if the request was dispatched, false otherwise.
     */
    bool getPatternControls(String &patternId, void (*handler)(String &, Control *, size_t),
                            void (*onError)(FailureCause) = logError);

    /**
     * Gets a preview image for a specified pattern. The returned stream is a 100px wide by 150px tall 8-bit JPEG image.
     * Note that many modern TFT libraries for displaying images do not support 8-bit JPEGs.
     *
     * @param patternId the pattern to fetch a preview for
     * @param replyHandler handler to ingest the image stream
     * @return true if the request was dispatched, false otherwise.
     */
    bool getPreviewImage(String &patternId, void (*handlerFn)(String &, CloseableStream *),
                         bool clean = true, void (*onError)(FailureCause) = logError);

    /**
     * Set the global brightness limit
     *
     * @param value clamped to [0, 1]
     * @param saveToFlash whether to persist the value through restarts. While you can send these at high volume
     *                    for smooth dimming, only save when the value settles.
     * @return true if the request was dispatched, false otherwise.
     */
    bool setBrightnessLimit(float value, bool saveToFlash);

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
     *  - Expander Channel Configuration:
     *      The configuration of the output expander if any.
     *
     * Because you frequently only care about one of the three, you can specify which responses to actually watch for.
     * Set the watchResponses arg to a bitwise-OR'd combination of:
     *   (int) SettingReply::Settings
     *   (int) SettingReply::Sequencer
     *   (int) SettingReply::Expander
     * Note that the default drops SettingReply::Expander, as they can come in out-of-order and cause issues
     *
     * Note that because the sequencer message is identical to the pattern change message, it may get picked up by
     * the unrequested message handler even if it's ignored here.
     *
     * @param settingsHandler handler for the settings response, ignore with noopSettings
     * @param seqHandler handler for the sequencer response, ignore with noopSequencer
     * @param expanderHandler handler for the expander channel response
     * @param rawWatchReplies OR'd  together
     *
     * @return true if the request was dispatched, false otherwise.
     */
    bool getSystemState(
            void (*settingsHandler)(Settings &),
            void (*seqHandler)(SequencerState &),
            void (*expanderHandler)(ExpanderChannel *, size_t),
            int rawWatchReplies = (int) SettingReply::Settings | (int) SettingReply::Sequencer,
            void (*onError)(FailureCause) = logError);

    /**
     * Utility wrapper around getSystemState()
     *
     * @param settingsHandler handler for the non-ignored response
     * @return true if the request was dispatched, false otherwise.
     */
    bool getSettings(void (*settingsHandler)(Settings &), void (*onError)(FailureCause) = logError);

    /**
     * Utility wrapper around getSystemState()
     *
     * @param seqHandler handler for the non-ignored response
     * @return true if the request was dispatched, false otherwise.
     */
    bool getSequencerState(void (*seqHandler)(SequencerState &), void (*onError)(FailureCause) = logError);

    /**
     * Utility wrapper around getSystemState()
     *
     * @param expanderHandler handler for the non-ignored response
     * @return true if the request was dispatched, false otherwise.
     */
    bool getExpanderConfig(void (*expanderHandler)(ExpanderChannel *, size_t), void (*onError)(FailureCause) = logError);

    /**
     * Send a ping to the controller
     *
     * Note that this prompts a response that's identical to other requests, so if they overlap the round trip time will
     * be nonsense as there's no way to tell which ack is for which message.
     *
     * @param replyHandler handler will receive the approximate round trip time
     * @return true if the request was dispatched, false otherwise.
     */
    bool ping(void (*handler)(uint32_t), void (*onError)(FailureCause) = logError);

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
     * @param rawRequestBinType the raw BinaryMsgType of the outbound request
     * @param request Binary stream to send to the backend
     * @return true if the request was dispatched, false otherwise.
     */
    bool rawRequest(RawBinaryHandler &replyHandler, int rawRequestBinType, Stream &request);

    /**
     * Utility function for interacting with the backend in arbitrary ways if they're not implemented in this library
     *
     * Note that the maximum chunk size is bounded by binaryBufferBytes
     *
     * @param replyHandler handler to deal with the resulting message
     * @param rawBinType the raw BinaryMsgType of the outbound request
     * @param request Binary stream to send to the backend
     * @return true if the request was dispatched, false otherwise.
     */
    bool rawRequest(RawTextHandler &replyHandler, int rawBinType, Stream &request);

    /**
     * Default handler for reply error reporting. Error codes are represented by the FAILURE_
     * #defines in PixelblazeHandlers.h
     *
     * @param failureCause A code indicating rough failure reasons
     */
    static void logError(FailureCause failureCause) {
        Serial.print("Request failed with error code: ");
        Serial.println((int) failureCause);
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
    static String humanizeVarName(String &camelCaseVar, int maxWords = 4);

    /**
     * Utility functions for dropping responses from getSystemState
     */
    static void noopSettings(Settings &s) {};
    static void noopSequencer(SequencerState &s) {};
    static void noopExpander(ExpanderChannel *e, size_t c) {};
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

    bool handleUnrequestedBinary(int rawBinaryType);

    bool enqueueReply(ReplyHandler *handler);

    bool enqueueReplies(int, ...);

    void dequeueReply();

    void compactQueue();

    void evictQueue(FailureCause cause);

    void parseSequencerState();

    bool sendJson(JsonDocument &doc);

    bool sendBinary(int rawBinType, Stream &stream);

    static String *getColorOrder(uint8_t code);

private:
    WebSocketClient &wsClient;
    PixelblazeBuffer &streamBuffer;
    PixelblazeWatcher &watcher;
    ClientConfig clientConfig;

    ReplyHandler **replyQueue;
    size_t queueFront = 0;
    size_t queueBack = 0;

    SequencerState sequencerState;
    Stats statsEvent;
    Settings settings;
    Playlist playlist;
    PlaylistUpdate playlistUpdate;

    ExpanderChannel *expanderChannels;
    size_t numExpanderChannels = 0;
    Peer *peers;
    size_t peerCount = 0;
    Control *controls;
    size_t controlCount = 0;

    uint8_t *byteBuffer;
    char *textReadBuffer;
    DynamicJsonDocument json;

    int rawBinaryReadType = -1;

    uint32_t lastPingAtMs = 0;
    uint32_t lastSuccessfulPingAtMs = 0;
    uint32_t lastPingRoundtripMs = 0;
};

#endif
