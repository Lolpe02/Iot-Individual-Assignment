#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "WiFi.h"
#include "PubSubClient.h"
#include "config.h"

struct BlockTiming {
  uint16_t blockNumber;
  int64_t start_timestamp;
  int64_t end_timestamp;
};

struct SignalStats {
  float mean;
  float stdDev;
  float min;
  float max;
};

extern volatile uint32_t samplingFreq;
extern volatile uint32_t optimizedFreq;

extern WiFiClient wifiClient;
extern PubSubClient mqttClient;

extern TaskHandle_t fftTaskHandle;
extern TaskHandle_t readADCTaskHandle;
extern TaskHandle_t filterTaskHandle;
extern TaskHandle_t communicationTaskHandle;
extern TaskHandle_t timingCsvTaskHandle;

extern QueueHandle_t maxFFTQueue;
extern QueueHandle_t communicationTimestampsQueue;
extern QueueHandle_t filterTimestampsQueue;
extern QueueHandle_t fftTimestampsQueue;
extern QueueHandle_t samplingTimestampsQueue;
extern QueueHandle_t statsQueue;

extern SemaphoreHandle_t xSamplingReady;
extern SemaphoreHandle_t xFilterReady;
extern SemaphoreHandle_t xFilterFinished;
extern SemaphoreHandle_t xFFTFinished;
extern SemaphoreHandle_t xCommReady;

extern float vRaw0[SAMPLES];
extern float vRaw1[SAMPLES];
extern float *fillRaw;
extern float *procRaw;

extern float vReal0[SAMPLES];
extern float vImag0[SAMPLES];
extern float vReal1[SAMPLES];
extern float vImag1[SAMPLES];
extern float *fillReal;
extern float *fillImag;
extern float *procReal;
extern float *procImag;

void initSystemResources();
