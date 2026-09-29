#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
uint32_t millis();
void delay(uint32_t ms);
struct EspClass { uint64_t getEfuseMac() { return 0x1234567890ABull; } };
extern EspClass ESP;
