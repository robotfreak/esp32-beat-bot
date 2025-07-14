#include <esp_now.h>
#include <WiFi.h>

typedef struct struct_message {
  char text[32];
} struct_message;

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  struct_message incoming;
  memcpy(&incoming, data, sizeof(incoming));
  Serial.print("Empfangen: ");
  Serial.println(incoming.text);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Fehler bei ESP-NOW Init");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);
}

void loop() {}

