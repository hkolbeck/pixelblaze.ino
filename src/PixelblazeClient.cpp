#include "PixelblazeCommon.h"
#include "PixelblazeClient.h"
#include "PixelblazeHandlers.h"

#include <ArduinoJson.h>
#include <WebSocketClient.h>
#include "BufferUtils/BufferReader.h"

PixelblazeClient::PixelblazeClient(
        WebSocketClient &wsClient,
        PixelblazeBuffer &streamBuffer,
        PixelblazeWatcher &watcher,
        ClientConfig clientConfig) :
        wsClient(wsClient), streamBuffer(streamBuffer),
        watcher(watcher), clientConfig(clientConfig),
        json(DynamicJsonDocument(clientConfig.jsonBufferBytes)) {

    byteBuffer = new uint8_t[clientConfig.binaryBufferBytes];
    textReadBuffer = new char[clientConfig.textReadBufferBytes];
    expanderChannels = new ExpanderChannel[clientConfig.expanderChannelLimit];
    peers = new Peer[clientConfig.peerLimit];
    controls = new Control[clientConfig.controlLimit];
    replyQueue = new ReplyHandler *[clientConfig.replyQueueSize];
    sequencerState.controls = new Control[clientConfig.controlLimit];
    playlist.items = new PlaylistItem[clientConfig.playlistLimit];
    playlistUpdate.items = new PlaylistItem[clientConfig.playlistLimit];
}

PixelblazeClient::~PixelblazeClient() {
    while (queueLength() > 0) {
        replyQueue[queueFront]->reportFailure(FailureCause::ClientDestructorCalled);
        delete replyQueue[queueFront];
        queueFront = (queueFront + 1) % clientConfig.replyQueueSize;
    }

    delete[] byteBuffer;
    delete[] textReadBuffer;
    delete[] expanderChannels;
    delete[] peers;
    delete[] controls;
    delete[] replyQueue;
    delete[] sequencerState.controls;
    delete[] playlist.items;
    delete[] playlistUpdate.items;
}

bool PixelblazeClient::begin() {
    return wsClient.begin("/") == 0;
}

bool PixelblazeClient::connected() {
    return wsClient.connected();
}

bool PixelblazeClient::getPatterns(void (*handler)(AllPatternIterator &), void (*onError)(FailureCause)) {
    String bufferId = String(random());
    auto *myHandler = new AllPatternsReplyHandler(handler, bufferId, true, onError);
    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    json.clear();
    json["listPrograms"] = true;
    return sendJson(json);
}

bool PixelblazeClient::getPlaylist(void (*handler)(Playlist &), String &playlistName, void (*onError)(FailureCause)) {
    auto *myHandler = new PlaylistReplyHandler(handler, onError);
    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    json.clear();
    json["getPlaylist"] = playlistName;
    return sendJson(json);
}

bool PixelblazeClient::getPlaylistIndex(void (*handler)(size_t), void (*onError)(FailureCause)) {
    //Until lambdas are everywhere, here we are
    class ExtractIndex : public RawTextHandler {
    public:
        explicit ExtractIndex(void (*handler)(size_t), void (*onError)(FailureCause)) :
                handler(handler), onError(onError), RawTextHandler() {};

        bool jsonMatches(JsonDocument &json) override {
            return json.containsKey("playlist") && json["playlist"].containsKey("position");
        }

        void reportFailure(FailureCause cause) override {
            onError(cause);
        }

        void handle(JsonDocument &json) override {
            handler(json["playlist"]["position"]);
        }

    private:
        void (*handler)(size_t);

        void (*onError)(FailureCause);
    };

    json.clear();
    json["getPlaylist"] = defaultPlaylist;

    auto replyHandler = ExtractIndex(handler, onError);
    return rawRequest(replyHandler, json);
}

bool PixelblazeClient::setPlaylistIndex(int idx) {
    json.clear();
    JsonObject playlistObj = json.createNestedObject("playlist");
    playlistObj["position"] = idx;
    return sendJson(json);
}

bool PixelblazeClient::nextPattern() {
    json.clear();
    json["nextProgram"] = true;
    return sendJson(json);
}

bool PixelblazeClient::prevPattern() {
    //Until lambdas are everywhere, here we are
    class ExtractIndexAndHitBack : public RawTextHandler {
    public:
        explicit ExtractIndexAndHitBack(PixelblazeClient *client) : client(client), RawTextHandler() {};

        bool jsonMatches(JsonDocument &json) override {
            return json.containsKey("playlist") && json["playlist"].containsKey("position");
        }

        void handle(JsonDocument &json) override {
            JsonObject playlistObj = json["playlist"];
            size_t position = playlistObj["position"];
            size_t playlistLen = playlistObj["items"].as<JsonArray>().size();
            if (playlistLen == 0) {
                return;
            }

            if (position == 0) {
                position = playlistLen;
            }
            position--;

            client->setPlaylistIndex(position);
        }

    private:
        PixelblazeClient *client;
    };

    json.clear();
    json["getPlaylist"] = defaultPlaylist;

    auto replyHandler = ExtractIndexAndHitBack(this);
    return rawRequest(replyHandler, json);
}

bool PixelblazeClient::playSequence() {
    json.clear();
    json["runSequencer"] = true;
    return sendJson(json);
}

bool PixelblazeClient::pauseSequence() {
    json.clear();
    json["runSequencer"] = false;
    return sendJson(json);
}

bool PixelblazeClient::setSequencerMode(SequencerMode sequencerMode) {
    json.clear();
    json["sequencerMode"] = (int) sequencerMode;
    return sendJson(json);
}

bool PixelblazeClient::getPeers(void (*handler)(Peer *, size_t), void (*onError)(FailureCause)) {
    auto *myHandler = new PeersReplyHandler(handler, onError);
    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    json.clear();
    json["getPeers"] = 1;
    return sendJson(json);
}

bool PixelblazeClient::setCurrentPatternControls(Control *controls, int numControls, bool saveToFlash) {
    json.clear();
    JsonObject controlsObj = json.createNestedObject("setControls");
    for (int idx = 0; idx < numControls; idx++) {
        controlsObj[controls[idx].name] = controls[idx].value;
    }

    json["save"] = saveToFlash;
    return sendJson(json);
}

bool PixelblazeClient::setCurrentPatternControl(String &controlName, float value, bool saveToFlash) {
    json.clear();
    JsonObject controls = json.createNestedObject("setControls");
    controls[controlName] = value;
    json["save"] = saveToFlash;
    return sendJson(json);
}

bool PixelblazeClient::setBrightness(float brightness, bool saveToFlash) {
    json.clear();
    json["brightness"] = constrain(brightness, 0, 1);
    json["save"] = saveToFlash;
    return sendJson(json);
}

bool PixelblazeClient::getPatternControls(String &patternId, void (*handler)(String &, Control *, size_t),
                                          void (*onError)(FailureCause)) {
    auto *myHandler = new PatternControlReplyHandler(handler, onError);

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    json.clear();
    json["getControls"] = patternId;
    return sendJson(json);
}

bool PixelblazeClient::getCurrentPatternControls(void (*handler)(Control *, size_t), void (*onError)(FailureCause)) {
    //Until lambdas work everywhere, here we are
    class CurrentPatternControlExtractor : public RawTextHandler {
    public:
        CurrentPatternControlExtractor(Control *controls, size_t *controlCount, size_t maxControls,
                                       void (*handler)(Control *, size_t),
                                       void (*onError)(FailureCause))
                : controls(controls), controlCount(controlCount), maxControls(maxControls), handler(handler),
                  onError(onError), RawTextHandler() {};

        bool jsonMatches(JsonDocument &json) override {
            return json.containsKey("activeProgram");
        }

        void reportFailure(FailureCause cause) override {
            onError(cause);
        }

        void handle(JsonDocument &json) override {
            JsonObject controlsObj = json["activeProgram"]["controls"];
            int controlIdx = 0;
            for (JsonPair kv: controlsObj) {
                controls[controlIdx].name = kv.key().c_str();
                controls[controlIdx].value = kv.value();
                controlIdx++;
                if (controlIdx >= maxControls) {
                    Serial.print(F("Got more controls than could be saved: "));
                    Serial.println(controlsObj.size());
                    break;
                }
            }
            *controlCount = controlIdx;
        }

    private:
        void (*handler)(Control *, size_t);

        void (*onError)(FailureCause);

        Control *controls;
        size_t *controlCount;
        size_t maxControls;
    };

    auto currentControlExtractor =
            CurrentPatternControlExtractor(controls, &controlCount,
                                           clientConfig.controlLimit, handler, onError);
    json.clear();
    json["getConfig"] = true;

    return rawRequest(currentControlExtractor, json);
}

bool PixelblazeClient::getPreviewImage(String &patternId, void (*handler)(String &, CloseableStream *), bool clean,
                                       void (*onError)(FailureCause)) {
    auto *myHandler = new PreviewImageReplyHandler(patternId, handler, clean, onError);
    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    json.clear();
    json["getPreviewImg"] = patternId;
    return sendJson(json);
}

bool PixelblazeClient::setBrightnessLimit(float value, bool saveToFlash) {
    json.clear();
    json["maxBrightness"] = round(constrain(value, 0, 1) * 100);
    json["save"] = saveToFlash;
    return sendJson(json);
}

bool PixelblazeClient::setPixelCount(uint32_t pixels, bool saveToFlash) {
    json.clear();
    json["pixelCount"] = pixels;
    json["save"] = saveToFlash;
    return sendJson(json);
}

bool PixelblazeClient::getSystemState(
        void (*settingsHandler)(Settings &),
        void (*seqHandler)(SequencerState &),
        void (*expanderHandler)(ExpanderChannel *, size_t),
        int watchResponses,
        void (*onError)(FailureCause)) {

    auto mySettingsHandler = new SettingsReplyHandler(settingsHandler, onError);
    if (!(watchResponses & (int) SettingReply::Settings)) {
        mySettingsHandler->satisfied = true;
    }

    auto *mySeqHandler = new SequencerReplyHandler(seqHandler, onError);
    if (!(watchResponses & (int) SettingReply::Sequencer)) {
        mySeqHandler->satisfied = true;
    }

    String bufferId = String(random());
    auto *myExpanderHandler = new ExpanderChannelsReplyHandler(expanderHandler, bufferId, true, onError);
    if (!(watchResponses & (int) SettingReply::Expander)) {
        myExpanderHandler->satisfied = true;
    }

    if (!enqueueReplies(3, mySettingsHandler, mySeqHandler, myExpanderHandler)) {
        delete mySettingsHandler;
        delete mySeqHandler;
        delete myExpanderHandler;
        return false;
    }

    json.clear();
    json["getConfig"] = true;
    return sendJson(json);
}

bool PixelblazeClient::getSettings(void (*settingsHandler)(Settings &), void (*onError)(FailureCause)) {
    return getSystemState(settingsHandler, noopSequencer, noopExpander, (int) SettingReply::Settings, onError);
}

bool PixelblazeClient::getSequencerState(void (*seqHandler)(SequencerState &), void (*onError)(FailureCause)) {
    return getSystemState(noopSettings, seqHandler, noopExpander, (int) SettingReply::Sequencer, onError);
}

bool PixelblazeClient::getExpanderConfig(void (*expanderHandler)(ExpanderChannel *, size_t), void (*onError)(FailureCause)) {
    return getSystemState(noopSettings, noopSequencer, expanderHandler, (int) SettingReply::Expander, onError);
}

bool PixelblazeClient::ping(void (*handler)(uint32_t), void (*onError)(FailureCause)) {
    auto *myHandler = new PingReplyHandler(handler, onError);
    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    json.clear();
    json["ping"] = true;
    return sendJson(json);
}

bool PixelblazeClient::sendFramePreviews(bool sendEm) {
    json.clear();
    json["sendUpdates"] = sendEm;
    return sendJson(json);
}

bool PixelblazeClient::rawRequest(RawBinaryHandler &replyHandler, JsonDocument &request) {
    auto *myHandler = new RawBinaryHandler(replyHandler);
    myHandler->requestTsMs = millis();
    myHandler->satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    return sendJson(request);
}

bool PixelblazeClient::rawRequest(RawTextHandler &replyHandler, JsonDocument &request) {
    auto *myHandler = new RawTextHandler(replyHandler);
    myHandler->requestTsMs = millis();
    myHandler->satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    return sendJson(request);
}

bool PixelblazeClient::rawRequest(RawBinaryHandler &replyHandler, int rawBinType, Stream &request) {
    auto *myHandler = new RawBinaryHandler(replyHandler);
    myHandler->requestTsMs = millis();
    myHandler->satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    return sendBinary(rawBinType, request);
}

bool PixelblazeClient::rawRequest(RawTextHandler &replyHandler, int rawBinType, Stream &request) {
    auto *myHandler = new RawTextHandler(replyHandler);
    myHandler->requestTsMs = millis();
    myHandler->satisfied = false;

    if (!enqueueReply(myHandler)) {
        delete myHandler;
        return false;
    }

    return sendBinary(rawBinType, request);
}

void PixelblazeClient::checkForInbound() {
    if (!connected()) {
        Serial.print(F("Connection to Pixelblaze lost, dropping pending handlers: "));
        Serial.println(queueLength());
        evictQueue(FailureCause::ConnectionLost);
    }

    if (!connectionMaintenance()) {
        Serial.println(F("Couldn't reconnect to Pixelblaze websocket, bailing from checkForInbound()"));
        return;
    }

//    if (millis() - lastPingAtMs > clientConfig.sendPingEveryMs) {
//        if (ping(pong)) {
//            lastPingAtMs = millis();
//        }
//    }

    weedExpiredReplies();
    uint32_t startTime = millis();

    int read = wsClient.parseMessage();
    while (read > 0 && startTime + clientConfig.maxInboundCheckMs > millis()) {
        WebsocketFormat format = websocketFormatFromInt(wsClient.messageType());
        if (format == WebsocketFormat::Unknown) {
            Serial.print("Got unexpected websocket message format: ");
            Serial.println(wsClient.messageType());

            read = wsClient.parseMessage();
            continue;
        }

        while (queueLength() > 0 && replyQueue[queueFront]->isSatisfied()) {
            dequeueReply();
        }

        if (queueLength() == 0) {
            //Nothing expected, dispatch everything through unrequested functions
            if (format == WebsocketFormat::Text) {
                DeserializationError deErr = deserializeJson(json, wsClient.readString());
                if (deErr) {
                    Serial.print(F("Message deserialization error: "));
                    Serial.println(deErr.f_str());
                } else {
                    handleUnrequestedJson();
                }
            } else if (format == WebsocketFormat::Binary && wsClient.available() > 0) {
                handleUnrequestedBinary(wsClient.read());
            } else {
                Serial.print(F("Unexpected reply format: "));
                Serial.println((int) format);
            }
        } else {
            WebsocketFormat soughtFormat = replyQueue[queueFront]->format;

            int repliesExamined = 0;
            while (repliesExamined <= queueLength()
                   && soughtFormat == WebsocketFormat::Binary
                   && ((BinaryReplyHandler *) replyQueue[queueFront])->rawBinType == (int) BinaryMsgType::ExpanderChannels
                   && (format != WebsocketFormat::Binary || wsClient.peek() != (int) BinaryMsgType::ExpanderChannels)) {
                //Expander configs can be non-optionally fetched by getConfig, and may never come if no expander is installed.
                //If the head of the queue is seeking them and the current message isn't one, bump it to the back of the queue.
                //This does require special handling in handleUnrequestedBinary(). If it's the only thing in the queue we'll be
                //thrashing it a bit, but that should be fine.

                ReplyHandler *expanderHandler = replyQueue[queueFront];
                replyQueue[queueFront] = nullptr;
                queueFront = (queueFront + 1) % clientConfig.replyQueueSize;
                enqueueReply(expanderHandler);

                soughtFormat = replyQueue[queueFront]->format;
                repliesExamined++;
            }

            if (soughtFormat == WebsocketFormat::Unknown) {
                Serial.println(F("Handler had unknown sought websocket format"));
                replyQueue[queueFront]->reportFailure(FailureCause::MalformedHandler);
                dequeueReply();
            } else if (format == WebsocketFormat::Text) {
                if (soughtFormat == WebsocketFormat::Text) {
                    seekingTextHasText();
                } else {
                    seekingBinaryHasText();
                }
            } else if (format == WebsocketFormat::Binary) {
                if (soughtFormat == WebsocketFormat::Text) {
                    seekingTextHasBinary();
                } else {
                    seekingBinaryHasBinary();
                }
            } else {
                Serial.println(F("Dropping message with 'other' reply format"));
            }
        }

        read = wsClient.parseMessage();
    }
}

/////////////////////////////
// Begin Private Functions //
/////////////////////////////

bool PixelblazeClient::connectionMaintenance() {
    if (connected()) {
        return true;
    }

    uint32_t startTime = millis();
    while (millis() - startTime < clientConfig.maxConnRepairMs) {
        if (begin()) {
            return true;
        }
        delay(clientConfig.connRepairRetryDelayMs);
    }

    return false;
}

void PixelblazeClient::weedExpiredReplies() {
    uint32_t currentTimeMs = millis();
    while (queueLength() > 0) {
        if (replyQueue[queueFront]->isSatisfied()) {
            queueFront = (queueFront + 1) % clientConfig.replyQueueSize;
        } else if (replyQueue[queueFront]->requestTsMs + clientConfig.maxResponseWaitMs < currentTimeMs) {
            replyQueue[queueFront]->reportFailure(FailureCause::TimedOut);
            queueFront = (queueFront + 1) % clientConfig.replyQueueSize;
        } else {
            return;
        }
    }
}

void PixelblazeClient::seekingTextHasText() {
    DeserializationError deErr = deserializeJson(json, wsClient.readString());
    if (deErr) {
        Serial.print(F("Message deserialization error: "));
        Serial.println(deErr.f_str());
    } else {
        if (replyQueue[queueFront]->jsonMatches(json)) {
            dispatchTextReply(replyQueue[queueFront]);
            dequeueReply();
        } else {
            handleUnrequestedJson();
        }
    }
}

void PixelblazeClient::seekingTextHasBinary() {
    handleUnrequestedBinary(wsClient.read());
}

void PixelblazeClient::seekingBinaryHasBinary() {
    auto *binaryHandler = (BinaryReplyHandler *) replyQueue[queueFront];
    int frameType = wsClient.read();
    if (frameType < 0) {
        Serial.println(F("Empty binary body received"));
    } else if (rawBinaryReadType < 0) {
        //We've read nothing so far, blank slate
        if (frameType == binaryHandler->rawBinType) {
            int frameFlag = wsClient.read();
            if (frameFlag & (int) FramePosition::First & (int) FramePosition::Last) {
                //Lone message
                if (readBinaryToStream(binaryHandler, binaryHandler->bufferId, false)) {
                    dispatchBinaryReply(replyQueue[queueFront]);
                }
                if (replyQueue[queueFront]->shouldDeleteBuffer()) {
                    streamBuffer.deleteStreamResults(binaryHandler->bufferId);
                }
                dequeueReply();
            } else if (frameFlag & (int) FramePosition::First) {
                if (!readBinaryToStream(binaryHandler, binaryHandler->bufferId, false)) {
                    streamBuffer.deleteStreamResults(binaryHandler->bufferId);
                    dequeueReply();
                    return;
                }
                rawBinaryReadType = frameType;
            } else {
                //Frame was middle, last, or 0, none of which should happen. Drop it and keep going
                Serial.print(F("Got unexpected frameFlag: "));
                Serial.print(frameFlag);
                Serial.print(F("For frameType: "));
                Serial.println(frameType);
            }
        } else {
            handleUnrequestedBinary(frameType);
        }
    } else if (frameType == rawBinaryReadType) {
        //We're mid read and the latest is compatible
        int frameFlag = wsClient.read();
        if (frameFlag & (int) FramePosition::Last) {
            if (readBinaryToStream(binaryHandler, binaryHandler->bufferId, true)) {
                dispatchBinaryReply(replyQueue[queueFront]);
            }

            if (replyQueue[queueFront]->shouldDeleteBuffer()) {
                streamBuffer.deleteStreamResults(binaryHandler->bufferId);
            }
            dequeueReply();
        } else if (frameFlag & (int) FramePosition::Middle) {
            if (!readBinaryToStream(binaryHandler, binaryHandler->bufferId, true)) {
                streamBuffer.deleteStreamResults(binaryHandler->bufferId);
                dequeueReply();
                return;
            }
        } else {
            //Frame was first or 0, neither of which should happen
            Serial.print(F("Got unexpected frameFlag: "));
            Serial.print(frameFlag);
            Serial.print(F("For frameType: "));
            Serial.println(frameType);
        }
    } else {
        //We're mid read and just got an incompatible frame
        if (!handleUnrequestedBinary(frameType)) {
            Serial.print(F("Expected frameType: "));
            Serial.print(rawBinaryReadType);
            Serial.print(F(" but got: "));
            Serial.println(frameType);

            //Scrap the current read, if the finisher never comes it would drop requested events until weeded
            binaryHandler->reportFailure(FailureCause::MultipartReadInterrupted);
            streamBuffer.deleteStreamResults(binaryHandler->bufferId);
            dequeueReply();
            rawBinaryReadType = -1;
        }
    }
}

void PixelblazeClient::seekingBinaryHasText() {
    DeserializationError deErr = deserializeJson(json, wsClient.readString());
    if (deErr) {
        Serial.print(F("Message deserialization error: "));
        Serial.println(deErr.f_str());
    } else {
        handleUnrequestedJson();
    }
}

bool PixelblazeClient::readBinaryToStream(ReplyHandler *handler, String &bufferId, bool append) {
    CloseableStream *stream = streamBuffer.makeWriteStream(bufferId, append);
    if (!stream) {
        Serial.println(F("Couldn't open write stream, attempting to garbage collect"));
        streamBuffer.garbageCollect();
        stream = streamBuffer.makeWriteStream(bufferId, append);
    }

    if (!stream) {
        Serial.print(F("Failed to get write stream for: "));
        Serial.println(bufferId);
        handler->reportFailure(FailureCause::BufferAllocFail);
        return false;
    }

    int available = wsClient.available();
    while (available > 0) {
        int bytesRead = wsClient.read(byteBuffer, min(clientConfig.binaryBufferBytes, available));
        size_t written = stream->write(byteBuffer, bytesRead);
        if (bytesRead != written) {
            Serial.print(F("Partial write on stream for bufferId: "));
            Serial.println(bufferId);
            handler->reportFailure(FailureCause::StreamWriteFailure);
            return false;
        }

        available -= bytesRead;
    }

    stream->close();
    return true;
}

void PixelblazeClient::dispatchTextReply(ReplyHandler *genHandler) {
    ReplyHandler *handler = genHandler;
    if (genHandler->type == ReplyHandlerType::Sync) {
        auto *syncHandler = (SyncHandler *) genHandler;
        syncHandler->finish();
        handler = syncHandler->getWrapped();
    }

    switch (handler->type) {
        case ReplyHandlerType::RawText: {
            auto *rawTextHandler = (RawTextHandler *) handler;
            rawTextHandler->handle(json);
            break;
        }
        case ReplyHandlerType::Playlist: {
            auto *playlistHandler = (PlaylistReplyHandler *) handler;
            JsonObject playlistObj = json["playlist"];
            playlist.id = playlistObj["id"].as<String>();
            playlist.position = playlistObj["position"];
            playlist.currentDurationMs = playlistObj["ms"];
            playlist.remainingCurrentMs = playlistObj["remainingMs"];

            JsonArray items = playlistObj["items"];
            int itemIdx = 0;
            for (JsonVariant v: items) {
                JsonObject itemObj = v.as<JsonObject>();
                playlist.items[itemIdx].id = itemObj["id"].as<String>();
                playlist.items[itemIdx].durationMs = itemObj["ms"];
                itemIdx++;
                if (itemIdx >= clientConfig.playlistLimit) {
                    Serial.print(F("Got too many patterns on playlist to store: "));
                    Serial.print(items.size());
                    break;
                }
            }
            playlist.numItems = itemIdx;

            playlistHandler->handle(playlist);
            break;
        }
        case ReplyHandlerType::Peers: {
            auto *peerHandler = (PeersReplyHandler *) handler;

            JsonArray peerArr = json["peers"];
            size_t peersFound = 0;
            for (JsonVariant v: peerArr) {
                if (peersFound >= clientConfig.peerLimit) {
                    break;
                }

                JsonObject peer = v.as<JsonObject>();
                peers[peersFound].id = peer["id"];
                peers[peersFound].ipAddress = peer["address"].as<String>();
                peers[peersFound].name = peer["name"].as<String>();
                peers[peersFound].version = peer["ver"].as<String>();
                peers[peersFound].isFollowing = !! peer["isFollowing"].as<int>();
                peers[peersFound].nodeId = peer["nodeId"];
                peers[peersFound].followerCount = peer["followerCount"];

                peersFound++;
            }

            peerCount = peersFound;
            peerHandler->handle(peers, peerCount);
            break;
        }
        case ReplyHandlerType::Settings: {
            auto *settingsHandler = (SettingsReplyHandler *) handler;
            settings.name = json["name"].as<String>();
            settings.brandName = json["brandName"].as<String>();
            settings.pixelCount = json["pixelCount"];
            settings.brightness = json["brightness"];
            settings.maxBrightness = json["maxBrightness"];
            settings.colorOrder = json["colorOrder"].as<String>();
            settings.dataSpeedHz = json["dataSpeedHz"];
            settings.ledType = ledTypeFromInt(json["ledType"].as<int>());
            settings.sequenceTimerMs = json["sequenceTimer"];
            settings.transitionDurationMs = json["transitionDuration"];
            settings.sequencerMode = json["sequencerMode"];
            settings.runSequencer = json["runSequencer"];
            settings.simpleUiMode = json["simpleUiMode"];
            settings.learningUiMode = json["learningUiMode"];
            settings.discoveryEnabled = json["discoveryEnable"];
            settings.timezone = json["timezone"].as<String>();
            settings.autoOffEnable = json["autoOffEnable"];
            settings.autoOffStart = json["autoOffStart"].as<String>();
            settings.autoOffEnd = json["autoOffEnd"].as<String>();
            settings.cpuSpeedMhz = json["cpuSpeed"];
            settings.networkPowerSave = json["networkPowerSave"];
            settings.mapperFit = json["mapperFit"];
            settings.leaderId = json["leaderId"];
            settings.nodeId = json["nodeId"];
            settings.soundSrc = inputSourceFromInt(json["soundSrc"]);
            settings.accelSrc = inputSourceFromInt(json["accelSrc"]);
            settings.lightSrc = inputSourceFromInt(json["lightSrc"]);
            settings.analogSrc = inputSourceFromInt(json["analogSrc"]);
            settings.exp = json["exp"];
            settings.version = json["ver"].as<String>();
            settings.chipId = json["chipId"];

            settingsHandler->handle(settings);
            break;
        }
        case ReplyHandlerType::Sequencer: {
            auto *seqHandler = (SequencerReplyHandler *) handler;
            parseSequencerState();
            seqHandler->handle(sequencerState);
            break;
        }
        case ReplyHandlerType::Ping: {
            auto *pingHandler = (PingReplyHandler *) handler;
            pingHandler->handle(millis() - pingHandler->requestTsMs);
            break;
        }
        case ReplyHandlerType::PatternControls: {
            //TODO
            break;
        }
        default: {
            Serial.print(F("Got unexpected text reply type: "));
            Serial.println((int) handler->type);
        }
    }
}

void PixelblazeClient::parseSequencerState() {
    JsonObject activeProgram = json["activeProgram"];
    sequencerState.name = activeProgram["name"].as<String>();
    sequencerState.activeProgramId = activeProgram["activeProgramId"].as<String>();

    JsonObject controlsObj = activeProgram["controls"];
    int controlIdx = 0;
    for (JsonPair kv: controlsObj) {
        sequencerState.controls[controlIdx].name = kv.key().c_str();
        sequencerState.controls[controlIdx].value = kv.value();
        controlIdx++;
        if (controlIdx >= clientConfig.controlLimit) {
            Serial.print(F("Got more controls than could be saved: "));
            Serial.println(controlsObj.size());
            break;
        }
    }
    sequencerState.controlCount = controlIdx;

    sequencerState.sequencerMode = sequencerModeFromInt(json["sequencerMode"]);
    sequencerState.runSequencer = json["runSequencer"];

    JsonObject playlistObj = json["playlist"];
    sequencerState.playlistPos = playlistObj["position"];
    sequencerState.playlistId = playlistObj["id"].as<String>();
    sequencerState.ttlMs = playlistObj["ms"];
    sequencerState.remainingMs = playlistObj["remainingMs"];
}

void PixelblazeClient::dispatchBinaryReply(ReplyHandler *handler) {
    BinaryReplyHandler *binHandler;
    if (handler->type == ReplyHandlerType::Sync) {
        auto *syncHandler = (SyncHandler *) handler;
        syncHandler->finish();
        binHandler = (BinaryReplyHandler *) syncHandler->wrappedHandler;
    } else {
        binHandler = (BinaryReplyHandler *) handler;
    }

    auto stream = streamBuffer.makeReadStream(binHandler->bufferId);
    if (!stream) {
        Serial.print(F("Couldn't open read string for bufferId: "));
        Serial.println(binHandler->bufferId);
        return;
    }

    switch (binHandler->type) {
        case ReplyHandlerType::RawBinary: {
            auto rawHandler = (RawBinaryHandler *) binHandler;
            rawHandler->handle(stream);
            break;
        }
        case ReplyHandlerType::AllPatterns: {
            auto allPatternsHandler = (AllPatternsReplyHandler *) binHandler;
            auto iterator = AllPatternIterator(stream, textReadBuffer, clientConfig.textReadBufferBytes);
            allPatternsHandler->handle(iterator);
            break;
        }
        case ReplyHandlerType::PreviewImage: {
            auto previewImageHandler = (PreviewImageReplyHandler *) binHandler;
            size_t buffIdx = 0;
            int peek = stream->peek();
            while (peek >= 0 && peek < 0xFF && buffIdx < clientConfig.textReadBufferBytes) {
                stream->read();
                textReadBuffer[buffIdx] = (char) peek;
                buffIdx++;
                peek = stream->peek();
            }

            textReadBuffer[buffIdx] = '\0';
            if (buffIdx == clientConfig.textReadBufferBytes && peek != 0xFF) {
                while (peek >= 0 && peek != 0xFF) {
                    peek = stream->read();
                }
            } else if (peek == 0xFF) {
                stream->read();
            }

            String id = textReadBuffer;
            previewImageHandler->handle(id, stream);
            break;
        }
        case ReplyHandlerType::Expander: {
            auto *expanderChannelHandler = (ExpanderChannelsReplyHandler *) handler;

            size_t read = stream->readBytes(byteBuffer, EXPANDER_CHANNEL_BYTE_WIDTH);
            size_t channelsFound = 0;
            while (read == EXPANDER_CHANNEL_BYTE_WIDTH && channelsFound < clientConfig.expanderChannelLimit) {
                //TODO: Do we really need to heap this? Is this because BufferReader hides the copy constructor?
                auto *reader = new BufferReader(byteBuffer, read, 0);
                auto channel = &expanderChannels[channelsFound];
                reader->read(channel->channelId);
                reader->read(channel->ledType);
                reader->read(channel->numElements);

                uint8_t colorOrderCode = 0;
                reader->read(colorOrderCode);
                channel->colorOrder = getColorOrder(colorOrderCode);

                reader->read(channel->pixels);
                reader->read(channel->startIndex);
                reader->read(channel->frequency);

                delete reader;
                channelsFound++;
                read = stream->readBytes(byteBuffer, EXPANDER_CHANNEL_BYTE_WIDTH);
            }

            expanderChannelHandler->handle(expanderChannels, numExpanderChannels);
            break;
        }
        default: {
            Serial.print(F("Got unexpected binary reply type: "));
            Serial.println((int) binHandler->type);
        }
    }

    stream->close();
    delete stream;
}

String PixelblazeClient::humanizeVarName(String &camelCaseVar, int maxWords) {
    camelCaseVar = camelCaseVar.substring(0);
    if (camelCaseVar.length() == 0) {
        return "";
    } else if (camelCaseVar.equals("slider")) {
        return "Slider";
    }

    size_t startIdx = 0;
    if (camelCaseVar.startsWith("slider")) {
        startIdx = 6;
    } else {
        camelCaseVar.setCharAt(0, (char) toupper(camelCaseVar.charAt(0)));
    }

    size_t wordStarts[maxWords];
    size_t numStarts = 0;
    for (size_t idx = startIdx; idx < camelCaseVar.length(); idx++) {
        if (isupper(camelCaseVar.charAt(idx))) {
            wordStarts[numStarts] = idx;
            numStarts++;
            if (numStarts >= maxWords) {
                break;
            }
        }
    }

    String result = "";
    for (size_t idx = 0; idx < numStarts - 1; idx++) {
        result += camelCaseVar.substring(wordStarts[idx], wordStarts[idx + 1]) + " ";
    }
    result += camelCaseVar.substring(wordStarts[numStarts - 1]);

    return result;
}

bool PixelblazeClient::sendJson(JsonDocument &doc) {
    wsClient.beginMessage((int) WebsocketFormat::Text);
    serializeJson(doc, wsClient);
    return !wsClient.endMessage();
}

void PixelblazeClient::handleUnrequestedJson() {
    if (json.containsKey("fps")) {
        statsEvent.fps = json["fps"];
        statsEvent.vmerr = json["vmerr"];
        statsEvent.vmerrpc = json["vmerrpc"];
        statsEvent.memBytes = json["mem"];
        statsEvent.expansions = json["exp"];
        statsEvent.renderType = renderTypeFromInt(json["renderType"]);
        statsEvent.uptimeMs = json["uptime"];
        statsEvent.storageBytesUsed = json["storageUsed"];
        statsEvent.storageBytesSize = json["storageSize"];
        statsEvent.rr0 = json["rr0"];
        statsEvent.rr1 = json["rr1"];
        statsEvent.rebootCounter = json["rebootCounter"];

        watcher.handleStats(statsEvent);
    } else if (json.containsKey("activeProgram")) {
        //This is also sent as part of the response to getConfig
        parseSequencerState();
        watcher.handlePatternChange(sequencerState);
    } else if (json.containsKey("playlist")) {
        //TODO
        //watcher.handlePlaylistChange()
    }
}

bool PixelblazeClient::handleUnrequestedBinary(int frameType) {
    if (frameType == (int) BinaryMsgType::PreviewFrame) {
        int frameSize = wsClient.read(byteBuffer,
                                      min(wsClient.available(), clientConfig.binaryBufferBytes));
        watcher.handlePreviewFrame(byteBuffer, frameSize);
        return true;
    } else if (frameType == (int) BinaryMsgType::ExpanderChannels) {
        // Expander configs can come in out of order, check if one has been requested
        size_t queuePos = queueFront;
        while (queuePos != queueBack) {
            if (replyQueue[queuePos]->format == WebsocketFormat::Binary &&
                ((BinaryReplyHandler *) replyQueue[queuePos])->type == ReplyHandlerType::Expander) {
                dispatchBinaryReply(replyQueue[queuePos]);
                replyQueue[queuePos]->satisfied = true;
                break;
            }
            queuePos = (queuePos + 1) % clientConfig.replyQueueSize;
        }
        return true;
    }

    return false;
}

size_t PixelblazeClient::queueLength() const {
    if (queueFront > queueBack) {
        //Queue shape: |***B...F**|
        return queueBack + (clientConfig.replyQueueSize - queueFront);
    } else {
        //Queue shape: |.F*****B..| or empty with them ==
        return queueBack - queueFront;
    }
}

bool PixelblazeClient::enqueueReply(ReplyHandler *replyHandler) {
    return enqueueReplies(1, replyHandler);
}

bool PixelblazeClient::enqueueReplies(int num, ...) {
    if (num == 0) {
        Serial.println(F("Got empty enqueue request"));
        return true;
    }

    //In order to drop handling parts of a response like from getSystemState, sometimes we mark replies
    //satisfied before we even enqueue them. There are probably cleaner ways but this works.
    int toEnqueue = 0;
    va_list arguments;
    va_start(arguments, num);
    for (int idx = 0; idx < num; idx++) {
        ReplyHandler *handler = va_arg(arguments, ReplyHandler*);
        if (handler && !handler->isSatisfied()) {
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
        ReplyHandler *handler = va_arg(arguments, ReplyHandler*);
        if (handler && !handler->isSatisfied()) {
            replyQueue[queueBack] = handler;
            queueBack = (queueBack + 1) % clientConfig.replyQueueSize;
        } else if (handler) {
            handler->cleanup();
            delete handler;
        }
    }
    va_end(arguments);

    return true;
}

void PixelblazeClient::dequeueReply() {
    if (queueLength() == 0) {
        Serial.println(F("Dequeue called on empty queue"));
        return;
    }

    replyQueue[queueFront]->cleanup();
    delete replyQueue[queueFront];
    replyQueue[queueFront] = nullptr;
    queueFront = (queueFront + 1) % clientConfig.replyQueueSize;
}

//Last ditch when an enqueue fails
void PixelblazeClient::compactQueue() {
    int toKeep = 0;
    unsigned long nowMs = millis();
    for (size_t idx = queueFront; idx != queueBack; idx = (idx + 1) % clientConfig.replyQueueSize) {
        if (!replyQueue[idx]->isSatisfied() && replyQueue[idx]->requestTsMs + clientConfig.maxResponseWaitMs > nowMs) {
            toKeep++;
        }
    }

    if (toKeep == 0) {
        for (size_t idx = 0; idx < clientConfig.replyQueueSize; idx++) {
            if (replyQueue[idx]) {
                replyQueue[idx]->cleanup();
                delete replyQueue[idx];
                replyQueue[idx] = nullptr;
            }
        }

        queueFront = 0;
        queueBack = 0;
    } else if (toKeep == clientConfig.replyQueueSize) {
        //Nothing to do
    } else {
        ReplyHandler *temp[toKeep];
        int tempIdx = 0;
        for (size_t idx = queueFront; idx != queueBack; idx = (idx + 1) % clientConfig.replyQueueSize) {
            if (!replyQueue[idx]->isSatisfied() &&
                replyQueue[idx]->requestTsMs + clientConfig.maxResponseWaitMs > nowMs) {
                temp[tempIdx] = replyQueue[idx];
                tempIdx++;
            } else {
                delete replyQueue[idx];
            }
            replyQueue[idx] = nullptr;
        }

        for (int idx = 0; idx < toKeep; idx++) {
            replyQueue[idx] = temp[idx];
        }

        queueFront = 0;
        queueBack = toKeep;
    }
}

void PixelblazeClient::evictQueue(FailureCause reason) {
    for (size_t idx = queueFront; idx != queueBack; idx = (idx + 1) % clientConfig.replyQueueSize) {
        replyQueue[idx]->reportFailure(reason);
        delete replyQueue[idx];
        replyQueue[idx] = nullptr;
    }

    queueFront = 0;
    queueBack = 0;
}

bool PixelblazeClient::sendBinary(int binType, Stream &stream) {
    size_t freeBufferLen = clientConfig.binaryBufferBytes - 2;
    bool hasSent = false;
    while (true) {
        size_t read = stream.readBytes(byteBuffer, freeBufferLen);
        if (read >= freeBufferLen) {
            FramePosition frameType = hasSent ? FramePosition::Middle : FramePosition::First;
            wsClient.beginMessage((int) WebsocketFormat::Binary);
            wsClient.write(binType);
            wsClient.write((int) frameType);
            wsClient.write(byteBuffer, read);
            if (wsClient.endMessage()) {
                return false;
            }

            hasSent = true;
        } else if (read > 0) {
            int frameType = hasSent ? (int) FramePosition::Last : (int) FramePosition::First | (int) FramePosition::Last;
            wsClient.beginMessage((int) WebsocketFormat::Binary);
            wsClient.write(binType);
            wsClient.write(frameType);
            wsClient.write(byteBuffer, read);
            if (wsClient.endMessage()) {
                return false;
            }

            hasSent = true;
        } else {
            if (hasSent) {
                //Our data broke perfectly along frame boundaries, hopefully sending an empty
                //last frame doesn't break things
                wsClient.beginMessage((int) WebsocketFormat::Binary);
                wsClient.write(binType);
                wsClient.write((int) FramePosition::Last);
                if (wsClient.endMessage()) {
                    return false;
                }
            }

            return true;
        }
    }
}

// Extracted from the web JS
static String BGR_STR = "BGR";
static String BRG_STR = "BRG";
static String GBR_STR = "GBR";
static String RBG_STR = "RBG";
static String GRB_STR = "GRB";
static String RGB_STR = "RGB";
static String WGRB_STR = "WGRB";
static String WRGB_STR = "WRGB";
static String GRBW_STR = "GRBW";
static String RGBW_STR = "RGBW";
String *PixelblazeClient::getColorOrder(uint8_t code) {
    switch (code) {
        case 6:
            return &BGR_STR;
        case 9:
            return &BRG_STR;
        case 18:
            return &GBR_STR;
        case 24:
            return &RBG_STR;
        case 33:
            return &GRB_STR;
        case 36:
            return &RGB_STR;
        case 54:
            return &WGRB_STR;
        case 57:
            return &WRGB_STR;
        case 225:
            return &GRBW_STR;
        case 228:
            return &RGBW_STR;
        default:
            // Default in web code
            return &BGR_STR;
    }
}

bool AllPatternIterator::next(PatternIdentifiers &fillMe) {
    size_t buffIdx = 0;
    int read = stream->read();
    if (read < 0) {
        return false;
    }

    while (read >= 0 && read != '\t' && buffIdx < bufferLen) {
        readBuffer[buffIdx] = (char) read;
        buffIdx++;
        read = stream->read();
    }

    //Our length limit exceeded, unclear what id length limits are though 16 bytes seems standard
    if (buffIdx >= bufferLen) {
        while (read >= 0 && read != '\t') {
            read = stream->read();
        }
    }

    if (read < 0) {
        Serial.println(F("Got malformed all pattern response."));
        return false;
    }

    readBuffer[buffIdx] = '\0';
    fillMe.id = readBuffer;

    buffIdx = 0;
    while (read >= 0 && read != '\n' && buffIdx < bufferLen) {
        readBuffer[buffIdx] = (char) read;
        buffIdx++;
        read = stream->read();
    }

    //Our length limit exceeded, unclear what name length limits are
    if (buffIdx >= bufferLen) {
        while (read >= 0 && read != '\n') {
            read = stream->read();
        }
    }

    readBuffer[buffIdx] = '\0';
    fillMe.name = readBuffer;

    return true;
}