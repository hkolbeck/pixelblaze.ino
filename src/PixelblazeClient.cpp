#include "PixelblazeClient.h"
#include "PixelblazeHandlers.h"

#include <ctype.h>
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>

PixelblazeClient::PixelblazeClient(
        WebSocketClient& _wsClient,
        PixelblazeBuffer& _binaryBuffer,
        PixelblazeUnrequestedHandler& _unrequestedHandler,
        ClientConfig& _clientConfig = defaultConfig) {

    wsClient = _wsClient;
    binaryBuffer = _binaryBuffer;
    unrequestedHandler = _unrequestedHandler;
    clientConfig = _clientConfig;

    json = doc(clientConfig.jsonBufferBytes);
    byteBuffer = new uint8_t[clientConfig.binaryBufferBytes];
    previewFrameBuff = new uint8_t[clientConfig.framePreviewBufferBytes];
    textReadBuffer = new char[clientConfig.textReadBufferBytes];
    controls = new Control[clientConfig.controlLimit];
    peers = new Peer[clientConfig.peerLimit];
    replyQueue = new ReplyHandler*[clientConfig.replyQueueSize];
    sequencerState = new Control[clientConfig.controlLimit] playlist.items = new PlaylistItem[clientConfig.playlistLimit];
    playlistUpdate.items = new PlaylistItem[clientConfig.playlistLimit];
}

PixelblazeClient::~PixelblazeClient() {
    while (queueLength() > 0) {
        delete replyQueue[queueFront];
        queueFront = (queueFront + 1) % clientConfig.replyQueueSize;
    }

    delete[] byteBuffer;
    delete[] previewFrameBuffer;
    delete[] textReadBuffer;
    delete[] controls;
    delete[] peers;
    delete[] replyQueue;
    delete[] playlist.items;
    delete[] playlistUpdate.items;
}

bool PixelblazeClient::connected() {
    return wsClient.connected();
}

bool PixelblazeClient::connectionMaintenance() {
    if (connected) {
        return true;
    }

    //TODO: retries n' stuff
    return wsClient.begin("/") == 0;
}

bool PixelblazeClient::getPatterns(AllPatternsReplyHandler& replyHandler) {
    AllPatternsReplyHandler* myHandler = new AllPatternsReplyHandler(replyHandler);
    myHandler.requestTsMs = time();
    myHandler.satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false
    }

    json.clear();
    json["listPrograms"] = true;
    sendJson(json);
    return true;
}

bool PixelblazeClient::getPlaylist(PlaylistReplyHandler& replyHandler, String& playlistName = "_defaultplaylist_") {
    PlaylistReplyHandler* myHandler = new PlaylistReplyHandler(replyHandler);
    myHandler.requestTsMs = time();
    myHandler.satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    json.clear();
    json["getPlaylist"] = playlistName;
    sendJson(json);
    return true;
}

class PlaylistIndexExtractor : PlaylistReplyHandler {
public:
    PlaylistIndexExtractor(PlaylistIndexHandler& _indexHandler)
            : PlaylistReplyHandler() {
        indexHandler = new PlaylistIndexHandler(_indexHandler);
    };

    virtual ~PlaylistIndexExtractor() {}

    virtual void handle(PlayList& playlist) {
        indexHandler.handle(playlist.index);
        delete indexHandler; //If not done here the wrapped handler would get freed when getPlaylist() returns
    }

private:
    PlaylistIndexHandler* indexHandler;
};

bool PixelblazeClient::getPlaylistIndex(PlaylistIndexHandler& replyHandler) {
    PlaylistIndexExtractor extractor = PlaylistIndexExtractor(replyHandler);
    return getPlaylist(extractor);
}

void PixelblazeClient::setPlaylistIndex(int idx) {
    json.clear();
    JsonObject pl = json.createNestedObject("playlist");
    pl["position"] = idx;
    sendJson(json);
}

void PixelblazeClient::nextPattern() {
    json.clear();
    json["nextProgram"] = true;
    sendJson(json);
}

class PrevPlaylistReplyHandler : PlaylistReplyHandler {
public:
    PrevPlaylistReplyHandler(PixelblazeClient* _client)
            : PlaylistReplyHandler() {
        client = _client;
    }

    virtual void handle(PlayList& playlist) {
        if (playlist.position == 0) {
            client.setPlaylistIndex(playlist.numItems - 1);
        } else {
            client.setPosition(playlist.position - 1);
        }
    }

private:
    PixelblazeClient* client;
};

//This is ugly ATM because we don't cache anything. Leaving it void instead of bool because it won't surprise
//me if a future library update adds a standalone prev command
void PixelblazeClient::prevPattern() {
    PrevPlaylistReplyHandler prevHandler = PrevPlaylistReplyHandler(this);
    getPlaylist(prevHandler);
}

void PixelblazeClient::playSequence() {
    json.clear();
    json["runSequencer"] = true;
    sendJson(json);
}

void PixelblazeClient::pauseSequence() {
    json.clear();
    json["runSequencer"] = false;
    sendJson(json);
}

void PixelblazeClient::setSequencerMode(int sequencerMode) {
    json.clear();
    json["sequencerMode"] = sequencerMode;
    sendJson(json);
}

bool PixelblazeClient::getPeers(PeersReplyHandler& replyHandler) {
    PeersReplyHandler* myHandler = new PeersReplyHandler(replyHandler);
    myHandler.requestTsMs = time();
    myHandler.satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    json.clear();
    json["getPeers"] = 1;
    sendJson(json);
    return true;
}

void PixelblazeClient::setCurrentPatternControls(Control* controls, int numControls, bool saveToFlash) {
    json.clear();
    JsonObject controls = json.createNestedObject("setControls");
    for (int idx = 0; idx < numControls; idx++) {
        controls[controls[idx].name] = controls[idx].value;
    }

    json["save"] = saveToFlash;
    sendJson(json);
}

void PixelblazeClient::setCurrentPatternControl(String& controlName, float value, bool saveToFlash) {
    json.clear();
    JsonObject controls = json.createNestedObject("setControls");
    controls[controlName] = value;
    json["save"] = saveToFlash;
    sendJson(json);
}

void PixelblazeClient::setBrightness(float brightness, bool saveToFlash) {
    json.clear();
    json["brightness"] = constrain(brightness, 0, 1);
    json["save"] = saveToFlash;
    sendJson(json);
}

bool PixelblazeClient::getPatternControls(String& patternId, PatternControlReplyHandler& replyHandler) {
    PatternControlReplyHandler myHandler = new PatternControlReplyHandler(replyHandler);
    myHandler.requestTsMs = time();
    myHandler.satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    json.clear();
    json["getControls"] = patternId;
    sendJson(json);

    return true;
}

class CurrentControlsReplyExtractor : SequencerReplyHandler {
public:
    CurrentControlsReplyExtractor(PatternControlReplyHandler* _wrapped)
            : SequencerReplyHandler() {
        wrapped = _wrapped;
    }

    virtual ~CurrentControlsReplyExtractor() {}

    virtual void handle(SequencerState& sequencerState) {
        wrapped.handle(sequencerState.controls, sequencerState.controlCount);
        delete wrapped; //If not done here the wrapped handler would get freed when getCurrentPatternControls() returns
    }

private:
    PatternControlReplyHandler* wrapped;
};

bool PixelblazeClient::getCurrentPatternControls(PatternControlReplyHandler& replyHandler) {
    PatternControlReplyHandler* myHandler = new PatternControlReplyHandler(replyHandler);
    CurrentControlsReplyExtractor
}

bool PixelblazeClient::getPreviewImage(String& patternId, PreviewImageReplyHandler& replyHandler) {
    PreviewImageReplyHandler* myReplyHandler = new PreviewImageReplyHandler(replyHandler);
    myHandler.requestTsMs = time();
    myHandler.satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    json.clear();
    json["getPreviewImg"] = patternId;
    sendJson(json);
    return true;
}

void PixelblazeClient::setBrightnessLimit(int value, bool saveToFlash) {
    json.clear();
    json["maxBrightness"] = constrain(value, 0, 100);
    json["save"] = saveToFlash;
    sendJson(json);
}

void PixelblazeClient::setPixelCount(uint32_t pixels, bool saveToFlash) {
    json.clear();
    json["pixelCount"] = pixels;
    json["save"] = saveToFlash;
    sendJson(json);
}

bool PixelblazeClient::getSettings(SettingsReplyHandler& settingsHandler, SequencerReplyHandler& seqHandler, ExpanderConfigReplyHandler& expanderHandler, int watchResponses) {
    int timeMs = time();

    SettingsReplyHandler* mySettingsHandler = new SettingsReplyHandler(settingsHandler);
    mySettingsHandler.requestTsMs = timeMs;
    mySettingsHandler.satisfied = false;
    if (!(watchResponses & WATCH_CONFIG_REQ)) {
        mySettingsHandler.satisfied = true;
    }

    SequencerReplyHandler* mySeqHandler = new SequencerReplyHandler(sequencerReplyHandler);
    mySeqHandler.requestTsMs = timeMs;
    mySeqHandler.satisfied = false;
    if (!(watchResponses & WATCH_SEQ_REQ)) {
        mySeqHandler.satisfied = true;
    }

    ExpanderConfigReplyHandler* myExpanderHandler = new ExpanderConfigReplyHandler(myExpanderHandler);
    myExpanderHandler.requestTsMs = timeMs;
    myExpanderHandler.satisfied = false;
    if (!(watchResponses & WATCH_EXPANDER_REQ)) {
        myExpanderHandler.satisfied = true;
    }

    if (!enqueueReplies(3, mySettingsHandler, mySeqHandler, myExpanderHandler)) {
        delete mySettingsHandler;
        delete mySeqHandler;
        delete myExpanderHandler;
        return false;
    }

    json.clear();
    json["getConfig"] = true;
    sendJson(json);
    return true;
}

bool PixelblazeClient::ping(PingHandler replyHandler) {
    PingHandler* myHandler = new PingHandler(replyHandler);
    myHandler.requestTsMs = time();
    myHandler.satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    json.clear();
    json["ping"] = true;
    sendJson(json);
    return true;
}

void PixelblazeClient::sendFramePreviews(bool sendEm) {
    json.clear();
    json["sendUpdates"] = sendEm;
    sendJson(json);
}

bool PixelblazeClient::getRawBinary(RawBinaryHandler& replyHandler, JsonDocument& request) {
    RawBinaryHandler* myHandler = new RawBinaryHandler(replyHandler);
    myHandler.requestTsMs = time();
    myHandler.satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    sendJson(request);
    return true;
}

bool PixelblazeClient::getRawText(RawTextHandler& replyHandler, JsonDocument& request) {
    RawTextHandler* myHandler = new RawTextHandler(replyHandler);
    myHandler.requestTsMs = time();
    myHandler.satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    sendJson(request);
    return true;
}

void PixelblazeClient::weedExpiredReplies() {
    uint32_t currentTimeMs = time();
    while (queueLength() > 0) {
        if (replyQueue[queueFront].isSatisfied() || replyQueue[queueFront].requestTsMs + clientConfig.maxResponseWaitMs < currentTimeMs) {
            queueFront = (queueFront + 1) % clientConfig.replyQueueSize;
        } else {
            return;
        }
    }
}

void PixelblazeClient::checkForInbound() {
    weedExpiredReplies();
    uint32_t startTime = time();

    int read = wsClient.parseMessage();
    while (read > 0 && startTime + checkDurationMs > time()) {
        int format = wsClient.messageType();
        while (queueLength() > 0 && replyQueue[queueFront].isSatisfied()) {
            dequeueReply();
        }

        if (queueLength() == 0) {
            //Nothing expected, dispatch everything through unrequested functions
            if (format == FORMAT_TEXT) {
                DeserializationError deErr = deserializeJson(json, wsClient.readString());
                if (deErr) {
                    Serial.print("Message deserialization error: ");
                    Serial.println(deErr.f_str());
                } else {
                    handleUnrequestedJson();
                }
            } else if (format == FORMAT_BINARY && wsClient.available() > 0) {
                handleUnrequestedBinary(wsClient.read());
            } else {
                Serial.print("Unexpected reply format: ");
                Serial.println(format);
            }
        } else {
            int soughtFormat = replyQueue[queueFront].format;

            int repliesExamined = 0;
            int startQueueLen = queueLength;
            while (repliesExamined <= queueLength()
                   && soughtFormat == FORMAT_BINARY
                   && ((BinaryReplyHandler*)replyQueue[queueFront]).binType == BIN_TYPE_EXPANDER_CONFIG
                   && (format != FORMAT_BINARY || wsClient.peek() != BIN_TYPE_EXPANDER_CONFIG)) {
                //Expander configs can be non-optionally fetched by getConfig, and may never come if no expander is installed.
                //If the head of the queue is seeking them and the current message isn't one, bump it to the back of the queue.
                //This does require special handling in handleUnrequestedBinary(). If it's the only thing in the queue we'll be
                //thrashing it a bit, but that should be fine.

                ReplyHandler* expanderHandler = replyQueue[queueFront];
                replyQueue[queueFront] = NULL;
                queueFront = (queueFront + 1) % clientConfig.replyQueueSize;
                enqueueReply(expanderHandler);

                soughtFormat = replyQueue[queueFront].format;
                repliesExamined++;
            }

            if (soughtFormat != FORMAT_TEXT && soughtFormat != FORMAT_BINARY) {
                Serial.print("Unexpected sought format: ");
                Serial.println(soughtFormat);
                dequeueReply();
            } else if (format == FORMAT_TEXT) {
                if (soughtFormat == FORMAT_TEXT) {
                    seekingTextHasText();
                } else {
                    seekingBinaryHasText();
                }
            } else if (format == FORMAT_BINARY) {
                if (soughtFormat == FORMAT_TEXT) {
                    seekingTextHasBinary();
                } else {
                    seekingBinaryHasBinary();
                }
            } else {
                Serial.print("Unexpected reply format: ");
                Serial.println(format);
                dequeueReply();
            }
        }

        read = wsClient.parseMessage();
    }
}

void seekingTextHasText() {
    DeserializationError deErr = deserializeJson(json, wsClient.readString());
    if (deErr) {
        Serial.print("Message deserialization error: ");
        Serial.println(deErr.f_str());
    } else {
        if (replyQueue[queueFront].jsonMatches(json)) {
            dispatchTextReply(replyQueue[queueFront]);
            dequeueReply();
        } else {
            handleUnrequestedJson();
        }
    }
}

void seekingTextHasBinary() {
    handleUnrequestedBinary(wsClient.read());
}

void seekingBinaryHasBinary() {
    BinaryReplyHandler* binaryHandler = (BinaryReplyHandler*)replyQueue[queuePos];
    int frameType = wsClient.read();
    if (frameType < 0) {
        Serial.println("Empty binary body received");
    } else if (binaryReadType < 0) {
        //We've read nothing so far, blank slate
        if (frameType == binaryHandler.binType) {
            int frameFlag = wsClient.read();
            if (frameFlag & FRAME_FIRST & FRAME_LAST) {
                //Lone message
                if (readBinaryToStream(binaryHandler.bufferId, false)) {
                    dispatchBinaryReply(replyQueue[queueFront]);
                }
                if (replyQueue[queueFront].shouldDeleteBuffer()) {
                    binaryBuffer.cleanupStream(binaryHandler.bufferId);
                }
                dequeueReply();
            } else if (frameFlag & FRAME_FIRST) {
                if (!readBinaryToStream(binaryHandler.bufferId, false)) {
                    binaryBuffer.cleanupStream(binaryHandler.bufferId);
                    dequeueReply();
                    return;
                }
                binaryReadType = frameType;
            } else {
                //Frame was middle, last, or 0, none of which should happen
                Serial.print("Got unexpected frameFlag: ");
                Serial.print(frameFlag);
                Serial.print("For frameType: ");
                Serial.println(frameType);
            }
        } else {
            handleUnrequestedBinary(frameType);
        }
    } else if (frameType == binaryReadType) {
        //We're mid read and the latest is compatible
        int frameFlag = wsClient.read();
        if (frameFlag & FRAME_LAST) {
            if (readBinaryToStream(binaryHandler.bufferId, true)) {
                dispatchBinaryReply(replyQueue[queueFront]);
            }

            if (replyQueue[queueFront].shouldDeleteBuffer()) {
                binaryBuffer.cleanupStream(binaryHandler.bufferId);
            }
            dequeueReply();
        } else if (frameFlag & FRAME_MIDDLE) {
            if (!readBinaryToStream(binaryHandler.bufferId, true)) {
                binaryBuffer.cleanupStream(binaryHandler.bufferId);
                dequeueReply();
                return;
            }
        } else {
            //Frame was first or 0, neither of which should happen
            Serial.print("Got unexpected frameFlag: ");
            Serial.print(frameFlag);
            Serial.print("For frameType: ");
            Serial.println(frameType);
        }
    } else {
        //We're mid read and just got an incompatible frame
        if (!handleUnrequestedBinary(frameType)) {
            Serial.print("Expected frameType: ");
            Serial.print(binaryReadType);
            Serial.print(" but got: ");
            Serial.println(frameType);

            //Scrap the current read, if the finisher never comes it would drop requested events until weeded
            binaryBuffer.cleanupStream(binaryHandler.bufferId);
            dequeueReply();
            binaryReadType = -1;
        }
    }
}

void seekingBinaryHasText() {
    DeserializationError deErr = deserializeJson(json, wsClient.readString());
    if (deErr) {
        Serial.print("Message deserialization error: ");
        Serial.println(deErr.f_str());
    } else {
        handleUnrequestedJson();
    }
}

bool PixelblazeClient::readBinaryToStream(String& bufferId, bool append) {
    Stream stream = binaryBuffer.makeWriteStream(bufferId, append);
    if (!stream) {
        Serial.println("Couldn't open write stream, attempting to garbage collect");
        binaryBuffer.garbageCollect();
        stream = binaryBuffer.makeWriteStream(bufferId, append);
    }

    if (!stream) {
        Serial.print("Failed to get write stream for: ");
        Serial.println(bufferId);
        return false;
    }

    int available = wsClient.available();
    while (available > 0) {
        int bytesRead = wsClient.read(&byteBuffer, min(clientConfig.binaryBufferBytes, available));
        size_t written = stream.write(&byteBuffer, bytesRead);
        if (bytesRead != written) {
            Serial.print("Partial write on stream for bufferId: ");
            Serial.println(bufferId);
            return false;
        }

        available -= bytesRead;
    }

    stream.close();
    return true;
}

void PixelblazeClient::dispatchTextReply(ReplyHandler* genHandler) {
    ReplyHandler* handler = genHandler;
    if (genHandler.replyType == HANDLER_SYNC) {
        SyncHandler* syncHandler = (SyncHandler*)genHandler;
        syncHandler.finish();
        handler = syncHandler.getWrapped();
    }

    switch (handler.replyType) {
        case HANDLER_RAW_TEXT:
            RawTextHandler* specHandler = (RawTextHandler*)handler;
            spacHandler.handle(json);
            break;
        case HANDLER_PLAYLIST:
            PlaylistReplyHandler* specHandler = (PlaylistReplyHandler*)handler;
            JsonObject playlistObj = json["playlist"];
            playlist.id = playlistObj["id"];
            playlist.position = playlistObj["position"];
            playlist.currentDurationMs = playlistObj["ms"];
            playlist.remainingCurrentMs = playlistObj["remainingMs"];

            JsonArray items = playlistObj["items"];
            int itemIdx = 0;
            for (JsonVariant v : items) {
                JsonObject itemObj = v.as<JsonObject>();
                playlist.items[itemIdx].id = itemObj["id"];
                playlist.items[itemIdx].durationMs = itemObj["ms"];
                itemIdx++;
                if (itemIdx >= clientConfig.playlistLimit) {
                    Serial.print("Got too many patterns on playlist to store: ");
                    Serial.print(items.size());
                    break;
                }
            }
            playlist.numItems = itemIdx;

            specHandler.handle(playlist);
            break;
        case HANDLER_PEERS:
            PeerReplyHandler* specHandler = (PeerReplyHandler*)handler;
            //TODO, and remember to bounds check against clientConfig.peerLimit
            specHandler.handler(peers, peerCount);
            break;
        case HANDLER_SETTINGS:
            SettingsReplyHandler* specHandler = (SettingsReplyHandler*)handler;
            settings.name = json["name"];
            settings.brandName = json["brandName"];
            settings.pixelCount = json["pixelCount"];
            settings.brightness = json["brightness"];
            settings.maxBrightness = json["maxBrightness"];
            settings.colorOrder = json["colorOrder"];
            settings.dataSpeed = json["dataSpeed"];
            settings.ledType = json["ledType"];
            settings.sequenceTimer = json["sequenceTimer"];
            settings.transitionDuration = json["transitionDuration"];
            settings.sequencerMode = json["sequencerMode"];
            settings.runSequencer = json["runSequencer"];
            settings.simpleUiMode = json["simpleUiMode"];
            settings.learningUiMode = json["learningUiMode"];
            settings.discoveryEnable = json["discoveryEnable"];
            settings.timezone = json["timezone"];
            settings.autoOffEnable = json["autoOffEnable"];
            settings.autoOffStart = json["autoOffStart"];
            settings.autoOffEnd = json["autoOffEnd"];
            settings.cpuSpeedMhz = json["cpuSpeed"];
            settings.networkPowerSave = json["networkPowerSave"];
            settings.mapperFit = json["mapperFit"];
            settings.leaderId = json["leaderId"];
            settings.nodeId = json["nodeId"];
            settings.soundSrc = json["soundSrc"];
            settings.accelSrc = json["accelSrc"];
            settings.lightSrc = json["lightSrc"];
            settings.exp = json["exp"];
            settings.ver = json["ver"];
            settings.chipId = json["chipId"];

            specHandler.handle(settings);
            break;
        case HANDLER_SEQ:
            SeqReplyHandler* specHandler = (SeqReplyHandler*)handler;
            parseSequencerState();
            specHandler.handle(sequencerState);
            break;
        case HANDLER_PING:
            PingReplyHandler* specHandler = (PingReplyHandler*)handler;
            specHandler.handle(time() - specHandler.requestTsMs);
            break;
        case HANDLER_PATTERN_CONTROLS:
            break;
        default:
            Serial.print("Got unexpected text reply type: ");
            Serial.println(handler.replyType);
    }
}

void PixelblazeClient::parseSequencerState() {
    JsonObject activeProgram = json["activeProgram"];
    sequencerState.name = activeProgram["name"];
    sequencerState.activeProgramId = activeProgram["activeProgramId"];

    JsonObject controlsObj = activeProgram["controls"];
    int controlIdx = 0;
    for (JsonPair kv : controlsObj) {
        sequencerState.controls[controlIdx].name = kv.key();
        sequencerState.controls[controlIdx].value = kv.value();
        controlIdx++;
        if (controlIdx >= clientConfig.controlLimit) {
            Serial.print("Got more controls than could be saved: ");
            Serial.println(controlsObj.size());
            break;
        }
    }
    sequencerState.controlCount = controlIdx;

    sequencerState.sequencerMode = json["sequencerMode"];
    sequencerState.runSequencer = json["runSequencer"];

    JsonObject playlistObj = json["playlist"];
    sequencerState.playlistPos = playlistObj["position"];
    sequencerState.playlistId = playlistObj["id"];
    sequencerState.ttlMs = playlistObj["ms"];
    sequencerState.remainingMs = playlistObj["remainingMs"];
}

void PixelblazeClient::dispatchBinaryReply(ReplyHandler* handler) {
    BinaryReplyHandler* binHandler;
    if (handler.replyType == HANDLER_SYNC) {
        SyncHandler* syncHandler = (SyncHandler*)handler;
        syncHandler.finish();
        binHandler = (BinaryReplyHandler*)syncHandler.wrappedHandler;
    } else {
        binHandler = (BinaryReplyHandler*)handler
    }

    Stream stream = binaryBuffer.makeReadStream(binHandler.bufferId);
    if (!stream) {
        Serial.print("Couldn't open read string for bufferId: ");
        Serial.println(binHandler.bufferId);
        return;
    }

    switch (binHandler.replyType) {
        case HANDLER_RAW_BINARY:
            auto specHandler = (RawBinaryRequestHandler*)binHandler;
            specHandler.handle(stream);
            break;
        case HANDLER_ALL_PATTERNS:
            auto specHandler = (AllPetternRequestHandler*)binHandler;
            specHandler.handle(AllPatternIterator(stream));
            break;
        case HANDLER_PREVIEW_IMG:
            auto specHandler = (PreviewImageReplyHandler*)binHandler;
            usize_t buffIdx = 0;
            int peek = stream.peek();
            while (peek >= 0 && peek < 0xFF && buffIdx < clientConfig.textReadBufferBytes) {
                stream.read();
                textReadBuffer[buffIdx] = peek;
                buffIdx++;
                peek = stream.peek();
            }

            textReadBuffer[buffIdx] = '\0';
            if (buffIdx == clientConfig.textReadBufferBytes && peek != 0xFF) {
                while (peek >= 0 && peek != 0xFF) {
                    peek = stream.read();
                }
            } else if (peek == 0xFF) {
                stream.read();
            }

            String id = textReadBuffer;
            specHandler.handle(id, stream);
            break;
        case HANDLER_EXPANDER_CONF:
            //TODO
            break;
        default:
            Serial.print("Got unexpected binary reply type: ");
            Serial.println(binHandler.replyType);
    }

    stream.close();
}

String PixelblazeClient::humanizeVarName(String& camelCaseVar) {
    camelCaseVar = camelCaseVar.substring(0);
    if (camelCaseVar.length() == 0) {
        return "";
    } else if (camelCaseVar.equals("slider")) {
        return "Slider";
    }

    usize_t startIdx = 0;
    if (camelCaseVar.startsWith("slider")) {
        startIdx = 6;
    } else {
        camelCaseVar.setCharAt(0, toupper(camelCaseVar.charAt(0)))
    }

    int wordStarts[10];
    usize_t numStarts = 0;
    for (int idx = startIdx; idx < camelCaseVar.length(); idx++) {
        if (isupper(camelCaseVar.charAt(idx))) {
            wordStarts[numStarts] = idx;
            numStarts++;
            if (numStarts > 10) {
                break;
            }
        }
    }

    String result = "";
    for (int idx = 0; idx < numStarts - 1; idx++) {
        result += camelCaseVar.substring(wordStarts[idx], wordStarts[idx + 1]) + " ";
    }
    result += camelCaseVar.substring(wordStarts[numStarts - 1]);

    return result;
}

String PixelblazeClient::humanizeDataSize(int bytes) {
    if (bytes > 1024 * 1024 * 1024) {
        return String(String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + "GB");
    } else if (bytes > 1024 * 1024) {
        return String(String(bytes / (1024.0 * 1024.0), 2) + "MB");
    } else if (bytes > 1024) {
        return String(String(bytes / 1024.0, 2) + "kB");
    } else {
        return String(String(bytes) + "B");
    }
}

void PixelblazeClient::sendJson(JsonDocument& doc) {
    wsClient.beginMessage(FORMAT_TEXT);
    serializeJson(doc, wsClient);
    wsClient.endMessage();
}

void PixelblazeClient::handleUnrequestedJson() {
    if (json.containsKey("fps")) {
        statsEvent.fps = json["fps"];
        statsEvent.vmerr = json["vmerr"];
        statsEvent.vmerrpc = json["vmerrpc"];
        statsEvent.memBytes = json["mem"];
        statsEvent.expansions = json["exp"];
        statsEvent.renderType = json["renderType"];
        statsEvent.uptimeMs = json["uptime"];
        statsEvent.storageBytesUsed = json["storageUsed"];
        statsEvent.storageBytesSize = json["storageSize"];
        statsEvent.rr0 = json["rr0"];
        statsEvent.rr1 = json["rr1"];
        statsEvent.rebootCounter = json["rebootCounter"];

        unrequestedHandler.handleStats(statsEvent)
    } else if (json.containsKey("activeProgram")) {
        //This is also sent as part of the response to getConfig
        parseSequencerState();
        unrequestedHandler.handlePatternChange(sequencerState);
    }
}

bool handleUnrequestedBinary(int frameType) {
    if (frameType == BIN_TYPE_PREVIEW_FRAME) {
        //Should always be 300 bytes, but just in case...
        int frameSize = wsClient.read(previewFrameBuff, min(wsClient.available, clientConfig.framePreviewBufferBytes));
        unrequestedHandler.handlePreviewFrame(previewFrameBuff, frameSize);
        return true;
    } else if (frameType == BIN_TYPE_EXPANDER_CONFIG) {
        // Expander configs can come in out of order, check if one has been requested
        usize_t queuePos = queueFront;
        while (queuePos != queueBack) {
            if (replyQueue[queuePos].type == FORMAT_BINARY && ((BinaryReplyHandler*)replyQueue[queuePos]).binType == BIN_TYPE_EXPANDER_CONFIG) {
                dispatchBinaryReply(replyQueue[queuePos]);
                replyQueue[queuePos].satisfied = true;
                break;
            }
            queuePos = (queuePos + 1) % clientConfig.replyQueueSize;
        }
        return true;
    }

    return false;
}

int PixelblazeClient::queueLength() {
    if (queueFront > queueBack) {
        //Queue shape: |***B...F**|
        return queueBack + (clientConfig.replyQueueSize - queueFront);
    } else {
        //Queue shape: |.F*****B..| or empty with them ==
        return queueBack - queueFront;
    }
}

bool PixelblazeClient::enqueueReply(ReplyHandler* replyHandler) {
    return enqueueReplies(1, replyHandler);
}

bool PixelblazeClient::enqueueReplies(int num, ...) {
    if (num == 0) {
        Serial.println("Got empty enqueue request");
        return true;
    }

    //In order to drop handling parts of a response like from getSettings, sometimes we mark replies
    //satisfied before we even enqueue them. There are probably cleaner ways but this works.
    int toEnqueue = 0;
    va_list arguments;
    va_start(arguments, num);
    for (int idx = 0; idx < num; idx++) {
        ReplyHandler* handler = va_arg(arguments, ReplyHandler*);
        if (handler && !handler.isSatisfied()) {
            toEnqueue++;
        }
    }
    va_end(arguments);

    if (toEnqueue == 0) {
        return true;
    }

    //Verify that there's space
    if (clientConfig.replyQueueSize - queueLength() < toEnqueue) {
        //Last ditch compact and try again
        compactQueue();
        if (clientConfig.replyQueueSize - queueLength() < toEnqueue) {
            return false;
        }
    }

    va_start(arguments, num);
    for (int idx = 0; idx < num; idx++) {
        ReplyHandler* handler = va_arg(arguments, ReplyHandler*);
        if (handler && !handler.isSatisfied()) {
            replyQueue[newBack] = handler;
            queueBack = (queueBack + 1) % clientConfig.replyQueueSize;
        } else if (handler) {
            handler.cleanup();
            delete handler;
        }
    }
    va_end(arguments);

    return true;
}

void PixelblazeClient::dequeueReply() {
    if (queueLength() == 0) {
        Serial.println("Dequeue called on empty queue");
        return;
    }

    replyQueue[queueFront].cleanup();
    delete replyQueue[queueFront];
    replyQueue[queueFront] = NULL;
    queueFront = (queueFront + 1) % clientConfig.replyQueueSize;
}

//Last ditch when an enqueue fails
void PixelblazeClient::compactQueue() {
    int toKeep = 0;
    int nowMs = time();
    for (int idx = queueFront; idx != queueBack; idx = (idx + 1) % clientConfig.replyQueueSize) {
        if (!replyQueue[idx].isSatisfied() && replyQueue[idx].requestTsMs + clientConfig.maxResponseWaitMs > nowMs) {
            toKeep++;
        }
    }

    if (toKeep == 0) {
        for (int idx = 0; idx < clientConfig.replyQueueSize; idx++) {
            if (replyQueue[idx]) {
                replyQueue[idx].cleanup();
                delete replyQueue[idx];
                replyQueue[idx] = NULL;
            }
        }

        queueFront = 0;
        queueBack = 0;
    } else if (toKeep == clientConfig.replyQueueSize) {
        //Nothing to do
    } else {
        ReplyHandler* temp[toKeep];
        int tempIdx = 0;
        for (int idx = queueFront; idx != queueBack; idx = (idx + 1) % clientConfig.replyQueueSize) {
            if (!replyQueue[idx].isSatisfied() && replyQueue[idx].requestTsMs + clientConfig.maxResponseWaitMs > nowMs) {
                temp[tempIdx] = replyQueue[idx];
                tempIdx++;
            } else {
                delete replyQueue[idx];
            }
            replyQueue[idx] = NULL;
        }

        for (int idx = 0; idx < toKeep; idx++) {
            replyQueue[idx] = temp[idx];
        }

        queueFront = 0;
        queueBack = toKeep;
    }
}

bool AllPatternIterator::next(PatternIdentifiers& fillMe) {
    usize_t buffIdx = 0;
    int read = stream.read();
    if (read < 0) {
        return false;
    }

    while (read >= 0 && read != '\t' && buffIdx < clientConfig.textReadBufferBytes) {
        textReadBuffer[buffIdx] = read;
        buffIdx++;
        read = stream.read()
    }

    //Our length limit exceeded, unclear what id length limits are though 16 bytes seems standard
    if (buffIdx >= clientConfig.textReadBufferBytes) {
        while (read >= 0 && read != '\t') {
            read = stream.read();
        }
    }

    if (read < 0) {
        return false;
    }

    textReadBuffer[buffIdx] = '\0';
    fillMe.id = textReadBuffer;

    buffIdx = 0;
    while (read >= 0 && read != '\n' && buffIdx < clientConfig.textReadBufferBytes) {
        textReadBuffer[buffIdx] = read;
        buffIdx++;
        read = stream.read()
    }

    //Our length limit exceeded, unclear what name length limits are
    if (buffIdx >= clientConfig.textReadBufferBytes) {
        while (read >= 0 && read != '\n') {
            read = stream.read();
        }
    }

    textReadBuffer[buffIdx] = '\0';
    fillMe.name = textReadBuffer;

    return true;
}