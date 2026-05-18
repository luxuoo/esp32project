#pragma once

#include <Arduino.h>

void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleButtonRefresh();
