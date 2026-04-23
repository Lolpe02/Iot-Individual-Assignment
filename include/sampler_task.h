#pragma once

#include <Arduino.h>

void setupI2S(uint32_t samplingRateHzRequestedForI2s);
void TaskReadADC(void *pvParameters);
