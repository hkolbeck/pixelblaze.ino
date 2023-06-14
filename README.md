# Pixelblaze.ino
## An Arduino-focused C++ library for the Pixelblaze LED controller's websocket API
### Status: Not even alpha.

Information about the Pixelblaze can be found at https://electromage.com. Needless to say I think they're pretty neat.

This builds extensively off of @zranger1's Python library: https://github.com/zranger1/pixelblaze-client, but its 
operation is fundamentally different. Where the python client is largely synchronous, I wanted to be able to fire and
forget with eventual updates or register handlers for events that come in with no request issued.

In general, the client works in three ways depending on the operation:
 - "set" operations: fire and forget with no completion-ack possible, only assurance is that the message was acked by 
   the Pixelblaze
 - "get" operations: send a request, passing along a handler to be invoked when or if a response is received. That
   handler will be passed the most parsed and annotated version of the data possible.
 - "watch" operations: The pixelblaze can send hundreds of events per second to connected clients with no prompting.
   If you're interested in for instance, notifications of pattern changes, you need a watcher.


```C++
#include "wifi_secrets.h"
#define WEBSOCKET_HOST "192.168.4.1"
#define WEBSOCKET_PORT 81

PixelblazeClient* pbClient = nullptr;
void setup() {
    WiFiClient wifi;
    WiFi.begin(WIFI_SSID, WIFI_PW);
    WebSocketClient wsClient = WebSocketClient(wifi, WEBSOCKET_HOST, WEBSOCKET_PORT);

    //We need somewhere to buffer relatively large partial reads, provided are in memory and SD card
    PixelblazeMemBuffer buffer = PixelblazeMemBuffer();
    
    //To watch for a given event, override the appropriate method on PixelblazeUnrequestedHandler
    PixelblazeUnrequestedHandler unrequestedHandler = PixelblazeUnrequestedHandler();
    
    pbClient = new PixelblazeClient(wsClient, buffer, unrequestedHandler);
    pbClient->begin();
}

void loop() {
    pbClient->checkForInbound();
    
    pbClient->
}
```


Confession
----------
This is the first real C++ I've written in 12 years, and it shows. This looks like Java, and that's because that's what 
I spent most of those 12 years writing. In particular my understanding of changes to the C++ language is very new, and
my instinct for memory is coming more from the (to me) sunnier fields of Rust. Both of those may contribute to 
over-complication and memory leaks. I'm happy to hear constructive feedback on most things.

# 💜
