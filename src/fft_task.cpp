#include "fft_task.h"

#include <arduinoFFT.h>

#include "shared.h"

void TaskFFT(void *pvParameters) {
  (void)pvParameters;

  uint8_t filledBufferCounter = 0;
  Serial.println("Starting FFT task...");
  while (1) {
    if (xSemaphoreTake(xFilterReady, portMAX_DELAY) == pdTRUE) {
      int64_t timerstart = esp_timer_get_time();

      float mean = 0.0f;
      for (int i = 0; i < SAMPLES; i++) {
        mean += procReal[i];
      }
      mean /= SAMPLES;
      for (int i = 0; i < SAMPLES; i++) {
        procReal[i] -= mean;
      }

      ArduinoFFT<float> FFT = ArduinoFFT<float>(procReal, procImag, SAMPLES, samplingFreq);
      FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
      FFT.compute(FFT_FORWARD);
      FFT.complexToMagnitude();

      float threshold = 500.0f;
      int topBin = 0;
      for (int i = (SAMPLES / 2) - 1; i >= 0; i--) {
        if (procReal[i] > threshold) {
          topBin = i;
          break;
        }
      }

      xSemaphoreGive(xFFTFinished);

      float f_max = (topBin * (float)samplingFreq) / (float)SAMPLES;
      optimizedFreq = (uint16_t)(f_max * 2.6f);
      Serial.printf(">Detected Max Freq (Hz): %.2f\r\n", f_max);

      BlockTiming timingInfo = {
          .blockNumber = filledBufferCounter++,
          .start_timestamp = timerstart,
          .end_timestamp = esp_timer_get_time()};
      xQueueSend(fftTimestampsQueue, &timingInfo, 10);
    }
  }
}
