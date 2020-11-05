#include <JC_Button.h>       // https://github.com/JChristensen/JC_Button
#include <SPIFFS.h>
#include <WiFiSettings.h>    // https://github.com/Juerd/ESP-WiFiSettings
#include <vs1053_ext.h>      // https://github.com/schreibfaul1/ESP32-vs1053_ext
#include "Arduino.h"
#include <SPI.h>



// Digital I/O used for VS1053
#define SPI_MOSI      23
#define SPI_MISO      19
#define SPI_SCK       18
#define VS1053_CS      2
#define VS1053_DCS     4
#define VS1053_DREQ   35
// RST relié à EN
VS1053 mp3(VS1053_CS, VS1053_DCS, VS1053_DREQ);


// Control stations
const byte BUTTON_1(26),BUTTON_2(27),BUTTON_3(25),BUTTON_4(33);
Button btn_1(BUTTON_1);
Button btn_2(BUTTON_2);
Button btn_3(BUTTON_3);
Button btn_4(BUTTON_4);


#define VOLUME  27

// Radios
String station ;
String stations[4];

void read_spiffs_station () {
  int i = 0;
  String line;
  File file = SPIFFS.open("/radios.txt");
  if(!file){ Serial.println("Failed to open file ");return; }
  Serial.println("Found radios:");
  while(file.available()){        
    line = file.readStringUntil('\n'); // Read line by line from the file
    stations[i] = line;
    if(i==3){       
      break;                             // exit after 4 stations
    }
    i++;    
  }
  file.close();
  for (i=0; i<4; i++) {
    Serial.println(stations[i]);
  }
}

void deal_with_buttons(){
  //Serial.println("button clicked ?");
  String station_changed;
  station_changed = station;
  if (btn_1.wasPressed()) {  
    Serial.println("1 clicked");
    station_changed = stations[0];
  } else if (btn_2.wasPressed()) {  
    Serial.println("2 clicked");
    station_changed = stations[1];
  } else if (btn_3.wasPressed()) {  
    Serial.println("3 clicked");
    station_changed = stations[2];
  } else if (btn_4.wasPressed()) {  
    Serial.println("4 clicked");
    station_changed = stations[3];
  }
  if (station_changed != station) {
    station = station_changed;
    mp3.connecttohost(station);    
  }
}

void setup() {

  Serial.begin(115200);
  
  // Init buttons
  btn_1.begin();
  btn_2.begin();
  btn_3.begin();
  btn_4.begin();
  
  // Spi connection for the VS 1053
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  SPIFFS.begin(true);  
  read_spiffs_station ();
  station = stations[0];
  
  
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
  mp3.connecttohost(station);  
}

void loop() {
  btn_1.read();
  btn_2.read();
  btn_3.read();
  btn_4.read();
  deal_with_buttons();
  
  mp3.loop();
  
}





void vs1053_info(const char *info) {                // called from vs1053
  Serial.print("DEBUG:        ");
  Serial.println(info);                           // debug infos
}
