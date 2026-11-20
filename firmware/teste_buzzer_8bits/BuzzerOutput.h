#pragma once
#include <Arduino.h>
class BuzzerOutput {
 public:
  bool begin(uint8_t pin) { pin_=pin; return ledcAttach(pin_,2000,8) && ledcWrite(pin_,0); }
  bool tone(uint16_t frequency) {
    if (!frequency) return ledcWrite(pin_,0);
    // PWM de 8 bits, 50%: 128 de 256. Frequencia muda a cada nota.
    if (!ledcChangeFrequency(pin_,frequency,8)) return false;
    return ledcWrite(pin_,128);
  }
 private: uint8_t pin_=32;
};
