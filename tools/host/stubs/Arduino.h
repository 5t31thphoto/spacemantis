#pragma once
#include "esp_sdk_macros.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
uint32_t millis();
inline uint32_t micros() { static uint32_t k = 0x1234567u; k = k * 1664525u + 1013904223u; return k ^ (millis() * 1000u); }
void delay(uint32_t ms);
struct EspClass { uint64_t getEfuseMac() { return 0x1234567890ABull; } };
extern EspClass ESP;
