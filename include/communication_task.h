#pragma once

void setupCommunication();
void reconnectMQTT();
void publishMQTT(const char *topic, const char *payload);
void TaskCommunication(void *pvParameters);
