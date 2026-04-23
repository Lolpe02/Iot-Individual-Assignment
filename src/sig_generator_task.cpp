#include "sig_generator_task.h"
//#include <driver/dac.h>
#include "shared.h"
#include "config.h"

// Task separato, usa DAC diretto senza I2S
#define DAC_GEN_PIN 25 // Pin DAC1 dell'ESP32
#define GEN_UPDATE_HZ 5000 // 1kHz è perfetto per una sinusoide a 5Hz fluida

const float kNoiseSigma = 0.2f;
const float kSpikeProb = 0.01f; // Ridotto un po' per stabilità
const int nWaves = 1;
const float waveFreqs[] = {5.0f}; // Solo la portante a 5Hz
const float waveAmps[] = {4.0f};  // Ampiezza

// Funzioni matematiche per il rumore (dal tuo codice Pico)
static float uniform01() { return (float)random(0, 10000) / 10000.0f; }

static float gaussianNoise(float sigma) {
  float z = 0.0f;
  for (int i = 0; i < 6; i++) z += uniform01();
  z -= 3.0f;
  return z * sigma;
}

static float anomalySpike(float p) {
  if (uniform01() < p) {
    float magnitude = 5.0f + 10.0f * uniform01();
    return (uniform01() < 0.5f) ? magnitude : -magnitude;
  }
  return 0.0f;
}

void TaskSignalGenerator(void *pvParameters) {
  (void)pvParameters;
  
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(1000 / GEN_UPDATE_HZ);
  
  float t = 0.0f;
  const float dt = 1.0f / GEN_UPDATE_HZ;

  Serial.println("[Generator] Internal DAC Signal Task started on Pin 25");

  while (1) {
    // 1. Calcolo del segnale sinusoidale
    float signalRaw = 0.0f;
    float signalAbsMax = 0.0f;

    for (int i = 0; i < nWaves; i++) {
      signalRaw += waveAmps[i] * sin(2.0f * PI * waveFreqs[i] * t);
      signalAbsMax += waveAmps[i];
    }

    // 2. Aggiunta di rumore e anomalie per testare i tuoi filtri
    //signalRaw += gaussianNoise(kNoiseSigma);
    //signalRaw += anomalySpike(kSpikeProb);

    // 3. Mapping per il DAC (0-255)
    // Mappiamo l'ampiezza (es. +/- 4) in un range 0.0 - 1.0, poi a 0-255
    float waveNorm = 0.5f + (signalRaw / (2.5f * signalAbsMax)); 
    waveNorm = constrain(waveNorm, 0.0f, 1.0f);
    
    int dacVal = (int)(waveNorm * 255.0f);
    
    // 4. Output fisico sul DAC
    dacWrite(DAC_GEN_PIN, dacVal);

    t += dt;

    // Attesa precisa per mantenere la frequenza di aggiornamento
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}