#pragma once

#include <Arduino.h>

constexpr int SAMPLES = 1024;
constexpr bool FILTER_ENABLED = true;
constexpr bool FILTER_ZSCORE_OR_HAMPEL = false;
constexpr int FILTER_WINDOW_SIZE = 15;
constexpr bool SELF_OPTIMIZING = false;
constexpr int SERIAL_PLOTTER_STRIDE = 32;

constexpr char WIFI_SSID[] = "F7";
constexpr char WIFI_PASSWORD[] = "TrallalleroTrallalla";

constexpr char MQTT_SERVER[] = "broker.hivemq.com";
constexpr uint16_t MQTT_PORT = 1883;

constexpr int QUEUE_LENGTH = 100;
