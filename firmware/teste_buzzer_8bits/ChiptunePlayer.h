#pragma once
#include <stddef.h>
#include <stdint.h>
struct ChipNote { uint16_t hz, soundMs, pauseMs; };
// Fanfare original em arpejos, estilo 8 bits. A saida eletrica e onda quadrada.
constexpr ChipNote CHIP_NOTES[] = {
  {2093,70,12},{2637,70,12},{3136,70,12},{4186,180,12},{3136,70,12},{2637,70,12},{2093,180,80},
  {2349,70,12},{2960,70,12},{3520,70,12},{4699,180,12},{3520,70,12},{2960,70,12},{2349,180,80},
  {2093,70,10},{2637,70,10},{3136,70,10},{4186,70,10},{3136,70,10},{2637,70,10},
  {2349,70,10},{1976,70,10},{2093,70,10},{4186,500,0},
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
