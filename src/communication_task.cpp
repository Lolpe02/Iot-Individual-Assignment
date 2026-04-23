#include "communication_task.h"

#include <math.h>
#include <stdio.h>

#include "shared.h"
#include "config.h"
#include "secrets.h"

#if USE_LORA
#define LORA_NSS  8
#define LORA_DIO1 14
#define LORA_NRST 12
#define LORA_BUSY 13
#include "RadioLib.h" 

bool isLoraConnected = false;
static uint32_t nextLoraTxMs = 0;
// Radio object
SX1262 radio(new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY));

// LoRaWAN node object
LoRaWANNode node(&radio, &EU868);

static void logLoraDownlink(const uint8_t* payload, size_t size, int16_t rxWindow) {
  int64_t now = esp_timer_get_time();
  int64_t rtt = now - sendTimestamp;
  Serial.printf("[LORA] Downlink RX%d | RTT: %.2f ms | %u bytes\n",
                rxWindow,
                rtt / 1000.0f,
                (unsigned)size);
  for (size_t i = 0; i < size; i++) {
    Serial.printf(" %02X", payload[i]);
  }
  Serial.println();
  PRINT_TIMING(TASK_COMM, filledBufferCounter++, sendTimestamp, now);
}

void setupLoraConn() {
  Serial.println("[LoRa] Initializing SX1262...");
  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
      Serial.printf("[LoRa] Radio error: %d\n", state);
      return; // Non usiamo vTaskDelete qui per non uccidere il task chiamante
  }

  Serial.println("[LoRa] Setting up TTN session (ABP)...");
  state = node.beginABP(devAddr, nwkSKey, nwkSKey, nwkSKey, appSKey);
  
  if (state == RADIOLIB_ERR_NONE) {
      state = node.activateABP();
  }

  if (state == RADIOLIB_LORAWAN_NEW_SESSION || state == RADIOLIB_ERR_NONE) {
      Serial.println("[LoRa] Successfully ready for TTN!");
      isLoraConnected = true;
  } else {
      Serial.printf("[LoRa] ABP Setup failed, error: %d\n", state);
  }
}
#endif

static int64_t sendTimestamp = 0;   // rtt timestamp 
static uint16_t filledBufferCounter = 0; // static means it stays here across calls, and it's only visible in this file

static void setupWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected!, IP address: %s\n", WiFi.localIP().toString().c_str());
}

// Callback chiamata quando arriva un messaggio sul topic subscribed
void onMQTTMessage(char* topic, byte* payload, unsigned int length) {
  int64_t now = esp_timer_get_time();
  int64_t rtt = now - sendTimestamp;
  Serial.printf("[MQTT] Round-trip time: %.2f ms\n", rtt / 1000.0f);
  //xQueueSend(communicationTimestampsQueue, &timingInfo, 10);
  PRINT_TIMING(TASK_COMM, filledBufferCounter++, sendTimestamp, now);
    

}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Attempting connection...");
    if (mqttClient.connect("ESP32Client")) {
      Serial.println("connected");
      mqttClient.subscribe("iot_single/response");  // ← subscribe alla risposta
    } else {
      Serial.printf("failed rc=%d, retry in 5s\n", mqttClient.state());
      delay(2000);
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
    mqttClient.setCallback(onMQTTMessage);
    if (!mqttClient.connected()) {
        reconnectMQTT();
    } 
  #endif
}

void TaskCommunication(void *pvParameters) {
  (void)pvParameters;

  SignalStats stats;
  Serial.println("Starting Communication task...");
  uint16_t loraPacketCounter = 0;
  while (1) {
    if (xQueueReceive(statsQueue, &stats, portMAX_DELAY) == pdTRUE) {

      int64_t timerstart = esp_timer_get_time();
      
      #if USE_LORA      // 4bytes payload: 2 for mean (x10), 2 for packet counter
      // Send average via LoRa
      sendTimestamp = timerstart;
      if (!isLoraConnected) {
          setupLoraConn();
      }

      if (isLoraConnected) {
          uint32_t nowMs = millis();
          if (nowMs < nextLoraTxMs) {
            static uint32_t lastWaitLogMs = 0;
            if ((nowMs - lastWaitLogMs) > 1000) {
              Serial.printf("[LoRa] Waiting duty-cycle window: %lu ms\n", (unsigned long)(nextLoraTxMs - nowMs));
              lastWaitLogMs = nowMs;
            }
            continue;
          }

          loraPacketCounter++;
          
          // 1. Prepariamo il dato (usiamo stats.mean dalla Queue!)
          uint16_t dataToSend = (uint16_t)(stats.mean * 10);
          
          uint8_t payload[4]; 
          payload[0] = (uint8_t)(dataToSend >> 8);   // MSB
          payload[1] = (uint8_t)(dataToSend & 0xFF); // LSB
          payload[2] = (uint8_t)(loraPacketCounter >> 8);
          payload[3] = (uint8_t)(loraPacketCounter & 0xFF);
          
          // 2. Cronometro per RTT
          sendTimestamp = esp_timer_get_time();
          uint32_t startMillis = millis();

          // 3. INVIO (Funzione bloccante che aspetta anche la finestra RX)
          Serial.printf("[LoRa] Sending packet %d (unconfirmed)...\n", loraPacketCounter);
          uint8_t downlink[64];
          size_t downlinkLen = sizeof(downlink);
          int state = node.sendReceive(payload, sizeof(payload), 1, downlink, &downlinkLen);
          
          uint32_t loraLatency = millis() - startMillis;
          int64_t now = esp_timer_get_time();

            if (state > 0) {
              Serial.printf("[LoRa] Uplink+Downlink | AVG: %.2f | Latency: %u ms\n", stats.mean, loraLatency);
              logLoraDownlink(downlink, downlinkLen, state);
            } else if (state == RADIOLIB_ERR_NONE) {
              Serial.printf("[LoRa] Uplink OK (no downlink) | AVG: %.2f | Latency: %u ms\n", stats.mean, loraLatency);
          } else {
              Serial.printf("[LoRa] Send failed: %d\n", state);
          }

            // Pace next uplink to avoid long blocking inside sendReceive when duty-cycle applies.
            RadioLibTime_t lastToA = node.getLastToA();
            RadioLibTime_t minIntervalMs = node.dutyCycleInterval(36000, lastToA); // 1% duty cycle
            nextLoraTxMs = millis() + (uint32_t)minIntervalMs;
            vTaskDelay(pdMS_TO_TICKS(500)); // Piccola pausa per evitare di saturare la CPU in attesa della finestra
      }
      #else
      // Send average via MQTT
      sendTimestamp = timerstart;
      if (!mqttClient.connected()) {
          reconnectMQTT();
      } 
      char payload[160]; //56 bytes for some reason
      snprintf(payload,
                sizeof(payload),
                "{\"mean\":%.2f,\"std\":%.2f,\"min\":%.2f,\"max\":%.2f}",
                stats.mean,
                stats.stdDev,
                stats.min,
                stats.max);
      mqttClient.publish("iot_single/stats", payload); // rtt: 5.273 ms
      mqttClient.loop();
      #endif
      }
  }
}
/* lora
[LoRa] Sending packet 1...
[LoRa] Uplink OK (no downlink) | AVG: 1886.99 | Latency: 2871 ms
[LoRa] Waiting duty-cycle window: 18058 ms
[LoRa] Sending packet 2...
[LoRa] Uplink OK (no downlink) | AVG: 1885.00 | Latency: 2863 ms
[LoRa] Sending packet 3...
[LoRa] Uplink OK (no downlink) | AVG: 1885.87 | Latency: 2863 ms





*/

/*
cd C:\Program Files\Mosquitto
mosquitto -c mosquitto.conf -v
mosquitto_sub -t "iot_single/stats" | mosquitto_pub -l -t "iot_single/response"

1776943053: Sending PUBLISH to ESP32Client (d0, q0, r0, m0, 'iot_single/response', ... (56 bytes))
1776943053: Received PUBLISH from ESP32Client (d0, q0, r0, m0, 'iot_single/stats', ... (56 bytes))
1776943053: Sending PUBLISH to auto-856FDC9D-0D2D-3404-8A3F-AA822E8D00C0 (d0, q0, r0, m0, 'iot_single/stats', ... (56 bytes))
1776943053: Received PUBLISH from auto-3D229021-8978-AFE6-0763-76C2D917971E (d0, q0, r0, m0, 'iot_single/response', ... (56 bytes))
1776943053: Sending PUBLISH to ESP32Client (d0, q0, r0, m0, 'iot_single/response', ... (56 bytes))
1776943054: Received PUBLISH from ESP32Client (d0, q0, r0, m0, 'iot_single/stats', ... (56 bytes))
1776943054: Sending PUBLISH to auto-856FDC9D-0D2D-3404-8A3F-AA822E8D00C0 (d0, q0, r0, m0, 'iot_single/stats', ... (56 bytes))
1776943054: Received PUBLISH from auto-3D229021-8978-AFE6-0763-76C2D917971E (d0, q0, r0, m0, 'iot_single/response', ... (56 bytes))
1776943054: Sending PUBLISH to ESP32Client (d0, q0, r0, m0, 'iot_single/response', ... (56 bytes))
1776943054: Received PUBLISH from ESP32Client (d0, q0, r0, m0, 'iot_single/stats', ... (56 bytes))
1776943054: Sending PUBLISH to auto-856FDC9D-0D2D-3404-8A3F-AA822E8D00C0 (d0, q0, r0, m0, 'iot_single/stats', ... (56 bytes))
*/

