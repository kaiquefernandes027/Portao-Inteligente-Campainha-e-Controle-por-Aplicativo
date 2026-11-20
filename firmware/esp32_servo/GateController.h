#pragma once
#include <stdint.h>

// Logica sem dependencias do Arduino, tambem utilizada nos testes de bancada.
class GateController {
 public:
  enum State { CLOSED, OPENING, OPEN, CLOSING };
  enum StartResult { STARTED, ALREADY_THERE, BUSY };
  GateController(int closedAngle, int openAngle, uint32_t stepMs, uint32_t settleMs)
      : closed_(closedAngle), open_(openAngle), stepMs_(stepMs), settleMs_(settleMs),
        angle_(closedAngle), target_(closedAngle) {}

  StartResult start(bool open, uint32_t now) {
    if (moving()) return BUSY;
    const int requested = open ? open_ : closed_;
    if (requested == angle_) return ALREADY_THERE;
    target_ = requested;
    state_ = open ? OPENING : CLOSING;
    lastStep_ = now;
    settling_ = false;
    return STARTED;
  }
  // Retorna true apenas quando termina o movimento e o tempo de acomodacao.
  bool tick(uint32_t now) {
    if (!moving()) return false;
    if (!settling_ && uint32_t(now - lastStep_) >= stepMs_) {
      angle_ += target_ > angle_ ? 1 : -1;
      lastStep_ = now;
      if (angle_ == target_) { settling_ = true; settleStart_ = now; }
    }
    if (settling_ && uint32_t(now - settleStart_) >= settleMs_) {
      state_ = target_ == open_ ? OPEN : CLOSED;
      settling_ = false;
      return true;
    }
    return false;
  }
  bool moving() const { return state_ == OPENING || state_ == CLOSING; }
  bool ledOn() const { return state_ != CLOSED; }
  int angle() const { return angle_; }
  int target() const { return target_; }
  State state() const { return state_; }
  const char* stateName() const {
    switch (state_) {
      case CLOSED: return "closed";
      case OPENING: return "opening";
      case OPEN: return "open";
      default: return "closing";
    }
  }
 private:
  int closed_, open_;
  uint32_t stepMs_, settleMs_;
  int angle_, target_;
  State state_ = CLOSED;
  uint32_t lastStep_ = 0, settleStart_ = 0;
  bool settling_ = false;
};

class ButtonDebouncer {
 public:
  bool update(bool pressed, uint32_t now) {
    if (pressed != raw_) { raw_ = pressed; changedAt_ = now; }
    if (pressed != stable_ && uint32_t(now - changedAt_) >= 40) {
      stable_ = pressed;
      return stable_;  // Uma borda por pressionamento, nao repete se segurar.
    }
    return false;
  }
 private:
  bool raw_ = false, stable_ = false;
  uint32_t changedAt_ = 0;
};

class PulseTimer {
 public:
  void start(uint32_t now, uint32_t duration) {
    started_ = now; duration_ = duration; active_ = true;
  }
  bool active(uint32_t now) {
    if (active_ && uint32_t(now - started_) >= duration_) active_ = false;
    return active_;
  }
 private:
  bool active_ = false;
  uint32_t started_ = 0, duration_ = 0;
};
