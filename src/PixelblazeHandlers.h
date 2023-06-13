#ifndef PixelblazeHandlers_h
#define PixelblazeHandlers_h

#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

#include "PixelblazeClient.h"

#define BIN_TYPE_PUT_SOURCE 1
#define BIN_TYPE_PUT_BYTE_CODE 3
#define BIN_TYPE_PREVIEW_IMAGE 4
#define BIN_TYPE_PREVIEW_FRAME 5
#define BIN_TYPE_GET_SOURCE 6
#define BIN_TYPE_GET_PROGRAM_LIST 7
#define BIN_TYPE_PUT_PIXEL_MAP 8
#define BIN_TYPE_EXPANDER_CONFIG 9

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

    virtual bool shouldDeleteBuffer() = 0;

    virtual bool jsonMatches(JsonDocument &json) = 0;

    virtual bool cleanup() {}

    virtual bool isSatisfied() {
        return satisfied;
    }

public:
    int replyType;
    int format;
    uint32_t requestTsMs;
    bool satisfied;
};

/*
  Special case handler that wraps any other handler and signals when it's been completed
*/
#define HANDLER_SYNC 0

class SyncHandler : ReplyHandler {
public:
    SyncHandler(ReplyHandler *_wrappedHandler, bool *_trueWhenFinished)
            : ReplyHandler(HANDLER_SYNC, _wrappedHandler->format) {
        wrappedHandler = _wrappedHandler;
        trueWhenFinished = _trueWhenFinished;
    }

    ~SyncHandler() override {
        delete wrappedHandler;
    }

    ReplyHandler *getWrapped() {
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

private:
    ReplyHandler *wrappedHandler;
    bool *trueWhenFinished;
};

/*
  Base class for all commands that receive JSON as a response, users should never interact
*/
class TextReplyHandler : ReplyHandler {
protected:
    explicit TextReplyHandler(int replyType)
            : ReplyHandler(replyType, TYPE_TEXT) {};

public:
    ~TextReplyHandler() override = default;

    //Text handlers never buffer
    virtual bool shouldDeleteBuffer() {
        return false;
    }
};

/*
  Base class for all commands that receive a possibly multipart binary-encoded message. Users should never interact
*/
class BinaryReplyHandler : ReplyHandler {
protected:
    BinaryReplyHandler(int replyType, String &_bufferId, int _binType, bool _clean)
            : ReplyHandler(replyType, TYPE_BINARY) {
        bufferId = String(_bufferId);
        binType = _binType;
        clean = _clean;
    };

public:

    ~BinaryReplyHandler() override {}

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

class RawBinaryHandler : BinaryReplyHandler {
public:
    RawBinaryHandler(String &bufferId, int binType, bool clean = true)
            : BinaryReplyHandler(HANDLER_RAW_BINARY, bufferId, binType, clean) {};

    ~RawBinaryHandler() override = default;

    virtual void handle(Stream &stream) = 0;
};

class NoopRawBinaryHandler : RawBinaryHandler {
    NoopRawBinaryHandler(String &bufferId, int binType, bool clean = true, bool _satisfaction = true)
            : RawBinaryHandler(bufferId, binType, clean) {
        satisfaction = _satisfaction;
    }

    void handle(Stream &stream) override {}

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

class RawTextHandler : TextReplyHandler {
public:
    RawTextHandler()
            : TextReplyHandler(HANDLER_RAW_TEXT) {};

    ~RawTextHandler() override = default;

    virtual void handle(JsonDocument &json) = 0;
    //Implementations must also include 'bool jsonMatches(json)';
};

class NoopRawTextHandler : RawTextHandler {
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


/*
  Fetches (id, name) info about all available patterns
*/
#define HANDLER_ALL_PATTERNS 3

class AllPatternsReplyHandler : BinaryReplyHandler {
public:
    explicit AllPatternsReplyHandler(String& bufferId, bool clean = true)
            : BinaryReplyHandler(HANDLER_ALL_PATTERNS, bufferId, BIN_TYPE_GET_PROGRAM_LIST, clean) {};

    ~AllPatternsReplyHandler() override = default;

    virtual void handle(AllPatternIterator &iterator) = 0;
};

class NoopAllPatternsReplyHandler : AllPatternsReplyHandler {
public:
    explicit NoopAllPatternsReplyHandler(bool _satisfaction = true)
            : AllPatternsReplyHandler() {
        satisfaction = _satisfaction;
    }

    virtual void handle(AllPatternIterator &iterator) {}

    virtual bool isSatisfied() {
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
    explicit AllPatternIterator(Stream& _stream) {
        stream = _stream;
    }

    bool next(PatternIdentifiers &fillMe);

private:
    Stream& stream;
};

/*
  Handles responses to requests for info on a specific playlist. Note that after returning from handle()
  any data in playlist may change
*/
#define HANDLER_PLAYLIST 4

class PlaylistReplyHandler : TextReplyHandler {
public:
    PlaylistReplyHandler()
            : TextReplyHandler(HANDLER_PLAYLIST) {};

    virtual ~PlaylistReplyHandler() {}

    virtual void handle(Playlist &playlist) = 0;

    virtual bool jsonMatches(JsonDocument &json) {
        return json["playlist"].containsKey("position");
    }
};

class NoopPlaylistReplyHandler : PlaylistReplyHandler {
    NoopPlaylistReplyHandler(bool _satisfaction = true)
            : PlaylistReplyHandler() {
        satisfaction = _satisfaction;
    }

    virtual void handle(Playlist &playlist) {}

    virtual bool isSatisfied() {
        return satisfaction;
    }

private:
    bool satisfaction;
};

/*
  Handle responses to requests for a list of peers on the network. Note that peers returned may be overwritten 
  after handle() returns 
*/
#define HANDLER_PEERS 5

class PeersReplyHandler : TextReplyHandler {
public:
    PeersReplyHandler()
            : TextReplyHandler(HANDLER_PEERS) {};

    ~PeersReplyHandler() override = default;

    virtual void handle(Peer *peers, size_t numPeers) = 0;

    virtual bool jsonMatches(JsonDocument &json) {
        return json.containsKey("peers")
    }
};

class NoopPeersReplyHandler : PeersReplyHandler {
public:
    explicit NoopPeersReplyHandler(bool _satisfaction = true)
            : PeersReplyHandler() {
        satisfaction = _satisfaction;
    }

    virtual void handle(Peer *peers, size_t numPeers) {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

/*
  Handles responses to requests for preview images of a pattern. The provided stream is an 8-bit JPEG file 100px wide by 150px tall
*/
#define HANDLER_PREVIEW_IMG 6

class PreviewImageReplyHandler : BinaryReplyHandler {
public:
    explicit PreviewImageReplyHandler(String &bufferId, bool clean = true)
            : BinaryReplyHandler(HANDLER_PREVIEW_IMG, bufferId, BIN_TYPE_PREVIEW_IMAGE, clean) {};

    ~PreviewImageReplyHandler() override = default;

    virtual void handle(String &patternId, Stream &stream) = 0;
};

class NoopPreviewImageReplyHandler : PreviewImageReplyHandler {
public:
    NoopPreviewImageReplyHandler(bool _satisfaction = true)
            : PreviewImageReplyHandler() {
        satisfaction = _satisfaction;
    }

    void handle(String &patternId, Stream &stream) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

/*
  Handles responses to requests for settings. Note that data in the provided Settings object may be 
  overwritten after handle() returns 
*/
#define HANDLER_SETTINGS 7

class SettingsReplyHandler : TextReplyHandler {
public:
    SettingsReplyHandler()
            : TextReplyHandler(HANDLER_SETTINGS) {};

    ~SettingsReplyHandler() override = default;

    virtual void handle(Settings &settings) = 0;

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("pixelCount");
    }
};

class NoopSettingsReplyHandler : SettingsReplyHandler {
public:
    explicit NoopSettingsReplyHandler(bool _satisfaction = true)
            : SettingsReplyHandler() {
        satisfaction = _satisfaction;
    }

    virtual void handle(Settings &settings) {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

/*
  Handles responses to requests for info about the current sequencer state. Note that data in the
  provided SequencerState may be overwritten after handle() returns.
*/
#define HANDLER_SEQ 8

class SequencerReplyHandler : TextReplyHandler {
    SequencerReplyHandler()
            : TextReplyHandler(HANDLER_SEQ) {};

    virtual ~SequencerReplyHandler() {}

    virtual void handle(SequencerState &sequencerState) = 0;

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("activeProgram")
    }
};

class NoopSequencerReplyHandler : SequencerReplyHandler {
public:
    explicit NoopSequencerReplyHandler(bool _satisfaction = true)
            : SequencerReplyHandler() {
        satisfaction = _satisfaction;
    }

    ~NoopSequencerReplyHandler() override = default;

    virtual void handle(SequencerState &sequencerState) {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

/*
  Handles responses to requests for info on the configuration of the output expander. Note that data in 
  ExpanderConfig may be overwritten after handle() returns
*/
#define HANDLER_EXPANDER_CONF 9

class ExpanderConfigReplyHandler : BinaryReplyHandler {
public:
    explicit ExpanderConfigReplyHandler(String bufferId, bool clean = true)
            : BinaryReplyHandler(HANDLER_EXPANDER_CONF, bufferId, BIN_TYPE_EXPANDER_CONFIG, clean) {};

    ~ExpanderConfigReplyHandler() override = default;

    virtual void handle(ExpanderConfig &expanderConfig) = 0;
};

class NoopExpanderConfigReplyHandler : ExpanderConfigReplyHandler {
public:
    explicit NoopExpanderConfigReplyHandler(bool _satisfaction = true)
            : ExpanderConfigReplyHandler() {
        satisfaction = _satisfaction;
    }

    virtual void handle(ExpanderConfig &expanderConfig) {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

/*
  Handles responses to requests to ping the backend.

  Lots of commands return ack, but we only do anything about it in the case of Ping. We let their acks
  be just discarded. If this handler picks up an ack from a previous command it could lie about the
  roundtrip, but that seems worthwhile to not clog the reply queue.
*/
#define HANDLER_PING 10

class PingReplyHandler : TextReplyHandler {
public:
    PingReplyHandler()
            : TextReplyHandler(HANDLER_PING) {};

    ~PingReplyHandler() override = default;

    virtual void handle(int roundtripMs) = 0;

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("ack");  //Lots of commands return this, nothing really to do about it
    }
};

class NoopPingReplyHandler : PingReplyHandler {
public:
    explicit NoopPingReplyHandler(bool _satisfaction = true)
            : PingReplyHandler() {
        satisfaction = _satisfaction;
    }

    void handle(int roundtripMs) override {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

/*
  Handles responses to requests for the state of the controls for a given pattern. Note that data in returned
  Controls may be overwritten after handle() returns 
*/
#define HANDLER_PATTERN_CONTROLS 11

class PatternControlReplyHandler : TextReplyHandler {
public:
    PatternControlReplyHandler()
            : TextReplyHandler(HANDLER_PATTERN_CONTROLS) {};

    ~PatternControlReplyHandler() override = default;

    virtual void handle(Control *controls, int numControls) = 0;
};

class NoopPatternControlReplyHandler : PatternControlReplyHandler {
public:
    explicit NoopPatternControlReplyHandler(bool _satisfaction = true)
            : PatternControlReplyHandler() {
        satisfaction = _satisfaction;
    }

    virtual void handle(Control *controls, int numControls) {}

    bool isSatisfied() override {
        return satisfaction;
    }

private:
    bool satisfaction;
};

// Not actually used like other handlers, just internally
class PlaylistIndexHandler {
public:
    virtual void handle(int playlistIndex) = 0;
};

#endif