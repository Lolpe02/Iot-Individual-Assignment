#include "sampler_task.h"

#include <driver/adc.h>
#include <driver/i2s.h>

#include "shared.h"

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

void TaskReadADC(void *pvParameters) {
  (void)pvParameters;

  static uint16_t i2s_raw_buffer[SAMPLES];
  size_t bytes_read = 0;
  uint16_t filledBufferCounter = 0;

  Serial.println("Starting ADC read task...");
  while (1) {
    int64_t timerstart = esp_timer_get_time();

    i2s_read(I2S_NUM_0, &i2s_raw_buffer, sizeof(i2s_raw_buffer), &bytes_read, portMAX_DELAY);

    for (int i = 0; i < SAMPLES; i++) {
      i2s_raw_buffer[i] &= 0x0FFF;
      fillRaw[i] = (float)i2s_raw_buffer[i];
    }

    if (bytes_read >= SAMPLES * sizeof(uint16_t)) {
      xSemaphoreTake(xFilterFinished, portMAX_DELAY);

      procRaw = fillRaw;
      fillRaw = (fillRaw == vRaw0) ? vRaw1 : vRaw0;

      if (SELF_OPTIMIZING && optimizedFreq > 0 && optimizedFreq != samplingFreq) {
        uint16_t bottom_range = (uint16_t)((float)samplingFreq * 0.90f);
        uint16_t top_range = (uint16_t)((float)samplingFreq * 1.10f);
        bool tooSimilar = (optimizedFreq > bottom_range) && (optimizedFreq < top_range);
        bool inValidRange = (optimizedFreq >= 0.005f) && (optimizedFreq <= 50000);

        if (inValidRange && !tooSimilar) {
          Serial.printf(
              "Optimized frequency %.2f Hz is too close or too far from current %.2f Hz, "
              "ignoring...\n",
              (float)optimizedFreq,
              (float)samplingFreq);
          samplingFreq = optimizedFreq;
          i2s_stop(I2S_NUM_0);
          i2s_set_clk(I2S_NUM_0, samplingFreq, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
          i2s_start(I2S_NUM_0);
          Serial.printf("Sampling frequency updated to %.2f Hz\n", (float)samplingFreq);
        }
      }

      xSemaphoreGive(xSamplingReady);

      BlockTiming timingInfo = {
          .blockNumber = filledBufferCounter++,
          .start_timestamp = timerstart,
          .end_timestamp = esp_timer_get_time()};
      xQueueSend(samplingTimestampsQueue, &timingInfo, 10);
    }
  }
}
