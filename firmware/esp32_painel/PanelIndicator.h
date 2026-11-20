#pragma once
#include <stdint.h>
#include <string.h>
struct PanelPacket {
  bool link; char state[10]; int angle, targetAngle; char completionId[81]; uint32_t receivedAt;
};
class PanelIndicator {
 public:
  bool apply(const PanelPacket& packet) {
    if (!packet.link) { linked_=false; return false; }
    const bool newOpening = live(packet.receivedAt) && !strcmp(packet.state,"open") && packet.completionId[0] && strcmp(last_.completionId,packet.completionId);
    last_=packet; has_=true; linked_=true;
    return newOpening;
  }
  bool live(uint32_t now) const { return has_ && linked_ && uint32_t(now-last_.receivedAt)<3000; }
  bool ledOn() const { return has_ && strcmp(last_.state,"closed")!=0; }
  bool known() const { return has_; }
  const PanelPacket& snapshot() const { return last_; }
 private:
  bool has_=false, linked_=false; PanelPacket last_={};
};
