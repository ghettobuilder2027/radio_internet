#include <SPIFFS.h>
#include <WiFiSettings.h>    // https://github.com/Juerd/ESP-WiFiSettings
#include <vs1053_ext.h>      // https://github.com/schreibfaul1/ESP32-vs1053_ext
#include "Arduino.h"
#include <SPI.h>

//#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager    To avoid hardcoding wifi credentials

// Digital I/O used
#define SPI_MOSI      23
#define SPI_MISO      19
#define SPI_SCK       18
#define VS1053_CS      2
#define VS1053_DCS     4
#define VS1053_DREQ   35
// RST relié à EN
#define VOLUME  12


VS1053 mp3(VS1053_CS, VS1053_DCS, VS1053_DREQ);

void setup() {
  // Spi connection for the VS 1053
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  Serial.begin(115200);
  // Wifi code
  SPIFFS.begin(true);  // On first run, will format after failing to mount
  WiFiSettings.connect();
  Serial.print("Password: ");
  Serial.println(WiFiSettings.password);
    
  mp3.begin();
  mp3.printDetails();
  if (mp3.printVersion()) {
    Serial.println("vs1053 connected");
  } else {
    Serial.println("vs1053 not connected"); 
  }
  mp3.setVolume(VOLUME);
  mp3.connecttohost("icecast.radiofrance.fr/franceculture-midfi.mp3");  
}

void loop()
{
    mp3.loop();
}

void vs1053_info(const char *info) {                // called from vs1053
    Serial.print("DEBUG:        ");
    Serial.println(info);                           // debug infos
}
