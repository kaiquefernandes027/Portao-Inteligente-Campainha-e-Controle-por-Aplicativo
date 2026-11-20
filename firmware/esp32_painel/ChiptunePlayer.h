#pragma once
#include <stddef.h>
#include <stdint.h>
struct ChipNote { uint16_t hz, soundMs, pauseMs; };
// Perfil de maior audibilidade: arpejos entre 1568 e 2349 Hz, perto de 2 kHz.
// Notas sustentadas e pausas curtas mantem o estilo 8 bits. O ganho real depende
// da resposta do buzzer; a tensao e a amplitude eletrica do GPIO nao mudam.
constexpr ChipNote CHIP_NOTES[] = {
  {1568,120,6},{1760,120,6},{1976,120,6},{2093,280,6},{1976,120,6},{1760,120,6},{1568,280,60},
  {1760,120,6},{1976,120,6},{2217,120,6},{2349,280,6},{2217,120,6},{1976,120,6},{1760,280,60},
  {1760,120,6},{1976,120,6},{2093,120,6},{2349,120,6},{2093,120,6},{1976,120,6},
  {1850,120,6},{1760,120,6},{1976,120,6},{2093,700,0},
};
constexpr size_t CHIP_COUNT = sizeof(CHIP_NOTES)/sizeof(CHIP_NOTES[0]);
class ChiptunePlayer {
 public:
  void start(uint32_t now) { index_=0; playing_=true; pause_=false; started_=now; frequency_=CHIP_NOTES[0].hz; }
  void stop() { playing_=false; frequency_=0; }
  bool update(uint32_t now) {
    if (!playing_) return false;
    const ChipNote& note=CHIP_NOTES[index_];
    if (uint32_t(now-started_) < (pause_ ? note.pauseMs : note.soundMs)) return false;
    started_=now;
    if (pause_) { ++index_; pause_=false; frequency_=CHIP_NOTES[index_].hz; }
    else { frequency_=0; if(index_+1==CHIP_COUNT) playing_=false; else pause_=true; }
    return true;
  }
  bool playing() const { return playing_; }
  uint16_t frequency() const { return frequency_; }
  static constexpr uint32_t durationMs() { uint32_t t=0; for(const auto& n:CHIP_NOTES)t+=n.soundMs+n.pauseMs; return t; }
 private:
  size_t index_=0; bool playing_=false,pause_=false; uint16_t frequency_=0; uint32_t started_=0;
};
