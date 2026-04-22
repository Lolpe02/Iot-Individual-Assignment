#include "fft_task.h"

#include <arduinoFFT.h>

#include "shared.h"

void TaskFFT(void *pvParameters) {
  (void)pvParameters;

  uint16_t filledBufferCounter = 0;
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

      const float maxAnalysisFreqHz = 2000.0f;
      const int minBin = 1;
      int maxBin = (int)((maxAnalysisFreqHz * (float)SAMPLES) / (float)samplingFreq);
      if (maxBin > (SAMPLES / 2) - 1) {
        maxBin = (SAMPLES / 2) - 1;
      }
      if (maxBin < minBin) {
        maxBin = (SAMPLES / 2) - 1;
      }

      int topBin = minBin;
      float peakMag = procReal[minBin];
      float noiseAcc = 0.0f;
      int noiseCount = 0;
      for (int i = minBin; i <= maxBin; i++) {
        float mag = procReal[i];
        noiseAcc += mag;
        noiseCount++;
        if (mag > peakMag) {
          peakMag = mag;
          topBin = i;
        }
      }

      float noiseAvg = (noiseCount > 0) ? (noiseAcc / (float)noiseCount) : 0.0f;
      if (peakMag < (noiseAvg * 3.0f)) {
        topBin = 0;
      }

      xSemaphoreGive(xFFTFinished);

      float f_max = (topBin * (float)samplingFreq) / (float)SAMPLES;
      optimizedFreq = (uint32_t)(f_max * 2.6f);
      Serial.printf(">Detected Max Freq (Hz): %.2f\r\n", f_max);

      BlockTiming timingInfo = {
          .blockNumber = filledBufferCounter++,
          .start_timestamp = timerstart,
          .end_timestamp = esp_timer_get_time()};
      xQueueSend(fftTimestampsQueue, &timingInfo, 10);
    }
  }
}
