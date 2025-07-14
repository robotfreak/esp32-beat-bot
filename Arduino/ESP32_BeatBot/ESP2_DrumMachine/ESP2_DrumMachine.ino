// === ESP2: Beat Detection + Mikrofon (INMP441) + MIDI-Broadcast über ESP-NOW ===

#include <WiFi.h>
#include <esp_now.h>
#include <driver/i2s.h>

#define I2S_WS   15
#define I2S_SD   32
#define I2S_SCK  14
#define SAMPLE_RATE 44100
#define DETECT_THRESHOLD 4000

int16_t samples[1024];
unsigned long lastBeat = 0;
unsigned long beatInterval = 150; // Minimum Abstand in ms

// === ESP-NOW Setup ===
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void sendMidiBeat() {
  uint8_t midiMsg[3] = {0x99, 36, 127}; // Note On, Channel 10, Kick
  esp_now_send(broadcastAddress, midiMsg, sizeof(midiMsg));
}

void setupI2SMic() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void detectBeat() {
  size_t bytesRead;
  i2s_read(I2S_NUM_0, (void*)samples, sizeof(samples), &bytesRead, portMAX_DELAY);

  long sum = 0;
  for (int i = 0; i < 512; i++) {
    sum += abs(samples[i]);
  }
  sum /= 512;

  if (sum > DETECT_THRESHOLD && millis() - lastBeat > beatInterval) {
    lastBeat = millis();
    sendMidiBeat();
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init failed");
    return;
  }

  setupI2SMic();
}

void loop() {
  detectBeat();
}
