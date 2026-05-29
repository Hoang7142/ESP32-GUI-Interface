#pragma once

#include <Arduino.h>

bool loraBegin();
bool loraSend(const uint8_t* data, size_t len);
bool loraRxPending();
int loraReceive(uint8_t* data, size_t maxLen, int16_t* rssiOut, float* snrOut);
void loraPrintChipStatus();
