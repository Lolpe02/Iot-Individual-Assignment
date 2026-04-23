#include "fft_task.h"

#include <arduinoFFT.h>

#include "shared.h"

static uint16_t filledBufferCounter = 0;
void TaskFFT(void *pvParameters) { //1–3ms
  (void)pvParameters;

  constexpr float kNyquistMargin = 2.6f;
  constexpr float kStepUp = 1.08f;
  constexpr float kStepDown = 0.92f;
  constexpr float kMinStepHz = 1.0f;
  constexpr float kMaxFs = 30000.0f;

  Serial.println("Starting FFT task...");
  while (1) {
    if (xSemaphoreTake(xFilterReady, portMAX_DELAY) == pdTRUE) {
      int64_t timerStart = esp_timer_get_time();
      uint32_t fsThisBlock = samplingFrequencyHzAssociatedWithCurrentPipelineBlock;
      if (fsThisBlock == 0) fsThisBlock = 1;

      // Detrend
      float mean = 0.0f;
      for (int i = 0; i < SAMPLES; i++) mean += procReal[i];
      mean /= SAMPLES;
      for (int i = 0; i < SAMPLES; i++) procReal[i] -= mean;

      // FFT
      ArduinoFFT<float> FFT = ArduinoFFT<float>(procReal, procImag, SAMPLES, fsThisBlock);
      FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
      FFT.compute(FFT_FORWARD);
      FFT.complexToMagnitude();

      // Trova la magnitudine massima
      const int minBin = 1;
      const int maxBin = (SAMPLES / 2) - 1;
      float peakMag = 0.0f;
      for (int i = minBin; i <= maxBin; i++) {
        if (procReal[i] > peakMag) peakMag = procReal[i];
      }

      // Frequenza più alta con ampiezza >= 70% del picco
      const float kThreshold = 0.7f;
      int topBin = 0;
      for (int i = maxBin; i >= minBin; i--) {
        if (procReal[i] >= peakMag * kThreshold) {
          topBin = i;
          break;
        }
      }
      float f_max = (topBin * (float)fsThisBlock) / (float)SAMPLES;
      float targetFreq = f_max * kNyquistMargin;
  
      Serial.printf("Found_freq=%.1fHz\r\n", f_max);
      // --- LOGICA DI OTTIMIZZAZIONE PROTETTA ---
      if (SELF_OPTIMIZING) {
        uint32_t nextFreq = fsThisBlock;

        // Se il picco è troppo vicino allo zero (Bin 1), la risoluzione è pessima
        // o siamo troppo veloci. Proviamo a scendere ma con cautela.
        if (topBin <= 2) {
            // Se siamo a 10kHz e il segnale è nel bin 1, scendiamo drasticamente
            if (fsThisBlock > 500) nextFreq = fsThisBlock / 2;
            else nextFreq = kMinStepHz;
        } else {
            // Calcoliamo la frequenza ideale basata su Nyquist
            float idealFs = f_max * kNyquistMargin;
            nextFreq = (uint32_t)(idealFs + 0.5f);
        }

        // --- APPLICA I LIMITI DI SICUREZZA ---
        if (nextFreq < kMinStepHz) nextFreq = kMinStepHz;
        if (nextFreq > kMaxFs) nextFreq = kMaxFs;

        // Applica il cambio solo se la variazione è significativa (> 15%)
        // per evitare che il sistema "balli" continuamente (jitter)
        float changeRatio = (float)nextFreq / (float)fsThisBlock;
        if (changeRatio < 0.85f || changeRatio > 1.15f) {
            requestedSamplingFrequencyHzForNextAcquisitionBlock = nextFreq;
            Serial.printf("Next_freq: %uHz\r\n", nextFreq);
        }
      }

      xSemaphoreGive(xFFTFinished);

      //BlockTiming timingInfo = {.blockNumber = filledBufferCounter++,.start_timestamp = timerStart,.end_timestamp = esp_timer_get_time()};
      //xQueueSend(fftTimestampsQueue, &timingInfo, 10);
      PRINT_TIMING(TASK_FFT, filledBufferCounter++, timerStart, esp_timer_get_time());
    }
  }
}
