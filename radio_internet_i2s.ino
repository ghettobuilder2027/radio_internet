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

String ssid = "";
String password = "";

const int BUTTON_1 = 4;
const int BUTTON_2 = 5;
const int BUTTON_3 = 21;
const int BUTTON_4 = 23;
Button2 btn1 = Button2(BUTTON_1);
Button2 btn2 = Button2(BUTTON_2);
Button2 btn3 = Button2(BUTTON_3);
Button2 btn4 = Button2(BUTTON_4);

unsigned int volume =  3;

String station ;
int stationNumber = 0;
int stationTotal;
String stations[12];


void read_spiffs_station () {
  int i = 0;
  String line;
  File file = SPIFFS.open("/radios.txt");
  if(!file){ Serial.println("Failed to open file ");return; }
  Serial.println("Found radios:");
  while(file.available()){        
    line = file.readStringUntil('\n'); // Read line by line from the file
    stations[i] = line;
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
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(1); // 0...21
  SPIFFS.begin(true);  
  read_spiffs_station ();
  
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
    Serial.print("station     ");Serial.println(info);
}

/*
void audio_info(const char *info){
    Serial.print("info        "); Serial.println(info);
}
void audio_id3data(const char *info){  //id3 metadata
    Serial.print("id3data     ");Serial.println(info);
}
void audio_eof_mp3(const char *info){  //end of file
    Serial.print("eof_mp3     ");Serial.println(info);
}

void audio_showstreaminfo(const char *info){
    Serial.print("streaminfo  ");Serial.println(info);
}
void audio_showstreamtitle(const char *info){
    Serial.print("streamtitle ");Serial.println(info);
}
void audio_bitrate(const char *info){
    Serial.print("bitrate     ");Serial.println(info);
}
void audio_commercial(const char *info){  //duration in sec
    Serial.print("commercial  ");Serial.println(info);
}
void audio_icyurl(const char *info){  //homepage
    Serial.print("icyurl      ");Serial.println(info);
}
void audio_lasthost(const char *info){  //stream URL played
    Serial.print("lasthost    ");Serial.println(info);
}
void audio_eof_speech(const char *info){
    Serial.print("eof_speech  ");Serial.println(info);
}
*/
