#pragma once
#include <stdint.h>
namespace panelio {
class ButtonDebouncer {
 public:
  bool update(bool pressed,uint32_t now){
    if(pressed!=raw_){raw_=pressed;changedAt_=now;}
    if(pressed!=stable_&&uint32_t(now-changedAt_)>=40){stable_=pressed;return stable_;}
    return false;
  }
 private: bool raw_=false,stable_=false;uint32_t changedAt_=0;
};
class PulseTimer {
 public:
  void start(uint32_t now,uint32_t duration){start_=now;duration_=duration;active_=true;}
  bool active(uint32_t now){if(active_&&uint32_t(now-start_)>=duration_)active_=false;return active_;}
 private: bool active_=false;uint32_t start_=0,duration_=0;
};
}
