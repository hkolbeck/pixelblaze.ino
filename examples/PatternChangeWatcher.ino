#include <Arduino.h>
#include <WiFi.h>

#include "PixelblazeClient.h"
#include "PixelblazeMemBuffer.h"

/**
 * Expected format:
 *
 * #define WIFI_SSID "my-wifi"
 * #define WIFI_PW "hunter2"
 */
#include "wifi_secrets.h"

// Change these as needed
#define WEBSOCKET_HOST "192.168.4.1"
#define WEBSOCKET_PORT 81

/**
 * This is a definition of behavior to take when a pattern change is detected
 */
class PatternChangeWatcher : public PixelblazeWatcher {
public:
    PatternChangeWatcher() : PixelblazeWatcher() {}

    void handlePatternChange(SequencerState &patternChange) override {
        Serial.print(F("Pattern change detected. New pattern: "));
        Serial.print(patternChange.name);
        Serial.print(F(" ID: "));
        Serial.println(patternChange.activeProgramId);
    }
};

WiFiClient wifi;
WebSocketClient wsClient = WebSocketClient(wifi, WEBSOCKET_HOST, WEBSOCKET_PORT);
PixelblazeClient* pbClient = nullptr;
void setup() {
    Serial.begin(115200);
    while (!Serial);

    WiFi.begin(WIFI_SSID, WIFI_PW);
    delay(1000);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print("Wifi status: ");
        Serial.println(WiFi.status());
        Serial.print("Attempting to connect to Network named: ");
        Serial.println(WIFI_SSID);

        WiFi.begin(WIFI_SSID, WIFI_PW);
        delay(1000);
    }

    Serial.println("Wifi connected!");

    //Some reads involve buffering data across multiple messages. By default, this grabs 3 10kb buffers, but it can
    //be tweaked. If significant issues arise, there's also an implementation using an attached SD card. Other mediums
    //can be used by extending PixelblazeBuffer.
    PixelblazeMemBuffer buffer = PixelblazeMemBuffer();

    //Pixelblaze sends several message types unprompted, we're watching for active pattern changes
    PatternChangeWatcher unrequestedHandler = PatternChangeWatcher();

    pbClient = new PixelblazeClient(wsClient, buffer, unrequestedHandler);
    pbClient->begin();
}

void loop() {
    pbClient->checkForInbound();
    delay(100);
}
