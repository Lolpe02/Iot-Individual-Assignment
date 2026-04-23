#pragma once

#include <Arduino.h>
#include "secrets.h"

// Task IDs
#define TASK_SAMPLER 0
#define TASK_FILTER  1
#define TASK_FFT     2
#define TASK_COMM    3
#define USE_LORA false // true = LoRa, false = MQTT

// Macro unica per tutti i task
#define PRINT_TIMING(taskId, blockNum, startUs, endUs) \
  Serial.printf("%d,%u,%lld,%lld\n", (taskId), (uint32_t)(blockNum), (int64_t)(startUs), (int64_t)(endUs))
constexpr int SAMPLES = 1024;
constexpr bool FILTER_ENABLED = true;
constexpr bool FILTER_ZSCORE_OR_HAMPEL = true; // true = z-score, false = Hampel
constexpr int FILTER_WINDOW_SIZE = 25;
constexpr bool SELF_OPTIMIZING = true;
constexpr uint32_t SERIAL_PLOTTER_HZ = 180;
constexpr int QUEUE_LENGTH = 100;
