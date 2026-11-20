/* Grave SOMENTE no ESP32 do painel, para testar buzzer passivo GPIO 32.
 * Sem Wi-Fi, display, flash, servo nem bibliotecas externas.
 * Arduino-ESP32 3.x. Regrave esp32_painel ao terminar.
 */
#include <Arduino.h>
#include <esp_arduino_version.h>
#include "BuzzerOutput.h"
#include "ChiptunePlayer.h"
#include "InputTimers.h"
#if ESP_ARDUINO_VERSION_MAJOR < 3
#error "Este teste exige pacote Espressif ESP32 3.x."
#endif
BuzzerOutput buzzer;
ChiptunePlayer chip;
panelio::ButtonDebouncer button;
panelio::PulseTimer testTimer;
bool testingTone=false;
void setTone(uint16_t hz){if(!buzzer.tone(hz))Serial.println("ERRO: nao foi possivel gerar PWM no GPIO 32.");}
void play(){testingTone=false;chip.start(millis());setTone(chip.frequency());Serial.println("Tocando som 8 bits (24 notas).");}
void setup(){
  Serial.begin(115200);pinMode(14,INPUT_PULLUP);
  if(!buzzer.begin(32)){Serial.println("ERRO: ledcAttach/ledcWrite falhou.");while(true)delay(1000);}
  delay(500);Serial.println("Referencia: 2 kHz por 1 segundo; depois som 8 bits. C=musica; T=tom; S=parar.");
  setTone(2000);delay(1000);setTone(0);delay(200);play();
}
void loop(){
  if(Serial.available()){
    const char c=Serial.read();
    if(c=='C'||c=='c')play();
    else if(c=='T'||c=='t'){chip.stop();testingTone=true;testTimer.start(millis(),2000);setTone(2000);Serial.println("Tom de 2 kHz.");}
    else if(c=='S'||c=='s'){chip.stop();testingTone=false;setTone(0);}
  }
  if(button.update(digitalRead(14)==LOW,millis())){Serial.println("Botao GPIO 14 reconhecido.");play();}
  if(chip.update(millis()))setTone(chip.frequency());
  if(testingTone&&!testTimer.active(millis())){testingTone=false;setTone(0);}
  delay(1);
}
