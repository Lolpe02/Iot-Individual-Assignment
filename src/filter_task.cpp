#include "filter_task.h"

#include <algorithm>
#include <math.h>
#include <string.h>

#include "shared.h"

static void calculate_stats_and_send();
static float median_copy(const float *arr, int size);

static float median_copy(const float *arr, int size) {
  float temp[FILTER_WINDOW_SIZE];
  memcpy(temp, arr, size * sizeof(float));
  std::sort(temp, temp + size);

  if ((size & 1) == 0) {
    return 0.5f * (temp[(size / 2) - 1] + temp[size / 2]);
  }
  return temp[size / 2];
}

void TaskFilter(void *pvParameters) {
  (void)pvParameters;

  static float history[FILTER_WINDOW_SIZE - 1] = {0};
  static bool initialized = false;
  const int windowSize = (FILTER_WINDOW_SIZE < SAMPLES) ? FILTER_WINDOW_SIZE : SAMPLES;
  uint16_t filledBufferCounter = 0;
  const float eps = 1e-6f;
  const int historySize = windowSize - 1;
  const float zThreshold = 2.5f;
  const float hampelThreshold = 3.0f;

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
        if (!initialized) {
          for (int i = 0; i < historySize; i++) {
            history[i] = procRaw[i];
          }
          initialized = true;
        }

        if (FILTER_ZSCORE_OR_HAMPEL) {
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

            if ((i & 0x3F) == 0) {
              taskYIELD();
            }
          }
        } else {
          float windowValues[FILTER_WINDOW_SIZE];
          float deviations[FILTER_WINDOW_SIZE];
          for (int i = 0; i < SAMPLES; i++) {
            for (int j = 0; j < windowSize; j++) {
              int sampleIndex = i - (windowSize - 1) + j;
              if (sampleIndex < 0) {
                windowValues[j] = history[historySize + sampleIndex];
              } else {
                windowValues[j] = procRaw[sampleIndex];
              }
            }

            float median = median_copy(windowValues, windowSize);

            for (int j = 0; j < windowSize; j++) {
              deviations[j] = fabsf(windowValues[j] - median);
            }
            float mad = median_copy(deviations, windowSize);
            float threshold = hampelThreshold * (mad + eps);
            float deviation = fabsf(procRaw[i] - median);
            fillReal[i] = (deviation > threshold) ? median : procRaw[i];
            fillImag[i] = 0.0f;

            if (i % SERIAL_PLOTTER_STRIDE == 0) {
              Serial.printf(">Raw_value: %.2f, Filtered_value: %.2f\r\n", procRaw[i], fillReal[i]);
            }

            if ((i & 0x3F) == 0) {
              taskYIELD();
            }
          }
        }

        for (int i = 0; i < historySize; i++) {
          history[i] = procRaw[SAMPLES - historySize + i];
        }
      }

      calculate_stats_and_send();

      procReal = fillReal;
      procImag = fillImag;
      fillReal = (fillReal == vReal0) ? vReal1 : vReal0;
      fillImag = (fillImag == vImag0) ? vImag1 : vImag0;

      xSemaphoreGive(xFilterFinished);
      xSemaphoreGive(xFilterReady);

      BlockTiming timingInfo = {
          .blockNumber = filledBufferCounter++,
          .start_timestamp = timerstart,
          .end_timestamp = esp_timer_get_time()};
      xQueueSend(filterTimestampsQueue, &timingInfo, 10);

      // Let lower-priority/system tasks (including IDLE/WDT housekeeping) run.
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

static void calculate_stats_and_send() {
  SignalStats stats;
  stats.mean = 0.0f;
  stats.stdDev = 0.0f;
  stats.min = fillReal[0];
  stats.max = fillReal[0];

  for (int i = 0; i < SAMPLES; i++) {
    float val = fillReal[i];
    stats.mean += val;
    if (val < stats.min) {
      stats.min = val;
    }
    if (val > stats.max) {
      stats.max = val;
    }
  }
  stats.mean /= (float)SAMPLES;

  for (int i = 0; i < SAMPLES; i++) {
    float val = fillReal[i];
    stats.stdDev += (val - stats.mean) * (val - stats.mean);
  }
  stats.stdDev = sqrtf(stats.stdDev / (float)SAMPLES);

  xQueueOverwrite(statsQueue, &stats);
}