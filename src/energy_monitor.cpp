#define PIN_SDA 4
#define PIN_SCL 5
#define I2C_FREQ 400000

// INA default
#define INA219_ADDR 0x40
#define SAMPLING_FREQUENCY 500


#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_INA219.h"


Adafruit_INA219 ina219(INA219_ADDR);

void setup() {
  Serial.begin(115200);
  const unsigned long serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 3000) {
    delay(10);
  }
  Serial.println("[BOOT] Pico started");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Pico + Earle core supports explicit I2C pin assignment.
  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.begin();
  Wire.setClock(I2C_FREQ);
  if (!ina219.begin(&Wire)) {
    Serial.println("[ERR] Failed to find INA219 chip");
    while (1) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      Serial.println("[ERR] INA219 not detected, check wiring and address 0x40");
      delay(1000);
    }
  }
  // calibration for idk, pc usb power supply (5V, ~500mA max)
  ina219.setCalibration_16V_400mA();
  Serial.println("[OK] INA219 initialized");
}

void loop() {
  static unsigned long lastWakeTime = 0;
  const unsigned long periodMs = (SAMPLING_FREQUENCY > 0) ? (1000UL / SAMPLING_FREQUENCY) : 1UL;
  const unsigned long now = millis();

  if (now - lastWakeTime < periodMs) {
    return;
  }
  lastWakeTime = now;

  float shuntVoltage = ina219.getShuntVoltage_mV();
  float busVoltage = ina219.getBusVoltage_V();
  float current_mA = ina219.getCurrent_mA();
  float power_mW = ina219.getPower_mW();

  char line[140];
  snprintf(line, sizeof(line),
           "Bus Voltage: %.2f V | Shunt Voltage: %.2f mV | Current: %.2f mA | Power: %.2f mW",
           busVoltage, shuntVoltage, current_mA, power_mW);
  Serial.println(line);
}