#include "filter_task.h"

#include <math.h>

#include "shared.h"

void TaskFilter(void *pvParameters) {
  (void)pvParameters;

  static float history[FILTER_WINDOW_SIZE - 1] = {0};
  static bool initialized = false;
  const int windowSize = (FILTER_WINDOW_SIZE < SAMPLES) ? FILTER_WINDOW_SIZE : SAMPLES;
  uint8_t filledBufferCounter = 0;

  Serial.println("Starting Filter task...");
  while (1) {
    int64_t timerstart = esp_timer_get_time();
    bool ready1 = xSemaphoreTake(xSamplingReady, portMAX_DELAY);
    bool ready2 = xSemaphoreTake(xFFTFinished, portMAX_DELAY);

    if (ready1 == pdTRUE && ready2 == pdTRUE) {
      if (!FILTER_ENABLED) {
        for (int i = 0; i < SAMPLES; i++) {
          fillReal[i] = procRaw[i];
          fillImag[i] = 0.0f;
        }
      } else {
        const float zThreshold = 3.0f;
        const float eps = 1e-6f;
        const int historySize = windowSize - 1;

        if (!initialized) {
          for (int i = 0; i < historySize; i++) {
            history[i] = procRaw[i];
          }
          initialized = true;
        }

        float runningSum = 0.0f;
        float runningSumSq = 0.0f;
        for (int i = 0; i < historySize; i++) {
          float h = history[i];
          runningSum += h;
          runningSumSq += h * h;
        }

        for (int i = 0; i < SAMPLES; i++) {
          float newSample = procRaw[i];
          runningSum += newSample;
          runningSumSq += newSample * newSample;

          float oldSample;
          if (i == 0) {
            oldSample = 0.0f;
          } else if (i < windowSize) {
            oldSample = history[i - 1];
          } else {
            oldSample = procRaw[i - windowSize];
          }

          if (i > 0) {
            runningSum -= oldSample;
            runningSumSq -= oldSample * oldSample;
          }

          float mean = runningSum / (float)windowSize;
          float ex2 = runningSumSq / (float)windowSize;
          float variance = ex2 - (mean * mean);
          if (variance < 0.0f) {
            variance = 0.0f;
          }

          float stdDev = sqrtf(variance + eps);
          float z = (newSample - mean) / stdDev;

          fillReal[i] = (fabsf(z) > zThreshold) ? mean : newSample;
          fillImag[i] = 0.0f;

          if (i % SERIAL_PLOTTER_STRIDE == 0) {
            Serial.printf(">Raw_value: %.2f, Filtered_value: %.2f\r\n", procRaw[i], fillReal[i]);
          }
        }

        for (int i = 0; i < historySize; i++) {
          history[i] = procRaw[SAMPLES - historySize + i];
        }
      }

      procReal = fillReal;
      procImag = fillImag;
      fillReal = (fillReal == vReal0) ? vReal1 : vReal0;
      fillImag = (fillImag == vImag0) ? vImag1 : vImag0;

      xSemaphoreGive(xFilterFinished);
      xSemaphoreGive(xFilterReady);
      xSemaphoreGive(xCommReady);

      BlockTiming timingInfo = {
          .blockNumber = filledBufferCounter++,
          .start_timestamp = timerstart,
          .end_timestamp = esp_timer_get_time()};
      xQueueSend(filterTimestampsQueue, &timingInfo, 10);
    }
  }
}
