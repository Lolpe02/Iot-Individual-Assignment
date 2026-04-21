#pragma once

#include <Arduino.h>

void setupI2S(uint32_t samplingFreq);
void TaskReadADC(void *pvParameters);
