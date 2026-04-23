#include "shared.h"

volatile uint32_t requestedSamplingFrequencyHzForNextAcquisitionBlock = 1000;
volatile uint32_t samplingFrequencyHzAssociatedWithCurrentPipelineBlock = 1000;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

TaskHandle_t fftTaskHandle = nullptr;
TaskHandle_t readADCTaskHandle = nullptr;
TaskHandle_t filterTaskHandle = nullptr;
TaskHandle_t communicationTaskHandle = nullptr;
TaskHandle_t timingCsvTaskHandle = nullptr;
TaskHandle_t generatorTaskHandle = nullptr;

QueueHandle_t maxFFTQueue = nullptr;
QueueHandle_t communicationTimestampsQueue = nullptr;
QueueHandle_t filterTimestampsQueue = nullptr;
QueueHandle_t fftTimestampsQueue = nullptr;
QueueHandle_t samplingTimestampsQueue = nullptr;
QueueHandle_t statsQueue = nullptr;

SemaphoreHandle_t xSamplingReady = nullptr;
SemaphoreHandle_t xFilterReady = nullptr;
SemaphoreHandle_t xFilterFinished = nullptr;
SemaphoreHandle_t xFFTFinished = nullptr;
SemaphoreHandle_t xCommReady = nullptr;

float vRaw0[SAMPLES];
float vRaw1[SAMPLES];
float *fillRaw = vRaw0;
float *procRaw = vRaw1;

float vReal0[SAMPLES], vImag0[SAMPLES];
float vReal1[SAMPLES], vImag1[SAMPLES];
float *fillReal = vReal0;
float *fillImag = vImag0;
float *procReal = vReal1;
float *procImag = vImag1;

void initSystemResources() {
  xSamplingReady = xSemaphoreCreateBinary();
  xFilterReady = xSemaphoreCreateBinary();
  xFilterFinished = xSemaphoreCreateBinary();
  xFFTFinished = xSemaphoreCreateBinary();
  xCommReady = xSemaphoreCreateBinary();

  // Initial handshake so the first producer block does not stall.
  xSemaphoreGive(xFFTFinished);
  xSemaphoreGive(xFilterFinished);

  maxFFTQueue = xQueueCreate(QUEUE_LENGTH, sizeof(int));
  samplingTimestampsQueue = xQueueCreate(QUEUE_LENGTH, sizeof(BlockTiming));
  filterTimestampsQueue = xQueueCreate(QUEUE_LENGTH, sizeof(BlockTiming));
  fftTimestampsQueue = xQueueCreate(QUEUE_LENGTH, sizeof(BlockTiming));
  communicationTimestampsQueue = xQueueCreate(QUEUE_LENGTH, sizeof(BlockTiming));
  statsQueue = xQueueCreate(1, sizeof(SignalStats));
}
