# Pixelblaze.ino
## An Arduino-focused C++ library for the Pixelblaze LED controller's websocket API
### Status: Not even alpha.

Information about the Pixelblaze can be found at https://electromage.com. Needless to say I think they're pretty neat.

This builds extensively off of @zranger1's Python library: https://github.com/zranger1/pixelblaze-client, but its 
operation is fundamentally different. Where the python client is largely synchronous, I wanted to be able to fire and
forget with eventual updates or register handlers for events that come in with no request issued.

In general, the client works in three ways depending on the operation:
 - "set" operations: fire and forget with no completion-ack possible, only assurance is that the message was received by 
   the Pixelblaze HTTP server
 - "get" operations: send a request, passing along a handler to be invoked when or if a response is received. That
   handler will be passed the most parsed and annotated version of the data possible.
 - "watch" operations: The pixelblaze can send hundreds of events per second to connected clients with no prompting.
   If you're interested in for instance, notifications of pattern changes, you need a watcher.


## Right now the Pixelblaze can play a playlist or shuffle everything, let's (awkwardly) shuffle the playlist!

```C++
#include <Arduino.h>
#include <WiFi.h>
#include "../src/PixelblazeClient.h"
#include "../src/PixelblazeMemBuffer.h"

// #define WIFI_SSID "my-wifi"
// #define WIFI_PW "hunter2"
#include "wifi_secrets.h"

#define WEBSOCKET_HOST "192.168.4.1"
#define WEBSOCKET_PORT 81
#define PLAYLIST_DURATION_MS 30000

static size_t playlistLen = 0;

// This is a simple usage of the watcher class to stay up to date on the length of the playlist
class PlaylistChangeWatcher : public PixelblazeWatcher {
public:
    void handlePlaylistChange(PlaylistUpdate &playlistUpdate) override {
        playlistLen = playlistUpdate.numItems;
    }
};

//This is what we pass to be called when we issue a get for the playlist
void extractPlaylistLen(Playlist& playlist) {
    playlistLen = playlist.numItems;
}

PixelblazeClient *pbClient = nullptr;
void setup() {
    Serial.begin(115200);
    while (!Serial);

    WiFiClient wifi;
    WebSocketClient wsClient = WebSocketClient(wifi, WEBSOCKET_HOST, WEBSOCKET_PORT);
    WiFi.begin(WIFI_SSID, WIFI_PW);
    delay(1000);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print("Wifi status: ");
        Serial.println(WiFi.status());

        WiFi.begin(WIFI_SSID, WIFI_PW);
        delay(1000);
    }

    Serial.println("Wifi connected!");

    PixelblazeMemBuffer buffer = PixelblazeMemBuffer();
    PlaylistChangeWatcher watcher = PlaylistChangeWatcher();
    pbClient = new PixelblazeClient(wsClient, buffer, watcher);
    pbClient->begin();

    // Fetch a value and store it elsewhere. On non-AVR boards this can now use closures!
    pbClient->getPlaylist(extractPlaylistLen) 
        || Serial.printlin(F("Get playlist failed to dispatch"));
    
    // Make sure we're using a paused playlist
    pbClient->setSequencerMode(SEQ_MODE_PLAYLIST) 
        || Serial.printlin(F("Set sequencer mode failed to dispatch"));
    pbClient->pauseSequence() 
        || Serial.printlin(F("Pause failed to dispatch"));
}

uint32_t lastShuffle = millis();
void loop() {
    // The heart of the library's operation. Must be called a few times a second at least 
    // if the attached Pixelblaze is sending frame previews. Otherwise the more often the 
    // better, if there's nothing to do it will return almost immediately.
    // If it returns false, its connection died and was not re-openable, it will try some
    // more the next time checkForInbound() is called.
    if (pbClient->checkForInbound()) {
        Serial.println("Websocket connection failed and couldn't be recovered");
    }
    
    if (playlistLen > 0 && millis() - lastShuffle > PLAYLIST_DURATION_MS) {
        pbClient->setPlaylistIndex(random(0, playlistLen));
        lastShuffle = millis();
    }

    delay(100);
}
```

If you're looking at this and feeling race condition shivers up your spine, **good**. Instances of this library are very
explicitly **not** threadsafe, and the structs passed into "get" handlers are overwritten the minute that handler
returns. It is, however, safe to hold multiple client instances.

TODO
----
- Testing, testing, testing. I need to stop writing code and put together a rig to test all this, but that hasn't
  happened yet.
  - I don't have an expander or the energy to put together a rig with one for this, if you do let's talk?
  - I don't currently have a strong use case for sync, though I (hopefully) will soon, if you have one put together and
    know how to operate a browser dev utility, let's talk?
  - I just generally have really unpredictable energy levels, so just about any enthusiasm and help is welcome.
- Resting, resting, resting. I got a little too into this, I probably need to step away for a few days and at least
  fix my sleep schedule.
- Actually sit down with Ben to double check my understanding of a bunch of things.
- Actual docs, not just comments.
- Hunt down all the memory leaks I almost certainly included, I feel like I can hear valgrind cackling at me from its
  rightful place in hell.

Confession
----------
This is the first real C++ I've written in 12 years, and it shows. This looks like Java, and that's because that's what 
I spent most of those 12 years writing. In particular my understanding of changes to the C++ language is very new, and
my instinct for memory is coming more from the (to me) sunnier fields of Rust. Both of those may contribute to 
over-complication and memory leaks. I'm happy to hear constructive feedback on most things.

# 💜
