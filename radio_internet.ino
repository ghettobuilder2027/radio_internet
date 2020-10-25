/**
  Based on Vince Gellár (github.com/vincegellar) 
  and on https://github.com/frokats/Internet_Radio_ESP8266_VS1053
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
char *host = "icecast.radiofrance.fr"; 
char *path = "/franceinter-midfi.mp3";
int httpPort = 80;
//const char *host = "icecast.radiofrance.fr"; 
//const char *path = "/fip-lofi.aac?id=radiofrance";
//int httpPort = 80;

unsigned long TimePlay;


uint8_t mp3buff[32];   // vs1053 likes 32 bytes at a time

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
    TimePlay=millis();
}

void loop() {
  if (!client.connected()) {
    Serial.println("Reconnecting...");
    Connect2Radio(host, httpPort, path);
  }
  play_buffer();
  
  
}

void play_buffer(){
  if (client.available() > 0){
    uint8_t bytesread = client.read(mp3buff, 32);
    player.playChunk(mp3buff, bytesread);
  }  
}

void Connect2Radio(char *url, int pport , char *chemin )
{
  client.stop();
  client.flush();
  yield();
  delay(100);
  if (client.connect(url, pport)) {
    yield();
    client.print(String("GET ") + chemin + String ( " HTTP/1.1\r\n" ) +
                 String ( "Host: " ) + url + String ( "\r\n" ) +
                 String ( "Icy-MetaData:0") + String ("\r\n" ) +
                 String ( "Connection: close\r\n\r\n" ) ) ;
    yield();
    delay(200);           
  }
  else {
    Serial.println("No connection");
  }
}
