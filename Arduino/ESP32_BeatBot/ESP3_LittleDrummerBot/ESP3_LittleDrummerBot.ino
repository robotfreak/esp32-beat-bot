// === ESP3: Mobiler Roboter mit Drumsticks + MIDI-Empfang via ESP-NOW ===

#include <WiFi.h>
#include <esp_wifi.h>
#include <ESP32_NOW.h>
#include <ServoEasing.hpp>

#define ESPNOW_WIFI_CHANNEL 6

// === Servo Pins ===
#define ARM_LEFT_PIN  12
#define ARM_RIGHT_PIN 13
ServoEasing servoLeft;
ServoEasing servoRight;

// === MIDI-Befehle ===
#define MIDI_NOTE_ON  0x90
#define MIDI_NOTE_OFF 0x80
#define MIDI_CHANNEL_DRUMS 9

void hitDrum(ServoEasing& servo) {
  servo.setEasingType(EASE_CUBIC_IN_OUT);
  servo.setSpeed(120);
  servo.startEaseTo(150);
  delay(200);
  servo.startEaseTo(90);
}

void readMacAddress(){
  uint8_t baseMac[6];
  esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
  if (ret == ESP_OK) {
    Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
  } else {
    Serial.println("Failed to read MAC address");
  }
}

// Creating a new class that inherits from the ESP_NOW_Peer class is required.

class ESP_NOW_Peer_Class : public ESP_NOW_Peer {
public:
  // Constructor of the class
  ESP_NOW_Peer_Class(const uint8_t *mac_addr, uint8_t channel, wifi_interface_t iface, const uint8_t *lmk) : ESP_NOW_Peer(mac_addr, channel, iface, lmk) {}

  // Destructor of the class
  ~ESP_NOW_Peer_Class() {}

  // Function to register the master peer
  bool add_peer() {
    if (!add()) {
      log_e("Failed to register the broadcast peer");
      return false;
    }
    return true;
  }

  // Function to print the received messages from the master
  void onReceive(const uint8_t *data, size_t len, bool broadcast) {
    //Serial.printf("Received a message from master " MACSTR " (%s)\n", MAC2STR(addr()), broadcast ? "broadcast" : "unicast");
    //Serial.printf("msg received\n");
    if (len < 3) return;

    uint8_t status = data[0] & 0xF0;
    uint8_t channel = data[0] & 0x0F;
    uint8_t note = data[1];
    uint8_t velocity = data[2];

    if (status == MIDI_NOTE_ON && velocity > 0) {
      Serial.printf("midi: %d, %d, %d, %d\n", status, channel, note, velocity);
      switch (note) {
        case 36: hitDrum(servoLeft); break;     // Kick
        case 38: hitDrum(servoRight); break;    // Snare
      //case 42: moveForward(); delay(300); stopMotors(); break; // Hihat => Vorwärts
      //case 46: turnLeft(); delay(300); stopMotors(); break;    // Open Hihat => Links drehen
      }
    }
  }
};

/* Global Variables */

// List of all the masters. It will be populated when a new master is registered
// Note: Using pointers instead of objects to prevent dangling pointers when the vector reallocates
std::vector<ESP_NOW_Peer_Class *> masters;

/* Callbacks */

// Callback called when an unknown peer sends a message
void register_new_master(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
  if (memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) == 0) {
    Serial.printf("Unknown peer " MACSTR " sent a broadcast message\n", MAC2STR(info->src_addr));
    Serial.println("Registering the peer as a master");

    ESP_NOW_Peer_Class *new_master = new ESP_NOW_Peer_Class(info->src_addr, ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, nullptr);
    if (!new_master->add_peer()) {
      Serial.println("Failed to register the new master");
      delete new_master;
      return;
    }
    masters.push_back(new_master);
    Serial.printf("Successfully registered master " MACSTR " (total masters: %zu)\n", MAC2STR(new_master->addr()), masters.size());
  } else {
    // The slave will only receive broadcast messages
    log_v("Received a unicast message from " MACSTR, MAC2STR(info->src_addr));
    log_v("Igorning the message");
  }
}


void setup() {
  Serial.begin(115200);
  Serial.println("LDB started");
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
  }
  Serial.println("Wi-Fi parameters:");
  Serial.println("  Mode: STA");
  Serial.println("  MAC Address: " + WiFi.macAddress());
  Serial.printf("  Channel: %d\n", ESPNOW_WIFI_CHANNEL);

  readMacAddress();

  // Initialize the ESP-NOW protocol
  if (!ESP_NOW.begin()) {
    Serial.println("Failed to initialize ESP-NOW");
    Serial.println("Reeboting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  Serial.printf("ESP-NOW version: %d, max data length: %d\n", ESP_NOW.getVersion(), ESP_NOW.getMaxDataLen());

  // Register the new peer callback
  ESP_NOW.onNewPeer(register_new_master, nullptr);

  Serial.println("Setup complete. Waiting for a master to broadcast a message...");

  servoLeft.attach(ARM_LEFT_PIN, 90);
  servoRight.attach(ARM_RIGHT_PIN, 90);
  servoLeft.setEasingType(EASE_CUBIC_IN_OUT);
  servoRight.setEasingType(EASE_CUBIC_IN_OUT);
  setEaseToForAllServos();
}

void loop() {
  // wartet auf MIDI-Befehle via ESP-NOW
  // Print debug information every 10 seconds
  static unsigned long last_debug = 0;
  if (millis() - last_debug > 10000) {
    last_debug = millis();
    Serial.printf("Registered masters: %zu\n", masters.size());
    for (size_t i = 0; i < masters.size(); i++) {
      if (masters[i]) {
        Serial.printf("  Master %zu: " MACSTR "\n", i, MAC2STR(masters[i]->addr()));
      }
    }
  }
}
