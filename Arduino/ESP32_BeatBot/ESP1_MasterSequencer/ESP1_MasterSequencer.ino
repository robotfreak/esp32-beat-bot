// === ESP1 Master Sequencer mit Webinterface und MIDI-über-ESP-NOW Broadcast ===

#include <WiFi.h>
#include <ESP32_NOW.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include "LCD_Driver.h"
#include "GUI_Paint.h"
#include "image.h"

// === WLAN (für Webinterface) ===
const char* ssid = "BeatBot";
const char* password = "12345678";
AsyncWebServer server(80);

#define ESPNOW_WIFI_CHANNEL 6

// Creating a new class that inherits from the ESP_NOW_Peer class is required.

class ESP_NOW_Broadcast_Peer : public ESP_NOW_Peer {
public:
  // Constructor of the class using the broadcast address
  ESP_NOW_Broadcast_Peer(uint8_t channel, wifi_interface_t iface, const uint8_t *lmk) : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, channel, iface, lmk) {}

  // Destructor of the class
  ~ESP_NOW_Broadcast_Peer() {
    remove();
  }

  // Function to properly initialize the ESP-NOW and register the broadcast peer
  bool begin() {
    if (!ESP_NOW.begin() || !add()) {
      log_e("Failed to initialize ESP-NOW or register the broadcast peer");
      return false;
    }
    return true;
  }

  // Function to send a message to all devices within the network
  bool send_message(const uint8_t *data, size_t len) {
    if (!send(data, len)) {
      log_e("Failed to broadcast message");
      return false;
    }
    return true;
  }
};

/* Global Variables */

uint32_t msg_count = 0;

// Create a broadcast peer object
ESP_NOW_Broadcast_Peer broadcast_peer(ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, nullptr);

// === MIDI- und Pattern-Daten ===
//uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t broadcastAddress[] = {0xE0, 0x5A, 0x1B, 0x15, 0x18, 0x34};
//e0:5a:1b:15:18:34
uint8_t step = 0;
unsigned long lastStepTime = 0;
unsigned long stepInterval = 200;
bool kick[8]  = {1,0,0,0,1,0,0,0};
bool snare[8] = {0,0,1,0,0,0,1,0};
bool hat[8]   = {1,1,1,1,1,1,1,1};
bool clap[8]  = {0,0,0,0,0,0,0,0};
bool isRecording = false;

// === MIDI senden ===
void sendMidiNote(uint8_t note, uint8_t velocity) {
  uint8_t data[] = {0x99, note, velocity};

  if (!broadcast_peer.send_message((uint8_t *)data, sizeof(data))) {
    Serial.println("Failed to broadcast message");
  }
}

void playStep(uint8_t s) {
  if (kick[s]) sendMidiNote(36, 127);   // Kick
  if (snare[s]) sendMidiNote(38, 127);  // Snare
  if (hat[s]) sendMidiNote(42, 100);    // Closed Hihat
  if (clap[s]) sendMidiNote(39, 100);   // Clap
}

void parsePattern(String data, bool *track) {
  for (int i = 0; i < 8 && i < data.length(); i++) {
    track[i] = (data[i] == '1');
  }
}


String htmlPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='utf-8'>
  <style>
    body { font-family: sans-serif; }
    .step { width:30px; height:30px; margin:2px; display:inline-block; text-align:center; line-height:30px; border:1px solid #ccc; cursor:pointer; }
    .on { background:#0c0; color:#fff; }
    button { margin:5px; }
  </style>
  <script>
    let kick = [)rawliteral";
  for (int i = 0; i < 8; i++) html += String(kick[i]) + (i < 7 ? "," : "");
  html += R"rawliteral(];
    let snare = [)rawliteral";
  for (int i = 0; i < 8; i++) html += String(snare[i]) + (i < 7 ? "," : "");
  html += R"rawliteral(];
    let hat = [)rawliteral";
  for (int i = 0; i < 8; i++) html += String(hat[i]) + (i < 7 ? "," : "");
  html += R"rawliteral(];
    let clap = [)rawliteral";
  for (int i = 0; i < 8; i++) html += String(clap[i]) + (i < 7 ? "," : "");
  html += R"rawliteral(];
    function toggle(track,i){
      eval(track)[i] = eval(track)[i] ? 0 : 1;
      draw();
    }
    function draw(){
      ['kick','snare','hat','clap'].forEach(track=>{
        let html='';
        for(let i=0;i<8;i++){
          html += `<div class='step ${eval(track)[i] ? 'on' : ''}' onclick='toggle("${track}",${i})'>${i+1}</div>`;
        }
        document.getElementById(track).innerHTML = html;
      });
    }
    function send(){
      fetch('/update',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:`kick=${kick.join('')}&snare=${snare.join('')}&hat=${hat.join('')}&clap=${clap.join('')}`});
    }
    function rec(){
      fetch('/record');
    }
    function tempoChanged(val){
      document.getElementById('tempoValue').innerText = val + ' ms';
      fetch('/tempo?value=' + val);
    }
    window.onload=draw;
  </script>
</head>
<body>
  <h2>Beat Editor</h2>
  <h3>Kick</h3><div id='kick'></div>
  <h3>Snare</h3><div id='snare'></div>
  <h3>HiHat</h3><div id='hat'></div>
  <h3>Clap</h3><div id='clap'></div>
  <br>
  <button onclick='send()'>Speichern</button>
  <button onclick='rec()'>Aufnahme an/aus</button>
  <h3>Tempo</h3>
  <input type='range' min='60' max='1000' value='" + String(stepInterval) + "' onchange='tempoChanged(this.value)'><span id='tempoValue'>" + String(stepInterval) + " ms</span>"
</body>
</html>
)rawliteral";
  return html;
}

void intToIpAddress(uint32_t ip, char *result) {
    sprintf(result, "%d.%d.%d.%d", ip & 255,(ip >> 8) & 255,(ip >> 16) & 255,(ip >> 24) & 255);
}


void setupWebUI() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", htmlPage());
  });
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("kick", true)) parsePattern(request->getParam("kick", true)->value(), kick);
    if (request->hasParam("snare", true)) parsePattern(request->getParam("snare", true)->value(), snare);
    if (request->hasParam("hat", true)) parsePattern(request->getParam("hat", true)->value(), hat);
    if (request->hasParam("clap", true)) parsePattern(request->getParam("clap", true)->value(), clap);
    request->send(200, "text/plain", "OK");
  });
  server.on("/record", HTTP_GET, [](AsyncWebServerRequest *request){
    isRecording = !isRecording;
    request->send(200, "text/plain", isRecording ? "Recording ON" : "Recording OFF");
  });
  server.on("/tempo", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("value")) {
      stepInterval = request->getParam("value")->value().toInt();
    }
    request->send(200, "text/plain", "Tempo gesetzt");
  });
  server.begin();
}

void setup() {
  uint32_t ipAddress;
  char ipAddressStr[16]; 

  Config_Init();
  LCD_Init();
  Serial.begin(115200);
  Paint_NewImage(LCD_WIDTH, LCD_HEIGHT, 90, WHITE);
  Paint_SetRotate(270);
  LCD_Clear(0x000f);
  
   // Set the device as a Station and Soft Access Point simultaneously
  WiFi.mode(WIFI_AP_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
 
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFailed to connect to WiFi. Starting Access Point...");
    WiFi.softAP(ssid, password);
    Serial.print("AP IP address: ");
    ipAddress = WiFi.softAPIP();
    // You can now access the AP with the SSID and password
    // and configure the correct WiFi credentials via a web interface
  } else {
    Serial.println("\nConnected to WiFi!");
    ipAddress = WiFi.localIP();
  }
  Serial.print("Wi-Fi Channel: ");
  Serial.println(WiFi.channel());
  
  //uint32_t ipAddress = WiFi.softAPIP();
  intToIpAddress(ipAddress, ipAddressStr);
  Paint_DrawString_EN(0, 30, "Server started", &Font24, 0x000f, 0xfff0);
  Paint_DrawString_EN(0, 60, "IP:", &Font24, 0x000f, 0xfff0);
  Paint_DrawString_EN(55, 63, ipAddressStr, &Font20, 0x000f, 0xfff0);
  Serial.print("AP IP address: ");
  Serial.println(ipAddressStr);
  Serial.println("Server started");
  setupWebUI();

  // Register the broadcast peer
  if (!broadcast_peer.begin()) {
    Serial.println("Failed to initialize broadcast peer");
    Serial.println("Reebooting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  Serial.printf("ESP-NOW version: %d, max data length: %d\n", ESP_NOW.getVersion(), ESP_NOW.getMaxDataLen());


}

void loop() {
  unsigned long now = millis();
  if (now - lastStepTime > stepInterval) {
    lastStepTime = now;
    playStep(step);
    step = (step + 1) % 8;
    if ((step % 8) == 0)
      Serial.print('.');
  }
}
