#ifndef _CRYPTORNG_H
#define _CRYPTORNG_H

#include <stddef.h>
#include <stdint.h>

// Replaces the one in arduinolibs, uses the TRNG in ESP8266
class RNGClass {
  public:
    static void rand(uint8_t *data, size_t len) {
      for(size_t p=0; p<len; ++p) {
        data[p]=*((volatile uint8_t*)0x3FF20E44);
      }
    }
};

extern RNGClass RNG;

#endif
