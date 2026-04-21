#include <Arduino.h>

#include "communication_task.h"
#include "fft_task.h"
#include "filter_task.h"
#include "sampler_task.h"
#include "shared.h"
#include "timing_task.h"

void setup() {
  Serial.begin(115200);
  Serial.println("Starting up...");

  setupI2S(samplingFreq);
  setupCommunication();
  initSystemResources();

  xTaskCreatePinnedToCore(TaskReadADC, "ReadADC", 3 * 1024, nullptr, 1, &readADCTaskHandle, 1);
  xTaskCreatePinnedToCore(TaskFFT, "FFT_Task", 10 * 1024, nullptr, 1, &fftTaskHandle, 0);
  xTaskCreate(TaskFilter, "Filter_Task", 3 * 1024, nullptr, 1, &filterTaskHandle);
  xTaskCreate(TaskCommunication,
              "Communication_Task",
              6 * 1024,
              nullptr,
              1,
              &communicationTaskHandle);
  xTaskCreate(TaskTimingCsv, "TimingCsv_Task", 1024, nullptr, 1, &timingCsvTaskHandle);

  Serial.println("Setup complete.");
}

void loop() {}
