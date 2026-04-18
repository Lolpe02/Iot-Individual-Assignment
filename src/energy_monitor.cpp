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
  // RP2040 (Arduino-mbed): Wire.begin() has no SDA/SCL parameters.
  Wire.begin();
  Wire.setClock(I2C_FREQ);
  if (!ina219.begin(&Wire)) {
    Serial.println("Failed to find INA219 chip");
    while (1) { delay(10); }
  }
  // calibration for idk, pc usb power supply (5V, ~500mA max)
  ina219.setCalibration_16V_400mA();
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