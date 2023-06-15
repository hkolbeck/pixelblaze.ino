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

#define PLAYLIST_DURATION_MS 30000

/**
 * Right now the Pixelblaze can play a playlist or shuffle everything, let's (awkwardly) shuffle a playlist!
 *
 */
static size_t playlistLen = 0;
class PlaylistChangeWatcher : public PixelblazeWatcher {
public:
    void handlePlaylistChange(PlaylistUpdate &playlistUpdate) override {
        playlistLen = playlistUpdate.numItems;
    }
};

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

    pbClient->getPlaylist(extractPlaylistLen);

}

uint32_t lastShuffle = millis();
void loop() {
    if (!pbClient->checkForInbound()) {
        Serial.println("Websocket connection failed and couldn't be recovered");
    }

    if (playlistLen > 0 && millis() - lastShuffle > PLAYLIST_DURATION_MS) {
        pbClient->setPlaylistIndex(random(0, playlistLen));
        lastShuffle = millis();
    }

    delay(100);
}
