#define PIN_SDA 20
#define PIN_SCL 21
#define I2C_FREQ 400000

// PIN per il segnale PWM (collegalo al condensatore del tuo circuito)
#define PWM_OUTPUT_PIN 15 

// Configurazione INA e Segnale
#define INA219_ADDR 0x40
#define PWM_CARRIER_HZ 200000    // Frequenza base PWM hardware
#define WAVE_UPDATE_HZ 5000      // Aggiornamento duty per onda molto piu morbida
#define TELEMETRY_UPDATE_HZ 25   // Lettura INA + print a bassa frequenza

const float kNoiseSigma = 0.2f;
const float kSpikeProb = 0.02f;

#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_INA219.h"
#include <math.h>

Adafruit_INA219 ina219(INA219_ADDR);
static bool gInaReady = false;

static float uniform01() {
  return (float)random(0, 10000) / 10000.0f;
}

static float gaussianNoise(float sigma) {
  // Approssimazione gaussiana leggera (CLT) per evitare trig/log pesanti.
  float z = 0.0f;
  for (int i = 0; i < 6; i++) {
    z += uniform01();
  }
  z -= 3.0f;
  return z * sigma;
}

static float anomalySpike(float p) {
  if (uniform01() < p) {
    float magnitude = 5.0f + 10.0f * uniform01(); // U(5, 15)
    return (uniform01() < 0.5f) ? magnitude : -magnitude;
  }
  return 0.0f;
}

void setup() {
  Serial.begin(115200);
  const unsigned long serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 3000) {
    delay(10);
  }
  Serial.println("[BOOT] Pico started");
  randomSeed((uint32_t)micros());

  // Configurazione PWM
  pinMode(PWM_OUTPUT_PIN, OUTPUT);
  analogWriteFreq(PWM_CARRIER_HZ);
  analogWriteRange(255);   // Risoluzione 8-bit (0-255)

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.begin();
  Wire.setClock(I2C_FREQ);
  
  gInaReady = ina219.begin(&Wire);
  if (!gInaReady) {
    Serial.println("[WARN] INA219 not found: PWM and waveform output will continue");
  } else {
    ina219.setCalibration_16V_400mA();
    Serial.println("[OK] INA219 initialized + PWM Signal Output on Pin 15");
  }
}

void loop() {
  static unsigned long lastWaveUs = 0;
  static unsigned long lastTelemetryUs = 0;
  static float t = 0.0f;
  static float lastSignalRaw = 0.0f;
  static float lastWaveNorm = 0.5f;

  const unsigned long wavePeriodUs = 1000000UL / WAVE_UPDATE_HZ;
  const unsigned long telemetryPeriodUs = 1000000UL / TELEMETRY_UPDATE_HZ;
  const unsigned long now = micros();

  if (now - lastWaveUs >= wavePeriodUs) {
    lastWaveUs = now;
    t += (float)wavePeriodUs / 1000000.0f;

    float signalRaw = 0.0f;
    float waveNorm = 0.0f;

    signalRaw = 2.0f * sin(2.0f * PI * 3.0f * t)
              + 4.0f * sin(2.0f * PI * 5.0f * t)
              + gaussianNoise(kNoiseSigma)
              + anomalySpike(kSpikeProb);

    // Mappa un range atteso [-22, 22] su [0, 1], con saturazione.
    const float signalAbsMax = 22.0f;
    waveNorm = 0.5f + (signalRaw / (2.0f * signalAbsMax));
    waveNorm = constrain(waveNorm, 0.0f, 1.0f);

    int pwmVal = (int)(waveNorm * 255.0f + 0.5f);
    analogWrite(PWM_OUTPUT_PIN, pwmVal);

    lastSignalRaw = signalRaw;
    lastWaveNorm = waveNorm;
  }

  if (now - lastTelemetryUs >= telemetryPeriodUs) {
    lastTelemetryUs = now;

    // --- LETTURA INA219 ---
    float current_mA = 0.0f;
    float power_mW = 0.0f;
    if (gInaReady) {
      current_mA = ina219.getCurrent_mA();
      power_mW = ina219.getPower_mW();
    }

    Serial.printf(">Wave:%.2f, Raw:%.3f, Current:%.2f, Power:%.2f\r\n", lastWaveNorm * 100.0f, lastSignalRaw, current_mA, power_mW);
  }
}