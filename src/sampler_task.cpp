#include "sampler_task.h"
#include <driver/adc.h>
#include <driver/i2s.h>
#include "shared.h"
#include "config.h"

#if defined(BOARD_HELTEC)
  #define ADC_PIN 4 // GPIO 4 per Heltec
  
  void setupI2S(uint32_t sampleRate) {
    pinMode(ADC_PIN, INPUT);
    analogReadResolution(12); // Forza 12 bit (0-4095)
    Serial.println("ADC Manual Sampling initialized for Heltec");
  }
#else
  void setupI2S(uint32_t sampleRate) {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
        .sample_rate = sampleRate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 2,
        .dma_buf_len = SAMPLES};
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr);
    i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_4);
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_12);
    i2s_adc_enable(I2S_NUM_0);
  }
#endif

void updateADCFrequency(uint32_t sampleRate) {
  #if !defined(BOARD_HELTEC)
    // 1. Ferma l'hardware per evitare che nuovi dati entrino nei buffer DMA
    i2s_stop(I2S_NUM_0);
    i2s_set_clk(I2S_NUM_0, sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
    // 3. SVUOTA I BUFFER RESIDUI (Flush)
    i2s_zero_dma_buffer(I2S_NUM_0);

    i2s_start(I2S_NUM_0);
    
    static uint16_t discard[SAMPLES];
    size_t dummy = 0;
    // Leggi una volta per "pulire i tubi"
    i2s_read(I2S_NUM_0, discard, sizeof(discard), &dummy, pdMS_TO_TICKS(50));
    
    Serial.printf("[I2S] Frequency updated to %d Hz and buffer flushed\n", sampleRate);
  #endif
  // Per Heltec non serve fare nulla qui, la frequenza è gestita nel loop del Task
}

static uint16_t filledBufferCounter = 0;


void TaskReadADC(void *pvParameters) {
  (void)pvParameters;
  // busy wait 0.2 secondo per permettere al logger di partire
  //vTaskDelay(pdMS_TO_TICKS(200));
  
  uint32_t appliedSamplingFreq = requestedSamplingFrequencyHzForNextAcquisitionBlock;

  #if !defined(BOARD_HELTEC)
    static uint16_t i2s_raw_buffer[SAMPLES];
  #endif

  Serial.println("Starting ADC read task...");
  
  while (1) {

    int64_t timerstart = esp_timer_get_time();
    size_t bytes_read = 0;

    if (SELF_OPTIMIZING) {
      uint32_t desiredSamplingFreq = requestedSamplingFrequencyHzForNextAcquisitionBlock;
      if (desiredSamplingFreq > 0 && desiredSamplingFreq != appliedSamplingFreq) {
        updateADCFrequency(desiredSamplingFreq);
        appliedSamplingFreq = desiredSamplingFreq;
        Serial.printf("Sampling frequency updated to %d Hz\n", appliedSamplingFreq);
      }
    }
    #if defined(BOARD_HELTEC)
      // --- CAMPIONAMENTO MANUALE TEMPORIZZATO PER HELTEC ---
      uint32_t microsecondsPerSample = 1000000 / appliedSamplingFreq;
      uint32_t nextSampleTime = micros();

      for (int i = 0; i < SAMPLES; i++) {
        fillRaw[i] = (float)analogRead(ADC_PIN);
        
        nextSampleTime += microsecondsPerSample;
        uint32_t now = micros();
        if (nextSampleTime > now) {
          delayMicroseconds(nextSampleTime - now);
        }
      }
      bytes_read = SAMPLES * sizeof(uint16_t); // Simuliamo bytes_read per far proseguire la pipeline

    #else
      // --- CAMPIONAMENTO I2S STANDARD ---
      i2s_read(I2S_NUM_0, &i2s_raw_buffer, sizeof(i2s_raw_buffer), &bytes_read, portMAX_DELAY);
      for (int i = 0; i < SAMPLES; i++) {
        fillRaw[i] = (float)(i2s_raw_buffer[i] & 0x0FFF);
      }
    #endif

    // Se abbiamo i campioni, passiamo il buffer al task del Filtro
    if (bytes_read >= SAMPLES * sizeof(uint16_t)) {
      xSemaphoreTake(xFilterFinished, portMAX_DELAY);

      procRaw = fillRaw;
      fillRaw = (fillRaw == vRaw0) ? vRaw1 : vRaw0;
      samplingFrequencyHzAssociatedWithCurrentPipelineBlock = appliedSamplingFreq;
      
      xSemaphoreGive(xSamplingReady);

      //BlockTiming timingInfo = {.blockNumber = filledBufferCounter++,.start_timestamp = timerstart,.end_timestamp = esp_timer_get_time()};
      //xQueueSend(samplingTimestampsQueue, &timingInfo, 10);
      // first block should print 0 but it prints 1 even with  filledBufferCounter++ in the timingInfo struct initializer, so we increment it here to have correct block numbers in the logs
      PRINT_TIMING(TASK_SAMPLER, filledBufferCounter++, timerstart, esp_timer_get_time());
      if (filledBufferCounter == 1) Serial.flush();  // forza flush solo al blocco 0
    }
  }
}