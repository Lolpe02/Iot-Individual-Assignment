#include <Arduino.h>
#include <arduinoFFT.h>
#include <driver/adc.h>
#include <driver/i2s.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "WiFi.h"
#include "PubSubClient.h"


#define SAMPLES 1024
#define FILTER true
#define FILTER_ZSCORE_OR_HAMPEL false
#define FILTER_WINDOW_SIZE 15
#define SELF_OPTIMIZING false
#define WiFi_SSID "F7"
#define WiFi_PASSWORD "TrallalleroTrallalla"
volatile uint32_t samplingFreq = 10000;
volatile uint16_t optimizedFreq = 0;

// archetypes
void TaskReadADC(void *pvParameters);
TaskHandle_t fftTaskHandle;
void TaskFFT(void *pvParameters);
TaskHandle_t readADCTaskHandle;
void TaskFilter(void *pvParameters);
TaskHandle_t filterTaskHandle;
void TaskCommunication(void *pvParameters);
TaskHandle_t communicationTaskHandle;
void TaskTimingCsv(void *pvParameters);
TaskHandle_t timingCsvTaskHandle;

// Handle for our Queues
QueueHandle_t maxFFTQueue;

QueueHandle_t communicationTimestampsQueue;
QueueHandle_t filterTimestampsQueue;
QueueHandle_t fftTimestampsQueue;
QueueHandle_t samplingTimestampsQueue;

SemaphoreHandle_t xSamplingReady;
SemaphoreHandle_t xFilterReady;
SemaphoreHandle_t xFilterFinished;
SemaphoreHandle_t xFFTFinished;
SemaphoreHandle_t xCommReady;

struct BlockTiming {
  uint8_t blockNumber;
  uint32_t start_timestamp;
  uint32_t end_timestamp;
};

// Double buffers for raw ADC data for filtering
float vRaw0[SAMPLES];
float vRaw1[SAMPLES];
// Control pointers
float *fillRaw = vRaw0;
float *procRaw = vRaw1;

// Double buffers for FFT processing
float vReal0[SAMPLES], vImag0[SAMPLES];
float vReal1[SAMPLES], vImag1[SAMPLES];
// Control pointers
float *fillReal = vReal0;
float *fillImag = vImag0;
float *procReal = vReal1;
float *procImag = vImag1;

void setup_i2s(uint32_t samplingFreq) {
  i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
        .sample_rate = samplingFreq, // 20kHz iniziale
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 2,
        .dma_buf_len = SAMPLES
  };
  // ... configurazione i2s_config_t come visto prima ...
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  // 1. Imposta l'unità ADC e il canale (es. GPIO 32 -> ADC1_CH4)
  i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_4);
  // 2. Imposta l'attenuazione a 11dB per il canale specifico
  // Questo permette di leggere fino a ~3.3V
  adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_12);
  // 3. Abilita l'ADC per I2S
  i2s_adc_enable(I2S_NUM_0);
}

void setupWiFi() {
  WiFi.begin(WiFi_SSID, WiFi_PASSWORD);
  Serial.print("[WiFi] Connecting to %s", WiFi_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected!, IP address: %s", WiFi.localIP().toString().c_str());
}

void setupMQTT() {
  mqttClient.setServer(mqtt_server, mqtt_port);
  reconnectMQTT();
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Attempting connection...");
    if (mqttClient.connect("ESP32Client")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      delay(5000);
    }
  }
}

void publishMQTT(const char* topic, const char* payload) {
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();
  
  char message[256];
  snprintf(message, sizeof(message), "%s", payload);
  mqttClient.publish(topic, message);
}

void setup() {
  Serial.begin(115200);
  // set adc pin 32 as input
  pinMode(32, INPUT);
  // activate i2s
  setup_i2s(samplingFreq);


  xSamplingReady = xSemaphoreCreateBinary();
  xFilterReady = xSemaphoreCreateBinary();
  xFilterFinished = xSemaphoreCreateBinary();
  xFFTFinished = xSemaphoreCreateBinary();
  xCommReady = xSemaphoreCreateBinary();

  xSemaphoreGive(xFFTFinished);  // Give the FFT semaphore initially so the Sampler can do the first swap without waiting
  xSemaphoreGive(xFilterFinished); // Give the Filter semaphore initially so the Sampler can do the first swap without waiting

  // Create a queue capable of containing 100 integers.
  maxFFTQueue = xQueueCreate(100, sizeof(int));
  samplingTimestampsQueue = xQueueCreate(100, sizeof(BlockTiming));
  filterTimestampsQueue = xQueueCreate(100, sizeof(BlockTiming));
  fftTimestampsQueue = xQueueCreate(100, sizeof(BlockTiming));
  communicationTimestampsQueue = xQueueCreate(100, sizeof(BlockTiming));

  


  xTaskCreatePinnedToCore(TaskReadADC, "ReadADC", 2048, NULL, 1, &readADCTaskHandle, 1);
  xTaskCreatePinnedToCore(TaskFFT, "FFT_Task", 10*1024, NULL, 1, &fftTaskHandle, 0);
  xTaskCreate(TaskFilter, "Filter_Task", 2048, NULL, 1, &filterTaskHandle);
  xTaskCreate(TaskCommunication, "Communication_Task", 1024, NULL, 1, &communicationTaskHandle);
  xTaskCreate(TaskTimingCsv, "TimingCsv_Task", 4096, NULL, 1, &timingCsvTaskHandle);
}

void loop() {
}

void TaskReadADC(void *pvParameters) {
  uint16_t i2s_raw_buffer[SAMPLES]; // we'll try 2048 later
  size_t bytes_read = 0;
  uint8_t filledBufferCounter = 0;
  while (1) {
    auto timerstart = esp_timer_get_time();

    // read all 1024 samples from I2S
    i2s_read(I2S_NUM_0, 
                 &i2s_raw_buffer, 
                 sizeof(i2s_raw_buffer), 
                 &bytes_read, 
                 portMAX_DELAY);

    // cast typeless buffer to float (16-bits) inside procReal
    for (int i = 0; i < SAMPLES; i++) {
      i2s_raw_buffer[i] &= 0x0FFF; // Mask per 12-bit ADC or (i2s_raw_buffer[i] >> 4) & 0x0FFF;
      fillRaw[i] = (float)i2s_raw_buffer[i];
    }
    if (bytes_read >= SAMPLES*sizeof(uint16_t)) {
      
      xSemaphoreTake(xFilterFinished, portMAX_DELAY);
      
      procRaw = fillRaw; 
      fillRaw = (fillRaw == vRaw0) ? vRaw1 : vRaw0;
      
      // change haardware sampling frequency if optimizedFreq is set by FFT
      if (SELF_OPTIMIZING && optimizedFreq > 0 && optimizedFreq != samplingFreq) {
        uint16_t bottom_range = samplingFreq * 0.90;
        uint16_t top_range = samplingFreq * 1.10;
        // if it's too similar ignore it, if it's too different cap it to avoid crazy values
        bool tooSimilar = (optimizedFreq > bottom_range) && (optimizedFreq < top_range);
        bool inValidRange = (optimizedFreq >= 0.005) && (optimizedFreq <= 50000);
        if (inValidRange && !tooSimilar) {
          Serial.printf("Optimized frequency %.2f Hz is too close or too far from current %.2f Hz, ignoring...\n", (float)optimizedFreq, (float)samplingFreq);
          samplingFreq = optimizedFreq;
          i2s_stop(I2S_NUM_0);
          i2s_set_clk(I2S_NUM_0, samplingFreq, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
          i2s_start(I2S_NUM_0);
          Serial.printf("Sampling frequency updated to %.2f Hz\n", (float)samplingFreq);
        }  
      }

      xSemaphoreGive(xSamplingReady); // Tell Filter:
    
      BlockTiming timingInfo = {
        .blockNumber = filledBufferCounter++,
        .start_timestamp = timerstart,
        .end_timestamp = esp_timer_get_time()
      };
      xQueueSend(samplingTimestampsQueue, &timingInfo, 10);      
    }
  }
}


void TaskFFT(void *pvParameters) {
  uint8_t filledBufferCounter = 0;
  while (1) {
    if (xSemaphoreTake(xFilterReady, portMAX_DELAY) == pdTRUE) {
      auto timerstart = esp_timer_get_time();

      // --- STEP 1: Remove DC Offset (Crucial to kill that 4Hz peak) ---
      float mean = 0;
      for(int i=0; i<SAMPLES; i++) mean += procReal[i];
      mean /= SAMPLES;
      for(int i=0; i<SAMPLES; i++) procReal[i] -= mean;

      // Re-link the library to the newly swapped proc pointers
      ArduinoFFT<float> FFT = ArduinoFFT<float>(procReal, procImag, SAMPLES, samplingFreq);

      FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
      FFT.compute(FFT_FORWARD);
      FFT.complexToMagnitude();

      // Find Max Frequency (Shannon check)
      float threshold = 500.0; // Scaled: Peak * (SAMPLES/something)
      int topBin = 0;
      for (int i = (SAMPLES / 2) - 1; i >= 0; i--) {
        if (procReal[i] > threshold) {
          topBin = i;
          break; 
        }
      }
      xSemaphoreGive(xFFTFinished);
      float f_max = (topBin * (float)samplingFreq) / (float)SAMPLES;
      // Suggest a new sampling frequency based on the detected max frequency
      optimizedFreq = f_max * 2.6; // 2.5 is a safety margin above Nyquist
      Serial.printf("Detected Max Freq: %.2f Hz | New Suggested Fs: %.2f Hz\n", f_max, f_max * 2.5);
      BlockTiming timingInfo = {
        .blockNumber = filledBufferCounter++,
        .start_timestamp = timerstart,
        .end_timestamp = esp_timer_get_time()
      };
      // wait just a little then drop
      xQueueSend(fftTimestampsQueue, &timingInfo, 10);
      }
    }  
}

void TaskFilter(void *pvParameters) {
  static float history[FILTER_WINDOW_SIZE-1] = {0};
  static bool initialized = false;
  const int windowSize = (FILTER_WINDOW_SIZE < SAMPLES) ? FILTER_WINDOW_SIZE : SAMPLES;
  uint8_t filledBufferCounter = 0;
  while (1) {
    auto timerstart = esp_timer_get_time();
    bool ready1 = xSemaphoreTake(xSamplingReady, portMAX_DELAY);
    bool ready2 = xSemaphoreTake(xFFTFinished, portMAX_DELAY);
    if (ready1 == pdTRUE && ready2 == pdTRUE) {
      // no filter
      if (!FILTER) {
        for (int i = 0; i < SAMPLES; i++) {
          fillReal[i] = procRaw[i]; // No filter, just copy
          fillImag[i] = 0.0f; // Keep imaginary part zero for FFT
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

        float runningSum = 0.0;
        float runningSumSq = 0.0;
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
          // var = E[(x - mean)^2] = E[x^2] - mean^2
          // This is the expanded form of (x - b)^2 = x^2 - 2bx + b^2,
          // averaged over the window, so we avoid a second pass over the samples.
          float variance = ex2 - (mean * mean);
          if (variance < 0.0f) variance = 0.0f;
          float stdDev = sqrtf(variance + eps);
          float z = (newSample - mean) / stdDev;

          if (fabsf(z) > zThreshold) {
            fillReal[i] = mean;
          } else {
            fillReal[i] = newSample;
          }
          fillImag[i] = 0.0f; // Keep imaginary part zero for FFT
        }

        for (int i = 0; i < historySize; i++) {
          history[i] = procRaw[SAMPLES - historySize + i];
        }
      }

      procReal = fillReal; procImag = fillImag;
      fillReal = (fillReal == vReal0) ? vReal1 : vReal0;
      fillImag = (fillImag == vImag0) ? vImag1 : vImag0;
      xSemaphoreGive(xFilterFinished); // Tell Sampler
      xSemaphoreGive(xFilterReady); // Tell FFT
      xSemaphoreGive(xCommReady); // Tell Communication Task

      BlockTiming timingInfo = {
        .blockNumber = filledBufferCounter++,
        .start_timestamp = timerstart,
        .end_timestamp = esp_timer_get_time()
      };
      xQueueSend(filterTimestampsQueue, &timingInfo, 10);
    }
  }
}

void TaskCommunication(void *pvParameters) {
  uint8_t filledBufferCounter = 0;
  while (1) {
    // we just read, we dont want to block the FFT task, so we check if the semaphore is available without waiting

    if (xSemaphoreTake(xCommReady, portMAX_DELAY) == pdTRUE) {
      auto timerstart = esp_timer_get_time();
      // copy procReal to a local buffer to avoid holding the semaphore while printing
      float localProcReal[SAMPLES];
      for (int i = 0; i < SAMPLES; i++) {
        localProcReal[i] = procReal[i];
      }
      xSemaphoreGive(xCommReady); // release immediately after copying

      // calculate random stuff from procReal to simulate some processing and have something to print 
      float max = 0;
      float min = 1000000;
      float sum = 0;

      for (int i = 0; i < SAMPLES; i++) {
        sum += localProcReal[i];
        if (localProcReal[i] > max) max = localProcReal[i];
        if (localProcReal[i] < min) min = localProcReal[i];
      }
      // calculate mean & standard deviation
      float mean = sum / (float)SAMPLES;
      float sumSq = 0;
      for (int i = 0; i < SAMPLES; i++) {
        float diff = localProcReal[i] - mean;
        sumSq += diff * diff;
      }
      float stdDev = sqrt(sumSq / (float)SAMPLES);
      
      BlockTiming timingInfo = {
        .blockNumber = filledBufferCounter++,
        .start_timestamp = timerstart,
        .end_timestamp = esp_timer_get_time()
       };
      xQueueSend(communicationTimestampsQueue, &timingInfo, 10);
      }
  }  
}

void TaskTimingCsv(void *pvParameters) {
  BlockTiming timingInfo;
  Serial.println("task,block,start_us,end_us,duration_us");
  while (1) {
    if (xQueueReceive(samplingTimestampsQueue, &timingInfo, 0) == pdTRUE) {
      Serial.printf("sampling,%u,%u,%u,%u\n",
                    timingInfo.blockNumber,
                    timingInfo.start_timestamp,
                    timingInfo.end_timestamp,
                    timingInfo.end_timestamp - timingInfo.start_timestamp);
    }
    if (xQueueReceive(filterTimestampsQueue, &timingInfo, 0) == pdTRUE) {
      Serial.printf("filter,%u,%u,%u,%u\n",
                    timingInfo.blockNumber,
                    timingInfo.start_timestamp,
                    timingInfo.end_timestamp,
                    timingInfo.end_timestamp - timingInfo.start_timestamp);
    }
    if (xQueueReceive(fftTimestampsQueue, &timingInfo, 0) == pdTRUE) {
      Serial.printf("fft,%u,%u,%u,%u\n",
                    timingInfo.blockNumber,
                    timingInfo.start_timestamp,
                    timingInfo.end_timestamp,
                    timingInfo.end_timestamp - timingInfo.start_timestamp);
    }
    if (xQueueReceive(communicationTimestampsQueue, &timingInfo, 0) == pdTRUE) {
      Serial.printf("comm,%u,%u,%u,%u\n",
                    timingInfo.blockNumber,
                    timingInfo.start_timestamp,
                    timingInfo.end_timestamp,
                    timingInfo.end_timestamp - timingInfo.start_timestamp);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}



/*
 // Moving Average Filter with history across consecutive blocks
        const int windowSize = (FILTER_WINDOW_SIZE < SAMPLES) ? FILTER_WINDOW_SIZE : SAMPLES;

        if (!initialized) {
          for (int i = 0; i < windowSize; i++) {
            history[i] = procRaw[i];
          }
          initialized = true;
        }

        float runningSum = 0.0;
        for (int i = 0; i < windowSize; i++) {
          runningSum += history[i];
        }

        for (int i = 0; i < SAMPLES; i++) {
          runningSum += procRaw[i];

          int oldIdx = i - windowSize;
          if (oldIdx >= 0) {
            runningSum -= procRaw[oldIdx];
          } else {
            runningSum -= history[windowSize + oldIdx];
          }

          fillReal[i] = runningSum / (float)windowSize;
          fillImag[i] = 0; // Keep imaginary part zero for FFT
        }

        // Save tail samples for next block
        for (int i = 0; i < windowSize; i++) {
          history[i] = procRaw[SAMPLES - windowSize + i];
        }
      }*/






/*

/// @dac adc
double vReal1[SAMPLES], vImag1[SAMPLES];
double *fillReal = vReal0, *fillImag = vImag0;
double *procReal = vReal0, *procImag = vImag0;

volatile int sampleIdx = 0;
double phaseAccum = 0;
const double phaseInc = (2.0 * M_PI * TARGET_FREQ) / (double)SAMPLING_FREQUENCY;

SemaphoreHandle_t xBufferSemaphore;
hw_timer_t * timer = NULL;

void IRAM_ATTR onTimer() {
    // REDUCED AMPLITUDE: Stay away from 0V and 3.3V rails to avoid clipping
    uint8_t dac_val = (uint8_t)(128 + 60 * sin(phaseAccum)); 
    dac_output_voltage(DAC_CHANNEL_1, dac_val);
    
    
    
    The FFT is extremely sensitive to Phase Discontinuity. 
    If your sine wave "jumps" or "stutters" at the end of a buffer, 
    the FFT sees a massive glitch instead of a smooth tone.
    
    Without phaseInc: If you just used a simple loop index, the sine wave would reset to $0$ every 1024 samples. 
    This creates a "click" or "pop" in the data.
    
    With phaseInc: The phaseAccumulator simply keeps adding the increment forever. 
    When it hits $2\pi$, it wraps around. This ensures the last sample of Buffer A and the first sample of 
    Buffer B connect perfectly, creating a seamless, infinite sine wave.
    
    // phaseAccum = 0;
    phaseAccum += phaseInc;
    

    if (phaseAccum >= 2.0 * M_PI) phaseAccum -= 2.0 * M_PI;

    fillReal[sampleIdx] = (double)analogRead(34);
    fillImag[sampleIdx] = 0;
    sampleIdx++;

    if (sampleIdx >= SAMPLES) {
        sampleIdx = 0;
        procReal = fillReal; procImag = fillImag;
        fillReal = (fillReal == vReal0) ? vReal1 : vReal0;
        fillImag = (fillImag == vImag0) ? vImag1 : vImag0;

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(xBufferSemaphore, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
    }
}

void TaskFFT(void *pvParameters) {
    ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal0, vImag0, SAMPLES, SAMPLING_FREQUENCY);
    while (1) {
        if (xSemaphoreTake(xBufferSemaphore, portMAX_DELAY) == pdTRUE) {
            
            // --- STEP 1: Remove DC Offset (Crucial to kill that 4Hz peak) ---
            double mean = 0;
            for(int i=0; i<SAMPLES; i++) mean += procReal[i];
            mean /= SAMPLES;
            for(int i=0; i<SAMPLES; i++) procReal[i] -= mean;

            // --- STEP 2: Process FFT ---
            FFT = ArduinoFFT<double>(procReal, procImag, SAMPLES, SAMPLING_FREQUENCY);
            FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
            FFT.compute(FFT_FORWARD);
            FFT.complexToMagnitude();

            double peak = FFT.majorPeak();
            
            // Log for Serial Plotter (Blue line = Target, Red line = Detected)
            Serial.printf("Target:%f,Detected:%f\n", TARGET_FREQ, peak);
        }
    }
}

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    // ADC Attenuation: 11dB allows reading up to ~3.1V
    analogSetAttenuation(ADC_11db); 
    dac_output_enable(DAC_CHANNEL_1);
    xBufferSemaphore = xSemaphoreCreateBinary();

    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &onTimer, true);
    timerAlarmWrite(timer, 1000000 / SAMPLING_FREQUENCY, true);
    timerAlarmEnable(timer);

    xTaskCreatePinnedToCore(TaskFFT, "FFT", 10000, NULL, 1, NULL, 0);
}
*/
/*

// double buffer
// 1. Static allocation (prevents Stack Overflow)
double vReal0[SAMPLES], vImag0[SAMPLES];
double vReal1[SAMPLES], vImag1[SAMPLES];

// Control pointers
double *fillReal = vReal0;
double *fillImag = vImag0;
double *procReal = vReal1;
double *procImag = vImag1;

volatile bool bufferReady = false;
SemaphoreHandle_t xSemaphore = NULL;
TaskHandle_t FFTTaskHandle = NULL;

// Add a second semaphore for the handshake
SemaphoreHandle_t xFFTFinished = NULL;

void TaskSample(void *pvParameters) {
  int64_t next_waketime = esp_timer_get_time();
  const int64_t interval = 1000000 / SAMPLING_FREQUENCY;
  int sampleIdx = 0;

  while (1) {
    float t = (float)sampleIdx / (float)SAMPLING_FREQUENCY;
    fillReal[sampleIdx] = 50.0 * sin(2.0 * M_PI * TARGET_FREQ * t) + 20.0 * sin(2.0 * M_PI * (2*TARGET_FREQ) * t);
    fillImag[sampleIdx] = 0;
    sampleIdx++;

    if (sampleIdx >= SAMPLES) {
      // --- THE HANDSHAKE ---
      // Wait for FFT task to confirm it is finished with procReal/procImag
      // If the FFT is slow, the Sampler will pause here briefly
      if (xFFTFinished != NULL) {
        xSemaphoreTake(xFFTFinished, portMAX_DELAY);
      }

      double *tempReal = fillReal; double *tempImag = fillImag;
      fillReal = procReal; fillImag = procImag;
      procReal = tempReal; procImag = tempImag;

      sampleIdx = 0;
      xSemaphoreGive(xSemaphore); // Tell FFT: "Data is ready"
    }

    next_waketime += interval;
    int64_t sleep_time = next_waketime - esp_timer_get_time();
    if (sleep_time > 0) delayMicroseconds(sleep_time);
    if (sampleIdx % 32 == 0) vTaskDelay(1); 
  }
}

void TaskFFT(void *pvParameters) {
  while (1) {
    if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
      // Re-link the library to the newly swapped proc pointers
      ArduinoFFT<double> FFT = ArduinoFFT<double>(procReal, procImag, SAMPLES, SAMPLING_FREQUENCY);

      FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
      FFT.compute(FFT_FORWARD);
      FFT.complexToMagnitude();

      // Find Max Frequency (Shannon check)
      double threshold = 500.0; // Scaled: Peak * (SAMPLES/something)
      int topBin = 0;
      for (int i = (SAMPLES / 2) - 1; i >= 0; i--) {
        if (procReal[i] > threshold) {
          topBin = i;
          break; 
        }
      }

      double f_max = (topBin * SAMPLING_FREQUENCY) / (double)SAMPLES;
      Serial.printf("Detected Max Freq: %.2f Hz | New Suggested Fs: %.2f Hz\n", f_max, f_max * 2.5);

      // --- THE HANDSHAKE ---
      // Tell the Sampler: "I am done printing, you can have the buffer back"
      xSemaphoreGive(xFFTFinished);
    }
  }
}

void setup() {
  Serial.begin(115200);
  xSemaphore = xSemaphoreCreateBinary();
  xFFTFinished = xSemaphoreCreateBinary();
  
  // Initially, the FFT is "finished" so the Sampler can do the first swap
  xSemaphoreGive(xFFTFinished); 

  xTaskCreatePinnedToCore(TaskSample, "Sampler", 4096, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(TaskFFT, "FFT_Task", 10000, NULL, 1, &FFTTaskHandle, 0);
}
*/