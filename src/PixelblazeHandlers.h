#ifndef PixelblazeHandlers_h
#define PixelblazeHandlers_h

#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

#include "PixelblazeCommon.h"
#include "PixelblazeClient.h"

#define EXPANDER_CHANNEL_BYTE_WIDTH 12

static String GARBAGE = "GARBAGE";

/*
  Base class for all objects which signify a command waiting for a response. Library users should never see this name
*/
class ReplyHandler {
protected:
    ReplyHandler(ReplyHandlerType replyType, WebsocketFormat format) : type(replyType), format(format) {
        requestTsMs = millis();
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

    virtual void reportFailure(FailureCause cause) {}

    virtual bool isSatisfied() {
        return satisfied;
    }

public:
    WebsocketFormat format;
    ReplyHandlerType type;

    unsigned long requestTsMs;
    bool satisfied;
};

/*
  Special case handler that wraps any other handler and signals when it's been completed
*/
class SyncHandler : public ReplyHandler {
public:
    SyncHandler(ReplyHandler *_wrappedHandler, bool *_trueWhenFinished)
            : ReplyHandler(ReplyHandlerType::Sync, _wrappedHandler->format) {
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
    explicit TextReplyHandler(ReplyHandlerType type)
            : ReplyHandler(type, WebsocketFormat::Text) {};

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
    BinaryReplyHandler(ReplyHandlerType replyType, String &_bufferId, int rawBinType, bool clean)
            : rawBinType(rawBinType), clean(clean), ReplyHandler(replyType, WebsocketFormat::Binary) {
        bufferId = String(_bufferId);
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
    int rawBinType;

private:
    bool clean;
};

/**
 * Edge case handler for allowing interaction with arbitrary binary-fetching commands if they're unimplemented.
 * The stream provided to handle() is closed after it returns.
 */
class RawBinaryHandler : public BinaryReplyHandler {
public:
    RawBinaryHandler(String &bufferId, int rawBinType, bool clean = true)
            : BinaryReplyHandler(ReplyHandlerType::RawBinary, bufferId, rawBinType, clean) {};

    ~RawBinaryHandler() override = default;

    virtual void handle(CloseableStream *stream) {};
};

/**
 * Edge case handler for allowing interaction with arbitrary JSON commands if they're unimplemented.
 */
class RawTextHandler : public TextReplyHandler {
public:
    RawTextHandler()
            : TextReplyHandler(ReplyHandlerType::RawText) {};

    ~RawTextHandler() override = default;

    virtual void handle(JsonDocument &json) {};
    //Implementations must also include 'bool jsonMatches(json)';
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

class AllPatternsReplyHandler : public BinaryReplyHandler {
public:
    explicit AllPatternsReplyHandler(void (*handleFn)(AllPatternIterator &), String &bufferId, bool clean,
                                     void (*onError)(FailureCause))
            : handleFn(handleFn), onError(onError),
              BinaryReplyHandler(ReplyHandlerType::AllPatterns, bufferId,
                                 (int) BinaryMsgType::GetProgramList, clean) {};

    ~AllPatternsReplyHandler() override = default;

    void handle(AllPatternIterator &iterator) {
        handleFn(iterator);
    };

    void reportFailure(FailureCause cause) override {
        onError(cause);
    }

private:
    void (*handleFn)(AllPatternIterator &);

    void (*onError)(FailureCause);
};

class PlaylistReplyHandler : public TextReplyHandler {
public:
    explicit PlaylistReplyHandler(void (*handlerFn)(Playlist &), void (*onError)(FailureCause))
            : handlerFn(handlerFn), onError(onError), TextReplyHandler(ReplyHandlerType::Playlist) {};

    ~PlaylistReplyHandler() override = default;

    void handle(Playlist &playlist) {
        handlerFn(playlist);
    };

    void reportFailure(FailureCause cause) override {
        onError(cause);
    }

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("playlist") && json["playlist"].containsKey("position");
    }

private:
    void (*handlerFn)(Playlist &);

    void (*onError)(FailureCause);
};

class PeersReplyHandler : public TextReplyHandler {
public:
    PeersReplyHandler(void (*handlerFn)(Peer *, size_t), void (*onError)(FailureCause))
            : handlerFn(handlerFn), onError(onError), TextReplyHandler(ReplyHandlerType::Peers) {};

    ~PeersReplyHandler() override = default;

    void handle(Peer *peers, size_t numPeers) {
        handlerFn(peers, numPeers);
    };

    void reportFailure(FailureCause cause) override {
        onError(cause);
    }

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("peers");
    }

private:
    void (*handlerFn)(Peer *, size_t);

    void (*onError)(FailureCause);
};

class PreviewImageReplyHandler : public BinaryReplyHandler {
public:
    explicit PreviewImageReplyHandler(String &patternId, void (*handlerFn)(String &, CloseableStream *), bool clean,
                                      void (*onError)(FailureCause))
            : handlerFn(handlerFn), onError(onError),
              BinaryReplyHandler(ReplyHandlerType::PreviewImage, patternId,
                                 (int) BinaryMsgType::PreviewImage, clean) {};

    ~PreviewImageReplyHandler() override = default;

    void handle(String &patternId, CloseableStream *stream) {
        handlerFn(patternId, stream);
    };

    void reportFailure(FailureCause cause) override {
        onError(cause);
    }

private:
    void (*handlerFn)(String &, CloseableStream *);

    void (*onError)(FailureCause);
};

class SettingsReplyHandler : public TextReplyHandler {
public:
    SettingsReplyHandler(void (*handlerFn)(Settings &), void (*onError)(FailureCause))
            : handlerFn(handlerFn), onError(onError), TextReplyHandler(ReplyHandlerType::Settings) {};

    ~SettingsReplyHandler() override = default;

    void handle(Settings &settings) {
        handlerFn(settings);
    };

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("pixelCount");
    }

    void reportFailure(FailureCause cause) override {
        onError(cause);
    }

private:
    void (*handlerFn)(Settings &);

    void (*onError)(FailureCause);
};

class SequencerReplyHandler : public TextReplyHandler {
public:
    SequencerReplyHandler(void (*handlerFn)(SequencerState &), void (*onError)(FailureCause))
            : handlerFn(handlerFn), onError(onError), TextReplyHandler(ReplyHandlerType::Sequencer) {};

    ~SequencerReplyHandler() override = default;

    void handle(SequencerState &sequencerState) {
        handlerFn(sequencerState);
    };

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("activeProgram");
    }

    void reportFailure(FailureCause cause) override {
        onError(cause);
    }

private:
    void (*handlerFn)(SequencerState &);

    void (*onError)(FailureCause);
};

class ExpanderChannelsReplyHandler : public BinaryReplyHandler {
public:
    explicit ExpanderChannelsReplyHandler(void (*handlerFn)(ExpanderChannel *, size_t), String bufferId, bool clean,
                                          void (*onError)(FailureCause))
            : handlerFn(handlerFn), onError(onError),
              BinaryReplyHandler(ReplyHandlerType::Expander, bufferId,
                                 (int) BinaryMsgType::ExpanderChannels, clean) {};

    ~ExpanderChannelsReplyHandler() override = default;

    void handle(ExpanderChannel *channels, size_t channelCount) {
        handlerFn(channels, channelCount);
    };

    void reportFailure(FailureCause cause) override {
        onError(cause);
    }

private:
    void (*handlerFn)(ExpanderChannel *, size_t);

    void (*onError)(FailureCause);
};

/**
 * Handles responses to requests to ping the backend.
 *
 * Lots of commands return ack, but we only do anything about it in the case of Ping. We let their acks
 * be just discarded. If this handler picks up an ack from a previous command it could lie about the
 * roundtrip, but that seems worthwhile to not clog the reply queue.
 */
class PingReplyHandler : public TextReplyHandler {
public:
    explicit PingReplyHandler(void (*handlerFn)(uint32_t), void (*onError)(FailureCause))
            : handlerFn(handlerFn), onError(onError), TextReplyHandler(ReplyHandlerType::Ping) {};

    ~PingReplyHandler() override = default;

    void handle(uint32_t roundtripMs) {
        handlerFn(roundtripMs);
    };

    bool jsonMatches(JsonDocument &json) override {
        return json.containsKey("ack");  //Lots of commands return this, nothing really to do about it
    }

    void reportFailure(FailureCause cause) override {
        onError(cause);
    }

private:
    void (*handlerFn)(uint32_t);

    void (*onError)(FailureCause);
};

class PatternControlReplyHandler : public TextReplyHandler {
public:
    PatternControlReplyHandler(void (*handlerFn)(String &, Control *, size_t), void (*onError)(FailureCause))
            : handlerFn(handlerFn), onError(onError), TextReplyHandler(ReplyHandlerType::PatternControls) {};

    ~PatternControlReplyHandler() override = default;

    void handle(String &patternId, Control *controls, size_t numControls) {
        handlerFn(patternId, controls, numControls);
    };

    void reportFailure(FailureCause cause) override {
        onError(cause);
    }

private:
    void (*handlerFn)(String &, Control *, size_t);

    void (*onError)(FailureCause);
};

#endif