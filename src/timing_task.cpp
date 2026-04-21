#include "timing_task.h"

#include "shared.h"

void TaskTimingCsv(void *pvParameters) {
  (void)pvParameters;

  BlockTiming timingInfo;
  bool headerPrinted = false;

  Serial.println("Starting Timing CSV task...");
  while (1) {
    if (xQueueReceive(samplingTimestampsQueue, &timingInfo, 0) == pdTRUE && headerPrinted) {
      Serial.printf("sampling,%u,%lld,%lld,%lld\n",
                    timingInfo.blockNumber,
                    (long long)timingInfo.start_timestamp,
                    (long long)timingInfo.end_timestamp,
                    (long long)(timingInfo.end_timestamp - timingInfo.start_timestamp));
    }

    if (xQueueReceive(filterTimestampsQueue, &timingInfo, 0) == pdTRUE && headerPrinted) {
      Serial.printf("filter,%u,%lld,%lld,%lld\n",
                    timingInfo.blockNumber,
                    (long long)timingInfo.start_timestamp,
                    (long long)timingInfo.end_timestamp,
                    (long long)(timingInfo.end_timestamp - timingInfo.start_timestamp));
    }

    if (xQueueReceive(fftTimestampsQueue, &timingInfo, 0) == pdTRUE && headerPrinted) {
      Serial.printf("fft,%u,%lld,%lld,%lld\n",
                    timingInfo.blockNumber,
                    (long long)timingInfo.start_timestamp,
                    (long long)timingInfo.end_timestamp,
                    (long long)(timingInfo.end_timestamp - timingInfo.start_timestamp));
    }

    if (xQueueReceive(communicationTimestampsQueue, &timingInfo, 0) == pdTRUE && headerPrinted) {
      Serial.printf("comm,%u,%lld,%lld,%lld\n",
                    timingInfo.blockNumber,
                    (long long)timingInfo.start_timestamp,
                    (long long)timingInfo.end_timestamp,
                    (long long)(timingInfo.end_timestamp - timingInfo.start_timestamp));
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
