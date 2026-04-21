#include "communication_task.h"

#include <math.h>
#include <stdio.h>

#include "shared.h"

static void setupWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected!, IP address: %s\n", WiFi.localIP().toString().c_str());
}

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
  setupWiFi();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
}

void TaskCommunication(void *pvParameters) {
  (void)pvParameters;

  uint8_t filledBufferCounter = 0;
  Serial.println("Starting Communication task...");
  while (1) {
    if (xSemaphoreTake(xCommReady, portMAX_DELAY) == pdTRUE) {
      int64_t timerstart = esp_timer_get_time();

      float localProcReal[SAMPLES];
      for (int i = 0; i < SAMPLES; i++) {
        localProcReal[i] = procReal[i];
      }
      xSemaphoreGive(xCommReady);

      float max = 0.0f;
      float min = 1000000.0f;
      float sum = 0.0f;
      for (int i = 0; i < SAMPLES; i++) {
        sum += localProcReal[i];
        if (localProcReal[i] > max) {
          max = localProcReal[i];
        }
        if (localProcReal[i] < min) {
          min = localProcReal[i];
        }
      }

      float mean = sum / (float)SAMPLES;
      float sumSq = 0.0f;
      for (int i = 0; i < SAMPLES; i++) {
        float diff = localProcReal[i] - mean;
        sumSq += diff * diff;
      }
      float stdDev = sqrtf(sumSq / (float)SAMPLES);

      if (mqttClient.connected()) {
        char payload[160];
        snprintf(payload,
                 sizeof(payload),
                 "{\"mean\":%.2f,\"std\":%.2f,\"min\":%.2f,\"max\":%.2f}",
                 mean,
                 stdDev,
                 min,
                 max);
        mqttClient.publish("iot_single/stats", payload);
      }
      mqttClient.loop();

      BlockTiming timingInfo = {
          .blockNumber = filledBufferCounter++,
          .start_timestamp = timerstart,
          .end_timestamp = esp_timer_get_time()};
      xQueueSend(communicationTimestampsQueue, &timingInfo, 10);
    }
  }
}
