#include <Arduino.h>

#include "communication_task.h"
#include "fft_task.h"
#include "filter_task.h"
#include "sampler_task.h"
#include "shared.h"
#include "timing_task.h"
#include "sig_generator_task.h"

void setup() {
  Serial.begin(115200);
  Serial.println("Starting up...");
  /*
  while (!Serial.available()) {
    delay(10);                      // aspetta qualsiasi byte dal PC
  }
  //Serial.read(); // REMOVE if not logging
  */
  setupI2S(requestedSamplingFrequencyHzForNextAcquisitionBlock);
  setupCommunication();
  initSystemResources();

  BaseType_t ok = pdPASS;
  ok = xTaskCreatePinnedToCore(TaskReadADC, "ReadADC", 5 * 1024, nullptr, 4, &readADCTaskHandle, 1);
  if (ok != pdPASS) {
    Serial.println("[ERR] Failed to create ReadADC task");
  }
  ok = xTaskCreate(TaskFilter, "Filter_Task", 3 * 1024, nullptr, 3, &filterTaskHandle);
  if (ok != pdPASS) {
    Serial.println("[ERR] Failed to create Filter task");
  }
  ok = xTaskCreatePinnedToCore(TaskFFT, "FFT_Task", 10 * 1024, nullptr, 4, &fftTaskHandle, 0);
  if (ok != pdPASS) {
    Serial.println("[ERR] Failed to create FFT task");
  }
  ok = xTaskCreate(TaskCommunication, "Communication_Task", 6 * 1024, nullptr, 2, &communicationTaskHandle);
  if (ok != pdPASS) {
    Serial.println("[ERR] Failed to create Communication task");
  }/*
  ok = xTaskCreate(TaskTimingCsv, "TimingCsv_Task", 1024, nullptr, 1, &timingCsvTaskHandle);
  if (ok != pdPASS) {
    Serial.println("[ERR] Failed to create TimingCsv task");
  }*/
  /*ok = xTaskCreate(TaskSignalGenerator, "Generator_Task", 2 * 1024, nullptr, 3, &generatorTaskHandle);
  if (ok != pdPASS) {
    Serial.println("[ERR] Failed to create Communication task");
  }*/
  Serial.println("Setup complete.");
}

void loop() {
  delay(1);
}
