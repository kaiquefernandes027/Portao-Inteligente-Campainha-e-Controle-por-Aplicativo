#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

inline bool validWireId(const char* id) {
  if (!id) return false;
  const size_t n = strlen(id); if (n < 8 || n > 80) return false;
  for (size_t i = 0; i < n; ++i) if (!isalnum((unsigned char)id[i]) && id[i] != '_' && id[i] != '-') return false;
  return true;
}
class Outbox {
 public:
  bool begin() {
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) return false;
    ready_ = LittleFS.begin(false);
    if (!ready_) {
      Serial.println("LittleFS nao montou. Para FORMATAR a fila, envie F em ate 10 s (somente se aceitar apagar os eventos locais).");
      const uint32_t start = millis();
      while (uint32_t(millis() - start) < 10000) {
        if (Serial.available() && Serial.read() == 'F') { ready_ = LittleFS.format() && LittleFS.begin(false); break; }
        delay(10);
      }
    }
    if (!ready_) { Serial.println("ERRO: historico local indisponivel. A operacao local continua, mas eventos novos podem ser perdidos."); return false; }
    LittleFS.mkdir("/outbox");
    File dir = LittleFS.open("/outbox");
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) if (String(f.name()).endsWith(".json")) ++count_;
    return true;
  }
  bool save(JsonDocument& event) {
    const char* id = event["id"] | "";
    if (!ready_ || !validWireId(id) || measureJson(event) > 1024) return false;
    const String path = String("/outbox/") + id + ".json";
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool ok = LittleFS.exists(path);
    if (!ok && count_ < 128) {
      const String tmp = path + ".tmp";
      File file = LittleFS.open(tmp, FILE_WRITE);
      ok = bool(file) && serializeJson(event, file) == measureJson(event);
      file.flush(); file.close();
      if (ok) ok = LittleFS.rename(tmp, path);
      if (ok) ++count_; else LittleFS.remove(tmp);
    }
    xSemaphoreGive(mutex_);
    return ok;
  }
  String next() {
    if (!ready_) return String();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    String best;
    File dir = LittleFS.open("/outbox");
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String path = f.path();
      if (path.endsWith(".json") && (best.isEmpty() || path.compareTo(best) < 0)) best = path;
    }
    String data;
    if (!best.isEmpty()) { File f = LittleFS.open(best, FILE_READ); if (f && f.size() <= 1024) data = f.readString(); }
    xSemaphoreGive(mutex_);
    return data;
  }
  void ack(const char* id) {
    if (!ready_ || !validWireId(id)) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (LittleFS.remove(String("/outbox/") + id + ".json") && count_) --count_;
    xSemaphoreGive(mutex_);
  }
  uint16_t size() {
    if (!ready_) return 0;
    xSemaphoreTake(mutex_, portMAX_DELAY); const uint16_t n = count_; xSemaphoreGive(mutex_); return n;
  }
 private:
  bool ready_ = false; uint16_t count_ = 0; SemaphoreHandle_t mutex_ = nullptr;
};
inline void stampEvent(JsonDocument& event) {
  const time_t now = time(nullptr);
  if (now > 1600000000) event["occurredAt"] = int64_t(now) * 1000;
  else event["occurredAt"] = nullptr;
}
