#include <Button2.h>         // https://github.com/LennartHennigs/Button2
#include <SPIFFS.h>
#include <WiFiSettings.h>    // https://github.com/Juerd/ESP-WiFiSettings
#include <vs1053_ext.h>      // https://github.com/schreibfaul1/ESP32-vs1053_ext
#include "Arduino.h"
#include <SPI.h>
#include <WiFi.h>              
#include <ESP32WebServer.h>    // https://github.com/Pedroalbuquerque/ESP32WebServer 
#include <ESPmDNS.h>


// Webserver stuff
String webpage = "";
#define   servername "radio" 
ESP32WebServer server(80);
File UploadFile; 

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
const int BUTTON_1 = 27;
const int BUTTON_2 = 26;
const int BUTTON_3 = 25;
const int BUTTON_4 = 33;
Button2 btn1 = Button2(BUTTON_1);
Button2 btn2 = Button2(BUTTON_2);
Button2 btn3 = Button2(BUTTON_3);
Button2 btn4 = Button2(BUTTON_4);

unsigned int volume =  28;

// Radios
String station ;
String stations[4];


// Read station urls from flash memory
// Fills the 4 first lines of radios.txt
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

// Routine to deal with pres of 4 buttons
// Long press on 1 and 2 : volume down
// Long press on 3 and 4 : volume up
// Short press : change station
void longpress(Button2& btn) {
  unsigned int time = btn.wasPressedFor();
  bool long_click = false;
  String station_changed;
  station_changed = station;
  int s;
  if (time > 300) {
    long_click = true;
  }
  if (btn == btn1) s=0;
  else if (btn == btn2) s=1;
  else if (btn == btn3) s=2;
  else if (btn == btn4) s=3;
  
  if (long_click) {
    if ( s<2) {
      volume -= 2;  
      Serial.println("volume down");
      mp3.setVolume(volume);
    } else {
      volume += 2;  
      Serial.println("volume up");
      mp3.setVolume(volume);
    }
  } else {
    station_changed = stations[s];
    if (station_changed != station) {
      station = station_changed;
      mp3.connecttohost(station); 
    }
  }   
}

void setup() {
  Serial.begin(115200);

  // Long and short press handler
  btn1.setClickHandler(longpress);
  btn2.setClickHandler(longpress);
  btn3.setClickHandler(longpress);
  btn4.setClickHandler(longpress);
  btn1.setLongClickHandler(longpress);
  btn2.setLongClickHandler(longpress);
  btn3.setLongClickHandler(longpress);
  btn4.setLongClickHandler(longpress);
  
  // Spi connection for the VS 1053
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  
  // Load station urls from flash memory
  SPIFFS.begin(true);  
  read_spiffs_station ();
  
  // Connect wifi
  WiFiSettings.connect();
  Serial.print("Password: ");
  Serial.println(WiFiSettings.password);

  // We use the 2 cores of the esp32, 
  // one for the webserver the other one to deal with streaming
  // Webserver task
  TaskHandle_t webserver;
  xTaskCreatePinnedToCore(
                    WebserverTask,   /* Function to implement the task */
                    "Webserver", /* Name of the task */
                    10000,      /* Stack size in words */
                    NULL,       /* Task input parameter */
                    0,          /* Priority of the task */
                    &webserver,  /* Task handle. */
                    0);         /* Core where the task should run */
                    
  // Radio and buttons task
  TaskHandle_t main_loop;
  xTaskCreatePinnedToCore(
                    main_loop_Task,   /* Function to implement the task */
                    "main_loop", /* Name of the task */
                    10000,      /* Stack size in words */
                    NULL,       /* Task input parameter */
                    3,          /* Priority of the task */
                    &main_loop,  /* Task handle. */
                    1);         /* Core where the task should run */
  Serial.println("Tasks created...");
}

void main_loop_Task( void * pvParameters ){    
  
  // Start the streaming
  mp3.begin();
  mp3.printDetails();
  if (mp3.printVersion()) {
    Serial.println("vs1053 connected");
  } else {
    Serial.println("vs1053 not connected"); 
  }
  mp3.setVolume(volume);
  station = stations[0];
  mp3.connecttohost(station);  
  
  while(true){
    btn1.loop();
    btn2.loop();
    btn3.loop();
    btn4.loop();
    mp3.loop();
  }
}

void WebserverTask( void * pvParameters ){ 
  // Launching and parametring server
  if (!MDNS.begin(servername)) {          // Set your preferred server name, if you use "myserver" the address would be http://myserver.local/
    Serial.println(F("Error setting up MDNS responder!"));   
  } else {
    Serial.println(F("Setting up MDNS responder!"));   
  }
  server.on("/",         File_Upload);
  server.on("/upload",   File_Upload);
  server.on("/fupload",  HTTP_POST,[](){ server.send(200);}, handleFileUpload);
  server.begin();
  Serial.println("HTTP server started");

  //loop
  while (true){
    server.handleClient();
  }
  
}

// Main loop is empty 
void loop() {
  // empty ..
  vTaskDelete(NULL);
}

void vs1053_info(const char *info) {                // called from vs1053
  Serial.print("DEBUG:        ");
  Serial.println(info);                           // debug infos
}

void HomePage(){
  SendHTML_Header();
  webpage += F("<br/><br/>");
  webpage += F("<a href='/upload'><button>Upload radio list</button></a>");
  webpage += F("<br/><br/>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop(); // Stop is needed because no content length was sent
}

void File_Upload(){
  append_page_header();
  webpage += F("<h3>Select radios.txt file to upload</h3>"); 
  webpage += F("<FORM action='/fupload' method='post' enctype='multipart/form-data'>");
  webpage += F("<input class='buttons' style='width:40%' type='file' name='fupload' id = 'fupload' value=''><br>");
  webpage += F("<br><button class='buttons' style='width:10%' type='submit'>Upload File</button><br>");
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  server.send(200, "text/html",webpage);
}

void handleFileUpload(){ // upload a new file to the Filing system
  HTTPUpload& uploadfile = server.upload(); // See https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer/src
  String filename = uploadfile.filename;
  if(!filename.startsWith("/")) filename = "/"+filename;
  if (filename == "/radios.txt") {
    if(uploadfile.status == UPLOAD_FILE_START) {
      Serial.print("Upload File Name: "); Serial.println(filename);
      SPIFFS.remove(filename);                         // Remove a previous version, otherwise data is appended the file again
      UploadFile = SPIFFS.open(filename, FILE_WRITE);  // Open the file for writing in SPIFFS (create it, if doesn't exist)
      filename = String();
    }
    else if (uploadfile.status == UPLOAD_FILE_WRITE) {
      if(UploadFile) UploadFile.write(uploadfile.buf, uploadfile.currentSize); // Write the received bytes to the file
    } 
    else if (uploadfile.status == UPLOAD_FILE_END){
      if(UploadFile) {       // If the file was successfully created                                    
        UploadFile.close();   // Close the file again
        Serial.print("Upload Size: "); Serial.println(uploadfile.totalSize);
        webpage = "";
        append_page_header();
        webpage += F("<h3>File was successfully uploaded</h3>"); 
        webpage += F("<h2>Uploaded File Name: "); webpage += uploadfile.filename+"</h2>";
        webpage += F("<h2>File Size: "); webpage += file_size(uploadfile.totalSize) + "</h2><br/><br/>"; 
        webpage += F("<h1> Restarting in 2 seconds ... </h1>");
        append_page_footer();
        server.send(200,"text/html",webpage);
        delay(2000);
        ESP.restart();
      } 
      else {
        ReportCouldNotCreateFile("upload");
      }
    }
  }
  else {
    webpage = "";
    append_page_header();
    webpage += F("<br/><h3>File name must be radios.txt </h3>"); 
    webpage += F("<h2>Please reupload it with the right name");
    
    webpage += F("<br>");
    append_page_footer();
    server.send(200,"text/html",webpage);
    SendHTML_Stop();
  }
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SendHTML_Header(){
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate"); 
  server.sendHeader("Pragma", "no-cache"); 
  server.sendHeader("Expires", "-1"); 
  server.setContentLength(CONTENT_LENGTH_UNKNOWN); 
  server.send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves. 
  append_page_header();
  server.sendContent(webpage);
  webpage = "";
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SendHTML_Content(){
  server.sendContent(webpage);
  webpage = "";
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SendHTML_Stop(){
  server.sendContent("");
  server.client().stop(); // Stop is needed because no content length was sent
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void SelectInput(String heading1, String command, String arg_calling_name){
  SendHTML_Header();
  webpage += F("<h3>"); webpage += heading1 + "</h3>"; 
  webpage += F("<FORM action='/"); webpage += command + "' method='post'>"; // Must match the calling argument e.g. '/chart' calls '/chart' after selection but with arguments!
  webpage += F("<input type='text' name='"); webpage += arg_calling_name; webpage += F("' value=''><br>");
  webpage += F("<type='submit' name='"); webpage += arg_calling_name; webpage += F("' value=''><br>");
  webpage += F("<a href='/'>[Back]</a>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void ReportFileNotPresent(String target){
  SendHTML_Header();
  webpage += F("<h3>File does not exist</h3>"); 
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void ReportCouldNotCreateFile(String target){
  SendHTML_Header();
  webpage += F("<h3>Could Not Create Uploaded File (write-protected?)</h3>"); 
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
String file_size(int bytes){
  String fsize = "";
  if (bytes < 1024)                 fsize = String(bytes)+" B";
  else if(bytes < (1024*1024))      fsize = String(bytes/1024.0,3)+" KB";
  else if(bytes < (1024*1024*1024)) fsize = String(bytes/1024.0/1024.0,3)+" MB";
  else                              fsize = String(bytes/1024.0/1024.0/1024.0,3)+" GB";
  return fsize;
}
void append_page_header() {
  webpage  = F("<!DOCTYPE html><html>");
  webpage += F("<head>");
  webpage += F("<title>Change radio stations</title>"); // NOTE: 1em = 16px
  webpage += F("<meta name='viewport' content='user-scalable=yes,initial-scale=1.0,width=device-width'>");
  webpage += F("<style>");
  webpage += F("body{max-width:85%;margin:0 auto;font-family:arial;font-size:105%;text-align:center;color:blue;background-color:#F7F2Fd;}");
  webpage += F("ul{list-style-type:none;margin:0.1em;padding:0;border-radius:0.375em;overflow:hidden;background-color:#dcade6;font-size:1em;}");
  webpage += F("li{float:left;border-radius:0.375em;border-right:0.06em solid #bbb;}last-child {border-right:none;font-size:85%}");
  webpage += F("li a{display: block;border-radius:0.375em;padding:0.44em 0.44em;text-decoration:none;font-size:85%}");
  webpage += F("li a:hover{background-color:#EAE3EA;border-radius:0.375em;font-size:85%}");
  webpage += F("section {font-size:0.88em;}");
  webpage += F("h1{color:white;border-radius:0.5em;font-size:1em;padding:0.2em 0.2em;background:#558ED5;}");
  webpage += F("h2{color:orange;font-size:1.0em;}");
  webpage += F("h3{font-size:0.8em;}");
  webpage += F("table{font-family:arial,sans-serif;font-size:0.9em;border-collapse:collapse;width:85%;}"); 
  webpage += F("th,td {border:0.06em solid #dddddd;text-align:left;padding:0.3em;border-bottom:0.06em solid #dddddd;}"); 
  webpage += F("tr:nth-child(odd) {background-color:#eeeeee;}");
  webpage += F(".rcorners_n {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:20%;color:white;font-size:75%;}");
  webpage += F(".rcorners_m {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:50%;color:white;font-size:75%;}");
  webpage += F(".rcorners_w {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:70%;color:white;font-size:75%;}");
  webpage += F(".column{float:left;width:50%;height:45%;}");
  webpage += F(".row:after{content:'';display:table;clear:both;}");
  webpage += F("*{box-sizing:border-box;}");
  webpage += F("footer{background-color:#eedfff; text-align:center;padding:0.3em 0.3em;border-radius:0.375em;font-size:60%;}");
  webpage += F("button{border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:20%;color:white;font-size:130%;}");
  webpage += F(".buttons {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:15%;color:white;font-size:80%;}");
  webpage += F(".buttonsm{border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:9%; color:white;font-size:70%;}");
  webpage += F(".buttonm {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:15%;color:white;font-size:70%;}");
  webpage += F(".buttonw {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:40%;color:white;font-size:70%;}");
  webpage += F("a{font-size:75%;}");
  webpage += F("p{font-size:75%;}");
  webpage += F("</style></head><body><h1>Internet Radio</h1>");
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void append_page_footer(){ // Saves repeating many lines of code for HTML page footers
  webpage += F("<ul>");
  webpage += F("<li><a href='/upload'>Upload</a></li>"); 
  webpage += F("</ul>");
  webpage += "<footer>Ghetto Builder 2020</footer>";
  webpage += F("</body></html>");
}
