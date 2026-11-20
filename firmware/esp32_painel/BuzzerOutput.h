#pragma once
#include <Arduino.h>
class BuzzerOutput {
 public:
  // Nivel de audio do programa: 100 = maior excitacao da fundamental neste
  // acionamento simples. Corresponde a PWM de 50%, nunca a nivel DC de 100%.
  static constexpr uint8_t VOLUME_PERCENT = 100;
  static constexpr uint8_t PWM_RESOLUTION = 8;
  static constexpr uint32_t SOUND_DUTY = (128U * VOLUME_PERCENT + 50U) / 100U;
  static_assert(VOLUME_PERCENT <= 100 && SOUND_DUTY <= 128, "Volume deve ficar entre 0 e 100");
  bool begin(uint8_t pin) { pin_=pin; return ledcAttach(pin_,2000,PWM_RESOLUTION) && ledcWrite(pin_,0); }
  bool tone(uint16_t frequency) {
    if (!frequency) return ledcWrite(pin_,0);
    if (!ledcChangeFrequency(pin_,frequency,PWM_RESOLUTION)) return false;
    return ledcWrite(pin_,SOUND_DUTY);
  }
 private: uint8_t pin_=32;
};
