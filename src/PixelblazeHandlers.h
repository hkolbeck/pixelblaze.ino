#ifndef PixelblazeHandlers_h
#define PixelblazeHandlers_h

#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

#include "PixelblazeCommon.h"
#include "PixelblazeClient.h"

#define BIN_TYPE_PUT_SOURCE 1
#define BIN_TYPE_PUT_BYTE_CODE 3
#define BIN_TYPE_PREVIEW_IMAGE 4
#define BIN_TYPE_PREVIEW_FRAME 5
#define BIN_TYPE_GET_SOURCE 6
#define BIN_TYPE_GET_PROGRAM_LIST 7
#define BIN_TYPE_PUT_PIXEL_MAP 8
#define BIN_TYPE_EXPANDER_CONFIG 9

#define FAILED_TIMED_OUT 1
#define FAILED_BUFFER_ALLOC_FAIL 2
#define FAILED_MULTIPART_READ_INTERRUPTED 3
#define FAILED_STREAM_WRITE_FAILURE 4
#define FAILED_MALFORMED_HANDLER 5
#define FAILED_CONNECTION_LOST 6

static String GARBAGE = "GARBAGE";

/*
  Base class for all objects which signify a command waiting for a response. Library users should never see this name
*/
class ReplyHandler {
protected:
    ReplyHandler(int _replyType, int _format) {
        replyType = _replyType;
        format = _format;

        // These are reset on every copy made before it's used.
        requestTsMs = 0;
        satisfied = false;
    }

public:
    virtual ~ReplyHandler() = default;

    virtual bool shouldDeleteBuffer() {
        return true;
    };

    virtual bool jsonMatches(JsonDocument &json) {
        return false;
    };

    virtual bool cleanup() {}

    virtual void replyFailed(int cause) {}

    virtual bool isSatisfied() {
        return satisfied;
    }

public:
    int replyType;
    int format;
    unsigned long requestTsMs;
    bool satisfied;
};

/*
  Special case handler that wraps any other handler and signals when it's been completed
*/
#define HANDLER_SYNC 0

class SyncHandler : public ReplyHandler {
public:
    SyncHandler(ReplyHandler *_wrappedHandler, bool *_trueWhenFinished)
            : ReplyHandler(HANDLER_SYNC, _wrappedHandler->format) {
        wrappedHandler = _wrappedHandler;
        trueWhenFinished = _trueWhenFinished;
    }

    ~SyncHandler() override {
        delete wrappedHandler;
    }

    ReplyHandler *getWrapped() const {
        return wrappedHandler;
    }

    void finish() {
        *trueWhenFinished = true;
    }

    bool shouldDeleteBuffer() override {
        return wrappedHandler->shouldDeleteBuffer();
    }

    bool jsonMatches(JsonDocument &json) override {
        return wrappedHandler->jsonMatches(json);
    }

    ReplyHandler *wrappedHandler;
private:
    bool *trueWhenFinished;
};

/*
  Base class for all commands that receive JSON as a response, users should never interact
*/
class TextReplyHandler : public ReplyHandler {
protected:
    explicit TextReplyHandler(int replyType)
            : ReplyHandler(replyType, TYPE_TEXT) {};

public:
    ~TextReplyHandler() override = default;

    //Text handlers never buffer
    bool shouldDeleteBuffer() override {
        return false;
    }
};

/*
  Base class for all commands that receive a possibly multipart binary-encoded message. Users should never interact
*/
class BinaryReplyHandler : public ReplyHandler {
protected:
    BinaryReplyHandler(int replyType, String &_bufferId, int _binType, bool _clean)
            : ReplyHandler(replyType, TYPE_BINARY) {
        bufferId = String(_bufferId);
        binType = _binType;
        clean = _clean;
    };

public:

    ~BinaryReplyHandler() override = default;

    //If clean is true the buffer will have myPixelblazeBuffer.cleanupStream(id/targetStream) called after the call to the
    //appropriate handle() returns, if not it won't. The buffered binary data can be opened with
    //myPixelblazeBuffer.makeReadStream(id), but the stream provided here will be closed after this returns in
    //either case. Note that specifying clean = false can easily cause your buffer to overflow.
    //myPixelblazeBuffer.garbageCollect() will be called if attempting to open a write stream fails, and then
    //opening will be retried. If the buffer stream cannot be created the client will log failures to Serial and all
    //reply handlers that require buffer space will be dropped, though those that don't will continue working.
    bool shouldDeleteBuffer() override {
        return clean;
    }

    //Binary replies never match json
    bool jsonMatches(JsonDocument &json) override {
        return false;
    };

public:
    String bufferId;
    int binType;

private:
    bool clean;
};

/* 
 * ==========================================================================================================================
 * = The rest are user-facing. Users will interact by extending the appropriate handler class and passing it to the client. =
 * = For each handler type a no-op version is provided for instances where only the request side effect is interesting.     =
 * ==========================================================================================================================
*/

/*
 Edge case handler for allowing interaction with arbitrary binary commands if they're unimplemented. The stream provided to 
 handle() is closed after it returns. 
*/
#define HANDLER_RAW_BINARY 1

class RawBinaryHandler : public BinaryReplyHandler {
public:
    RawBinaryHandler(String &bufferId, int binType, bool clean = true)
            : BinaryReplyHandler(HANDLER_RAW_BINARY, bufferId, binType, clean) {};

    ~RawBinaryHandler() override = default;

    virtual void handle(CloseableStream *stream) {};
};

class NoopRawBinaryHandler : public RawBinaryHandler {
public:
    explicit NoopRawBinaryHandler(int binType, bool clean = true, bool _satisfaction = true)
            : RawBinaryHandler(GARBAGE, binType, clean) {
        satisfaction = _satisfaction;
    }

    void handle(CloseableStream *stream) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

/*
 Edge case handler for allowing interaction with arbitrary JSON commands if they're unimplemented. Note that any data extracted
 in handle() must be copied, as it may be overwritten after handle() returns
*/
#define HANDLER_RAW_TEXT 2

class RawTextHandler : public TextReplyHandler {
public:
    RawTextHandler()
            : TextReplyHandler(HANDLER_RAW_TEXT) {};

    ~RawTextHandler() override = default;

    virtual void handle(JsonDocument &json) {};
    //Implementations must also include 'bool jsonMatches(json)';
};

class NoopRawTextHandler : public RawTextHandler {
public:
    explicit NoopRawTextHandler(bool _satisfaction = true) : RawTextHandler() {
        satisfaction = _satisfaction;
    }

    void handle(JsonDocument &json) override {}

    bool jsonMatches(JsonDocument &json) override {
        return false;
    }

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

struct PatternIdentifiers {
    String id;
    String name;
};

class AllPatternIterator {
public:
    explicit AllPatternIterator(
            CloseableStream *stream,
            char *readBuffer,
            size_t bufferLen) : stream(stream),
                                readBuffer(readBuffer),
                                bufferLen(bufferLen) {}

    bool next(PatternIdentifiers &fillMe);

private:
    CloseableStream *stream;
    char *readBuffer;
    size_t bufferLen;
};


#define HANDLER_ALL_PATTERNS 3

class AllPatternsReplyHandler : public BinaryReplyHandler {
public:
    explicit AllPatternsReplyHandler(String &bufferId, bool clean = true)
            : BinaryReplyHandler(HANDLER_ALL_PATTERNS, bufferId, BIN_TYPE_GET_PROGRAM_LIST, clean) {};

    ~AllPatternsReplyHandler() override = default;

    virtual void handle(AllPatternIterator &iterator) {};
};

class NoopAllPatternsReplyHandler : public AllPatternsReplyHandler {
public:
    explicit NoopAllPatternsReplyHandler(bool _satisfaction = true)
            : AllPatternsReplyHandler(GARBAGE) {
        satisfaction = _satisfaction;
    }

    void handle(AllPatternIterator &iterator) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

#define HANDLER_PLAYLIST 4

class PlaylistReplyHandler : public TextReplyHandler {
public:
    PlaylistReplyHandler()
            : TextReplyHandler(HANDLER_PLAYLIST) {};

    ~PlaylistReplyHandler() override = default;

    virtual void handle(Playlist &playlist) {};

    bool jsonMatches(JsonDocument &json) override {
        return json["playlist"].containsKey("position");
    }
};

class NoopPlaylistReplyHandler : public PlaylistReplyHandler {
public:
    explicit NoopPlaylistReplyHandler(bool _satisfaction = true)
            : PlaylistReplyHandler() {
        satisfaction = _satisfaction;
    }

    void handle(Playlist &playlist) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

#define HANDLER_PEERS 5

class PeersReplyHandler : public TextReplyHandler {
public:
    PeersReplyHandler()
            : TextReplyHandler(HANDLER_PEERS) {};

    ~PeersReplyHandler() override = default;

    virtual void handle(Peer *peers, size_t numPeers) {};

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("peers");
    }
};

class NoopPeersReplyHandler : public PeersReplyHandler {
public:
    explicit NoopPeersReplyHandler(bool _satisfaction = true)
            : PeersReplyHandler() {
        satisfaction = _satisfaction;
    }

    void handle(Peer *peers, size_t numPeers) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

#define HANDLER_PREVIEW_IMG 6

class PreviewImageReplyHandler : public BinaryReplyHandler {
public:
    explicit PreviewImageReplyHandler(String &bufferId, bool clean = true)
            : BinaryReplyHandler(HANDLER_PREVIEW_IMG, bufferId, BIN_TYPE_PREVIEW_IMAGE, clean) {};

    ~PreviewImageReplyHandler() override = default;

    virtual void handle(String &patternId, CloseableStream *stream) {};
};

class NoopPreviewImageReplyHandler : public PreviewImageReplyHandler {
public:
    explicit NoopPreviewImageReplyHandler(bool _satisfaction = true)
            : PreviewImageReplyHandler(GARBAGE) {
        satisfaction = _satisfaction;
    }

    void handle(String &patternId, CloseableStream *stream) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

#define HANDLER_SETTINGS 7

class SettingsReplyHandler : public TextReplyHandler {
public:
    SettingsReplyHandler()
            : TextReplyHandler(HANDLER_SETTINGS) {};

    ~SettingsReplyHandler() override = default;

    virtual void handle(Settings &settings) {};

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("pixelCount");
    }
};

class NoopSettingsReplyHandler : public SettingsReplyHandler {
public:
    explicit NoopSettingsReplyHandler(bool _satisfaction = true)
            : SettingsReplyHandler() {
        satisfaction = _satisfaction;
    }

    void handle(Settings &settings) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

#define HANDLER_SEQ 8

class SequencerReplyHandler : public TextReplyHandler {
public:
    SequencerReplyHandler()
            : TextReplyHandler(HANDLER_SEQ) {};

    ~SequencerReplyHandler() override = default;

    virtual void handle(SequencerState &sequencerState) {};

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("activeProgram");
    }
};

class NoopSequencerReplyHandler : public SequencerReplyHandler {
public:
    explicit NoopSequencerReplyHandler(bool _satisfaction = true)
            : SequencerReplyHandler() {
        satisfaction = _satisfaction;
    }

    ~NoopSequencerReplyHandler() override = default;

    void handle(SequencerState &sequencerState) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

#define HANDLER_EXPANDER_CONF 9

class ExpanderConfigReplyHandler : public BinaryReplyHandler {
public:
    explicit ExpanderConfigReplyHandler(String bufferId, bool clean = true)
            : BinaryReplyHandler(HANDLER_EXPANDER_CONF, bufferId, BIN_TYPE_EXPANDER_CONFIG, clean) {};

    ~ExpanderConfigReplyHandler() override = default;

    virtual void handle(ExpanderConfig &expanderConfig) {};
};

class NoopExpanderConfigReplyHandler : public ExpanderConfigReplyHandler {
public:
    explicit NoopExpanderConfigReplyHandler(bool _satisfaction = true)
            : ExpanderConfigReplyHandler(GARBAGE, bufferId) {
        satisfaction = _satisfaction;
    }

    void handle(ExpanderConfig &expanderConfig) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

/**
 * Handles responses to requests to ping the backend.
 *
 * Lots of commands return ack, but we only do anything about it in the case of Ping. We let their acks
 * be just discarded. If this handler picks up an ack from a previous command it could lie about the
 * roundtrip, but that seems worthwhile to not clog the reply queue.
 */
#define HANDLER_PING 10

class PingReplyHandler : public TextReplyHandler {
public:
    PingReplyHandler()
            : TextReplyHandler(HANDLER_PING) {};

    ~PingReplyHandler() override = default;

    virtual void handle(unsigned long roundtripMs) {};

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("ack");  //Lots of commands return this, nothing really to do about it
    }
};

class NoopPingReplyHandler : public PingReplyHandler {
public:
    explicit NoopPingReplyHandler(bool _satisfaction = true)
            : PingReplyHandler() {
        satisfaction = _satisfaction;
    }

    void handle(unsigned long roundtripMs) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

#define HANDLER_PATTERN_CONTROLS 11

class PatternControlReplyHandler : public TextReplyHandler {
public:
    PatternControlReplyHandler()
            : TextReplyHandler(HANDLER_PATTERN_CONTROLS) {};

    ~PatternControlReplyHandler() override = default;

    virtual void handle(String &patternId, Control *controls, size_t numControls) {};
};

class NoopPatternControlReplyHandler : public PatternControlReplyHandler {
public:
    explicit NoopPatternControlReplyHandler(bool _satisfaction = true)
            : PatternControlReplyHandler() {
        satisfaction = _satisfaction;
    }

    void handle(String &patternId, Control *controls, size_t numControls) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

// Not actually used like other handlers, just internally
class PlaylistIndexHandler {
public:
    virtual void handle(int playlistIndex) {};
};

#endif