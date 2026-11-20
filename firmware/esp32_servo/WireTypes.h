#pragma once
#include <stdint.h>
struct GateSnapshot {
  char state[10]; int angle; int targetAngle;
  char commandId[81]; char commandPhase[12]; char completionId[81];
};
struct ServoCommand { char id[81]; bool open; uint32_t validUntil; };
struct PeerHealth { bool online; };

// Limite superior conservador do horario do servidor, obtido por ida e volta
// autenticada. Permite descartar comandos vencidos mesmo sem NTP na bancada.
class CommandClock {
 public:
  void sync(int64_t serverTime, uint32_t receivedAt, uint32_t roundTripMs) {
    upper_ = serverTime + roundTripMs; at_ = receivedAt; ready_ = true;
  }
  int64_t upperNow(uint32_t now) const { return upper_ + uint32_t(now - at_); }
  int64_t remaining(int64_t expiresAt, uint32_t now) const {
    return ready_ ? expiresAt - upperNow(now) : -1;
  }
 private:
  bool ready_ = false; int64_t upper_ = 0; uint32_t at_ = 0;
};
