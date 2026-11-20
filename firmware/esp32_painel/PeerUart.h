#pragma once
#include <stddef.h>
#include <stdint.h>

namespace peer_uart {
constexpr uint32_t BAUD_RATE = 115200;
constexpr int SERVO_TX_PIN = 27;
constexpr int SERVO_RX_PIN = 33;
constexpr int PANEL_TX_PIN = SERVO_RX_PIN;
constexpr int PANEL_RX_PIN = SERVO_TX_PIN;
constexpr int PROTOCOL_VERSION = 2;
constexpr size_t BOOT_ID_LENGTH = 16;
constexpr uint32_t HELLO_INTERVAL_MS = 1000;
constexpr uint32_t LINK_TIMEOUT_MS = 3000;
constexpr size_t MAX_FRAME_BYTES = 2048;
constexpr size_t RX_BUFFER_BYTES = 4096;

inline bool isFresh(uint32_t now, uint32_t receivedAt) {
  return uint32_t(now - receivedAt) < LINK_TIMEOUT_MS;
}

class LineReader {
 public:
  template <typename Port>
  bool read(Port& serial) {
    while (serial.available() > 0) {
      const int byte = serial.read();
      if (byte < 0) return false;
      if (byte == '\n') {
        const bool complete = !discarding_ && used_ != 0;
        length_ = used_;
        used_ = 0;
        discarding_ = false;
        if (complete) return true;
      } else if (!discarding_) {
        if (used_ == sizeof(line_) || byte == 0) discarding_ = true;
        else line_[used_++] = static_cast<char>(byte);
      }
    }
    return false;
  }

  const char* data() const { return line_; }
  size_t size() const { return length_; }

 private:
  char line_[MAX_FRAME_BYTES];
  size_t used_ = 0;
  size_t length_ = 0;
  bool discarding_ = false;
};

template <typename Port, typename Document>
void send(Port& serial, const Document& packet) {
  if (measureJson(packet) > MAX_FRAME_BYTES) return;
  // O primeiro LF encerra um quadro incompleto se o cabo voltar no meio do envio.
  serial.write(static_cast<uint8_t>('\n'));
  serializeJson(packet, serial);
  serial.write(static_cast<uint8_t>('\n'));
}
}
