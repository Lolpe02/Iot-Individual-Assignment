#pragma once

#include <Arduino.h>

// Copy this file to include/secrets.h and fill with your real values.

// WiFi and MQTT configuration
constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

constexpr char MQTT_SERVER[] = "YOUR_MQTT_BROKER_IP_OR_HOST";
constexpr uint16_t MQTT_PORT = 1883;

// LoRa ABP configuration
constexpr uint32_t devAddr = 0x00000000;
constexpr uint8_t appSKey[16] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
constexpr uint8_t nwkSKey[16] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
