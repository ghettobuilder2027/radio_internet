#include <Button2.h>         // https://github.com/LennartHennigs/Button2
#include <SPIFFS.h>
#include <Audio.h>           // https://github.com/schreibfaul1/ESP32-audioI2S
#include "Arduino.h"
#include "WiFi.h"
 
// Digital I/O used
#define I2S_DOUT      33 // DIN connection   22
#define I2S_BCLK      26  // Bit clock        25
#define I2S_LRC       25    // Left Right Clock 26



Audio audio;

String ssid = "Livebox-E2B0";
String password = "rRqTxNZw62nZDaM5cd";



const int BUTTON_1 = 4;
const int BUTTON_2 = 5;
const int BUTTON_3 = 21;
const int BUTTON_4 = 23;
Button2 btn1 = Button2(BUTTON_1);
Button2 btn2 = Button2(BUTTON_2);
Button2 btn3 = Button2(BUTTON_3);
Button2 btn4 = Button2(BUTTON_4);

unsigned int volume =  8;

String station ;
int stationNumber = 0;
int stationTotal = 3;
String stations[3];


void read_stations() {
  stations[0] = "http://icecast.radiofrance.fr/franceculture-midfi.mp3";
  stations[1] = "http://icecast.radiofrance.fr/franceinter-midfi.mp3";
  stations[2] = "http://nova-ln.ice.infomaniak.ch/nova-ln-128.mp3";
  
 

}
void read_spiffs_station () {
  int i = 0;
  String line;
  File file = SPIFFS.open("/radios.txt");
  if(!file){ Serial.println("Failed to open file ");return; }
  Serial.println("Found radios:");
  while(file.available()){        
    line = file.readStringUntil('\n'); // Read line by line from the file
    stations[i] = line;
    Serial.println(line);
    i++;    
  }
  stationTotal = i;
  file.close();
  Serial.print("Nombre de stations : ");
  Serial.println(stationTotal);
  for (i=0; i<stationTotal; i++) {
    Serial.println(stations[i]);
  }
}

void longpress(Button2& btn) {
  String station_changed;
  station_changed = station;
  if (btn == btn1) changeVolume(1);
  else if (btn == btn2) changeVolume(-1);
  else if (btn == btn3) changeRadio(1);
  else if (btn == btn4) changeRadio(-1);
}

void changeVolume(int change) {
  if (change == -1 && volume > 1) {
    volume--;
    Serial.println("volume down");
  } else if (change == 1  && volume <21) {
    volume++;    
    Serial.println("volume up");
  }
  Serial.println(volume);
  audio.setVolume(volume);
}

void changeRadio(int change) {
  
  if (change==1) {
    if (stationNumber == stationTotal-1) {
      stationNumber=0 ;
      Serial.println ("retour à 0");
    } else {
      stationNumber++;
      Serial.println("On monte d'une radio");
    }
  } else if (change==-1){ 
    if (stationNumber == 0) {
      stationNumber=stationTotal-1 ;
      Serial.println ("Dernière station");
    } else {
      stationNumber--;
      Serial.println("On descend d'une radio");
    }
  }
  station = stations[stationNumber];
  Serial.println("radio changée");
  audio.connecttohost(station.c_str()); 
}
  

void setup() {
  Serial.begin(115200);
  Serial.println("Radio web is back, 2024 version");
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(1); // 0...21
  SPIFFS.begin(true);  
  read_stations ();

  
  btn1.setTapHandler(longpress);
  btn2.setTapHandler(longpress);
  btn3.setTapHandler(longpress);
  btn4.setTapHandler(longpress);
  Serial.println("Connecting to wifi");
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("...");
    delay(1500);
  }
  Serial.println("wifiiiiiiiiiiiiiiii");
  
  audio.setVolume(volume); // 0...21
   station = stations[stationNumber];
  audio.connecttohost(station.c_str()); 
}
void loop()
{
  audio.loop();
  btn1.loop();
  btn2.loop();
  btn3.loop();
  btn4.loop();
}
 
// optional
void audio_showstation(const char *info){
    Serial.print("station  ");Serial.println(info);
}

