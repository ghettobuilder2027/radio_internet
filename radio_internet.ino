/**
  Based on Vince Gellár (github.com/vincegellar) Licensed under GNU GPL v3
  Wiring:
  --------------------------------
  | VS1053  | ESP8266 |  ESP32   |
  --------------------------------
  |   SCK   |   D5    |   IO18   |
  |   MISO  |   D6    |   IO19   |
  |   MOSI  |   D7    |   IO23   |
  |   XRST  |   RST   |   EN     |
  |   CS    |   D1    |   IO5    |
  |   DCS   |   D0    |   IO16   |
  |   DREQ  |   D3    |   IO4    |
  |   5V    |   5V    |   5V     |
  |   GND   |   GND   |   GND    |
  --------------------------------
  
**/
#include <VS1053.h>              // https://github.com/baldram/ESP_VS1053_Library
#include <ESP8266WiFi.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager    To avoid hardcoding wifi credentials

#define VS1053_CS     D1
#define VS1053_DCS    D0
#define VS1053_DREQ   D3

// Default volume
#define VOLUME  80

VS1053 player(VS1053_CS, VS1053_DCS, VS1053_DREQ);
WiFiClient client;

// webradio 
const char *host = "icecast.radiofrance.fr"; 
const char *path = "/franceinter-lofi.mp3";
int httpPort = 80;


// The buffer size 64 seems to be optimal. At 32 and 128 the sound might be brassy.
uint8_t mp3buff[64];

void setup() {
    Serial.begin(115200);
    delay(4000); // Wait for VS1053 and PAM8403 to power up
    Serial.println("Radio wifi");
    WiFiManager wifiManager;
    wifiManager.autoConnect("Configure_webradio");
    Serial.println("Connected.");

    SPI.begin();
    player.begin();
    if (player.isChipConnected()){
      Serial.println("Connection avec la carte okay");
    } else {
      Serial.println("Probleme de connection avec la carte");
    }
    player.switchToMp3Mode(); // Needed to pass from midi to mp3mode
    player.setVolume(VOLUME);

    Serial.print("connecting to ");
    Serial.println(host);

    if (!client.connect(host, httpPort)) {
        Serial.println("Connection host failed");
        return;
    }

    Serial.print("Requesting stream: ");
    Serial.println(path);
    client.print(String("GET ") + path + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
}

void loop() {
    if (!client.connected()) {
        Serial.println("Reconnecting...");
        if (client.connect(host, httpPort)) {
            client.print(String("GET ") + path + " HTTP/1.1\r\n" +
                         "Host: " + host + "\r\n" +
                         "Connection: close\r\n\r\n");
        }
    }
    if (client.available() > 0) {
        // The buffer size 64 seems to be optimal. At 32 and 128 the sound might be brassy.
        uint8_t bytesread = client.read(mp3buff, 64);
        player.playChunk(mp3buff, bytesread);
    }
}
