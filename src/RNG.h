// Replaces the one in arduinolibs, uses the TRNG in ESP8266
#pragma once

#include <stddef.h>
#include <stdint.h>

class RNGClass {
  public:
    static void rand(uint8_t *data, size_t len) {
      for(size_t p=0; p<len; ++p) {
        data[p]=*((volatile uint8_t*)0x3FF20E44);
      }
    }
};

extern RNGClass RNG;
