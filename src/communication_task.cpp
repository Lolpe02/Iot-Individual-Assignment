#include "communication_task.h"

#include <math.h>
#include <stdio.h>

#include "shared.h"

#if USE_LORA
#include "LoRaWAN_ESP32.h"
#endif

static void setupWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected!, IP address: %s\n", WiFi.localIP().toString().c_str());
}

#if USE_LORA
EzLoRaWAN ttn;
EzLoRaWAN_CayenneLPP lpp;
bool isLoraConnected = false;

void message(const uint8_t* payload, size_t size, uint8_t port, int rssi) {
  Serial.println("-- LoRa MESSAGE --");
  Serial.printf("Received %d bytes on port %d (RSSI=%ddB):", size, port, rssi);
  for (int i = 0; i < size; i++) {
    Serial.printf(" %02X", payload[i]);
  }
  Serial.println();
}

void setupLoraConn() {
 
  ttn.begin();
  ttn.onMessage(message);
  ttn.join();  // Uses devEui, appEui, appKey from credentials
  Serial.print("[LoRa] Joining TTN ");
  while (!ttn.isJoined()) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\n[LoRa] Joined!");
  ttn.showStatus();
}
#endif

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Attempting connection...");
    if (mqttClient.connect("ESP32Client")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

void publishMQTT(const char *topic, const char *payload) {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();
  mqttClient.publish(topic, payload);
}

void setupCommunication() {
  #if USE_LORA
  Serial.println("[System] Using LoRa for communication");
  setupLoraConn();
  #else
  Serial.println("[System] Using MQTT for communication");
  setupWiFi();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  #endif
}

void TaskCommunication(void *pvParameters) {
  (void)pvParameters;

  uint16_t filledBufferCounter = 0;
  SignalStats stats;
  Serial.println("Starting Communication task...");
  while (1) {
    if (xQueueReceive(statsQueue, &stats, portMAX_DELAY) == pdTRUE) {

      int64_t timerstart = esp_timer_get_time();

      #if USE_LORA
      // Send average via LoRa
      if (!isLoraConnected) {
        setupLoraConn();
        isLoraConnected = true;
      }
      
      lpp.reset();
      lpp.addDigitalInput(1, (int)stats.mean);
      if (ttn.sendBytes(lpp.getBuffer(), lpp.getSize())) {
        Serial.printf("[LoRa] Sent average: %.2f (raw: %d)\n", stats.mean, (int)stats.mean);
      } else {
        Serial.println("[LoRa] Send failed");
      }
      #else
      // Send average via MQTT
      if (mqttClient.connected()) {
        char payload[160];
        snprintf(payload,
                 sizeof(payload),
                 "{\"mean\":%.2f,\"std\":%.2f,\"min\":%.2f,\"max\":%.2f}",
                 stats.mean,
                 stats.stdDev,
                 stats.min,
                 stats.max);
        mqttClient.publish("iot_single/stats", payload);
      }
      mqttClient.loop();
      #endif

      BlockTiming timingInfo = {
          .blockNumber = filledBufferCounter++,
          .start_timestamp = timerstart,
          .end_timestamp = esp_timer_get_time()};
      xQueueSend(communicationTimestampsQueue, &timingInfo, 10);
    }
  }
}
