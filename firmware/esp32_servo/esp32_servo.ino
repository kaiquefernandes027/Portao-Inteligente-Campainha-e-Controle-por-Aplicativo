/* ESP32 A: SOMENTE servo MG90S no GPIO 13.
 * Painel B troca mensagens por UART2, GPIO 27 TX e GPIO 33 RX.
 * Este ESP32 tambem conecta ao servidor Node por WebSocket para o app.
 * Arduino-ESP32 3.x + ArduinoJson 7 + WebSockets by Markus Sattler/Links2004.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Preferences.h>
#include <esp_arduino_version.h>
#include <esp_system.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "config.h"
#include "root_ca.h"
#include "GateController.h"
#include "WireTypes.h"
#include "Outbox.h"
#include "PeerUart.h"
#include "ServoOutput.h"

#if ESP_ARDUINO_VERSION_MAJOR < 3 || !defined(CONFIG_IDF_TARGET_ESP32)
#error "Use ESP32 classico WROOM-32 com pacote Espressif 3.x."
#endif
static_assert(CLOSED_ANGLE >= 0 && CLOSED_ANGLE <= 180 && OPEN_ANGLE >= 0 && OPEN_ANGLE <= 180 && CLOSED_ANGLE != OPEN_ANGLE, "Angulos invalidos");
static_assert(SERVO_MIN_US >= 500 && SERVO_MAX_US <= 2500 && SERVO_MIN_US < SERVO_MAX_US, "Pulsos invalidos");
GateController gate(CLOSED_ANGLE, OPEN_ANGLE, SERVO_STEP_MS, SERVO_SETTLE_MS);
ServoOutput servoOutput;
Outbox outbox;
Preferences prefs;
QueueHandle_t stateQueue, commandQueue, healthQueue;
char bootId[24], lastCommandId[81] = {}, completionId[81] = {};
char reportId[81] = {}, reportPhase[12] = {};
bool seenCommandThisBoot = false;
uint32_t eventSequence = 0;

void publishState() {
  GateSnapshot snapshot = {};
  strlcpy(snapshot.state, gate.stateName(), sizeof(snapshot.state));
  snapshot.angle = gate.angle(); snapshot.targetAngle = gate.target();
  strlcpy(snapshot.commandId, reportId, sizeof(snapshot.commandId));
  strlcpy(snapshot.commandPhase, reportPhase, sizeof(snapshot.commandPhase));
  strlcpy(snapshot.completionId, completionId, sizeof(snapshot.completionId));
  xQueueOverwrite(stateQueue, &snapshot);
}
void saveServoEvent(const char* type, const char* id = nullptr, const char* detail = nullptr) {
  JsonDocument event;
  char eventId[64]; snprintf(eventId, sizeof(eventId), "s-%s-%08lx", bootId, (unsigned long)++eventSequence);
  event["id"] = eventId; event["type"] = type; event["source"] = "servo";
  if (id && id[0]) event["commandId"] = id;
  if (detail) event["detail"] = detail;
  stampEvent(event);
  if (!outbox.save(event)) Serial.println("ERRO: evento do servo nao gravado (fila cheia ou filesystem indisponivel).");
}
void fillState(JsonDocument& packet, const GateSnapshot& state) {
  packet["type"] = "gate_state";
  packet["data"]["state"] = state.state;
  packet["data"]["angle"] = state.angle;
  packet["data"]["targetAngle"] = state.targetAngle;
  packet["data"]["completionId"] = state.completionId;
  packet["data"]["estimated"] = true;
}

void peerTask(void*) {
  Serial2.setRxBufferSize(peer_uart::RX_BUFFER_BYTES);
  Serial2.begin(peer_uart::BAUD_RATE, SERIAL_8N1, peer_uart::SERVO_RX_PIN, peer_uart::SERVO_TX_PIN);
  static peer_uart::LineReader receiver;
  bool authenticated = false, online = false, authErrorReported = false;
  uint32_t lastHello = 0, lastState = 0;
  for (;;) {
    const bool wasOnline = online;
    if (authenticated && !peer_uart::isFresh(millis(), lastHello)) {
      authenticated = false;
      online = false;
    }
    while (receiver.read(Serial2)) {
      JsonDocument request;
      if (deserializeJson(request, receiver.data(), receiver.size())) continue;
      const char* kind = request["type"] | "";
      if (strcmp(kind, "peer_auth") == 0) {
        const char* panelBootId = request["panelBootId"] | "";
        authenticated = request["protocol"].as<int>() == peer_uart::PROTOCOL_VERSION &&
          strlen(panelBootId) == peer_uart::BOOT_ID_LENGTH &&
          strcmp(request["panelId"] | "", PANEL_ID) == 0 &&
          strlen(PEER_SHARED_KEY) == 64 && strcmp(request["key"] | "", PEER_SHARED_KEY) == 0;
        // O painel confirma que recebeu a resposta deste boot. Isso verifica os dois fios.
        online = authenticated && strcmp(request["seenServoBootId"] | "", bootId) == 0;
        if (!authenticated) {
          if (!authErrorReported) Serial.println("UART: confira PANEL_ID, PEER_SHARED_KEY e a versao do firmware nas duas placas.");
          authErrorReported = true;
          continue;
        }
        authErrorReported = false;
        lastHello = millis();
        JsonDocument reply;
        reply["type"] = "peer_ready";
        reply["protocol"] = peer_uart::PROTOCOL_VERSION;
        reply["panelBootId"] = panelBootId;
        reply["servoBootId"] = bootId;
        const time_t now = time(nullptr);
        if (now > 1600000000) reply["epochSeconds"] = int64_t(now);
        peer_uart::send(Serial2, reply);
        continue;
      }
      // A UART aceita eventos de campainha. O comando do servo vem do servidor.
      if (!authenticated || strcmp(kind, "peer_event") != 0) continue;
      const JsonObjectConst payload = request["event"].as<JsonObjectConst>();
      const char* id = payload["id"] | "";
      if (!validWireId(id) || strcmp(payload["type"] | "", "doorbell") ||
          strcmp(payload["source"] | "", "panel") || !payload["commandId"].isNull()) continue;
      JsonDocument event;
      event.set(payload);
      if (!outbox.save(event)) continue;
      JsonDocument ack;
      ack["type"] = "peer_event_ack";
      ack["id"] = id;
      peer_uart::send(Serial2, ack);
    }
    if (online != wasOnline) Serial.println(online ? "PAINEL UART: conectado." : "PAINEL UART: sem resposta.");
    const PeerHealth health = {online};
    xQueueOverwrite(healthQueue, &health);
    if (authenticated && uint32_t(millis() - lastState) >= 150) {
      lastState = millis();
      GateSnapshot snapshot = {};
      if (xQueuePeek(stateQueue, &snapshot, 0) == pdTRUE) {
        JsonDocument packet;
        fillState(packet, snapshot);
        peer_uart::send(Serial2, packet);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void cloudTask(void*) {
  // A biblioteca deixa os ponteiros de certificado do cliente sem inicializacao.
  // O armazenamento estatico zera esses campos antes de chamar o construtor.
  static WebSocketsClient cloud;
  constexpr time_t MIN_TLS_EPOCH = 1600000000;
  const bool hasServoToken = strlen(SERVO_API_TOKEN) == 64;
  const bool hasServerCA = !SERVER_USE_TLS || strstr(ROOT_CA, "-----BEGIN CERTIFICATE-----") != nullptr;
  bool ready = false, begun = false, ntpStarted = false;
  uint32_t requestAt = 0, lastState = 0, lastEvent = 0, lastClock = 0;
  String lastIp;
  CommandClock serverClock;
  cloud.onEvent([&](WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_DISCONNECTED) { ready = false; return; }
    if (type == WStype_CONNECTED) {
      ready = false; requestAt = millis();
      JsonDocument auth; auth["type"] = "auth"; auth["protocol"] = 2; auth["role"] = "servo";
      auth["token"] = SERVO_API_TOKEN; auth["deviceId"] = SERVO_ID; auth["bootId"] = bootId;
      String text; serializeJson(auth, text); cloud.sendTXT(text); return;
    }
    if (type != WStype_TEXT || length > 4096) return;
    JsonDocument message; if (deserializeJson(message, payload, length)) return;
    const char* kind = message["type"] | "";
    if (strcmp(kind, "error") == 0) { Serial.printf("Servidor: %s\n", message["message"] | "erro de protocolo"); return; }
    if (strcmp(kind, "ready") == 0 || strcmp(kind, "clock") == 0) {
      const uint32_t now = millis(); serverClock.sync(message["serverTime"].as<int64_t>(), now, uint32_t(now - requestAt));
      if (!ready && strcmp(kind, "ready") == 0) Serial.println("SERVIDOR: autenticado.");
      ready = true; return;
    }
    if (!ready) return;
    if (strcmp(kind, "event_ack") == 0) outbox.ack(message["id"] | "");
    else if (strcmp(kind, "command") == 0) {
      const char* id = message["id"] | ""; const char* action = message["action"] | "";
      const int64_t remaining = serverClock.remaining(message["expiresAt"].as<int64_t>(), millis());
      if (!validWireId(id) || remaining <= 0 || remaining > 5000 || (strcmp(action, "open") && strcmp(action, "close"))) return;
      ServoCommand command = {}; strlcpy(command.id, id, sizeof(command.id)); command.open = strcmp(action, "open") == 0;
      command.validUntil = millis() + uint32_t(remaining); xQueueSend(commandQueue, &command, 0);
    }
  });
  for (;;) {
    const bool wifi = WiFi.status() == WL_CONNECTED;
    if (wifi) {
      const String ip = WiFi.localIP().toString();
      if (ip != lastIp) {
        lastIp = ip;
        Serial.printf("IP DO SERVO: %s. Conexao com servidor: %s:%u.\n", ip.c_str(), SERVER_HOST, unsigned(SERVER_PORT));
        if (!hasServoToken) Serial.println("SERVIDOR: confira os 64 caracteres de SERVO_API_TOKEN.");
        if (!hasServerCA) Serial.println("WSS: confira o certificado PEM em root_ca.h.");
      }
      if (!ntpStarted) {
        // Enderecos oficiais do NTP da Cloudflare. Usar IPs evita consultas DNS do
        // cliente NTP enquanto o core Wi-Fi resolve o dominio do WebSocket.
        configTime(0, 0, "162.159.200.1", "162.159.200.123");
        ntpStarted = true;
        if (SERVER_USE_TLS && time(nullptr) <= MIN_TLS_EPOCH) Serial.println("WSS: aguardando a hora via NTP.");
      }
      if (!begun && hasServoToken && hasServerCA && (!SERVER_USE_TLS || time(nullptr) > MIN_TLS_EPOCH)) {
        Serial.println(SERVER_USE_TLS ? "WSS: iniciando conexao com o servidor." : "WS: iniciando conexao com o servidor.");
        if (SERVER_USE_TLS) cloud.beginSslWithCA(SERVER_HOST, SERVER_PORT, "/ws", ROOT_CA);
        else cloud.begin(SERVER_HOST, SERVER_PORT, "/ws");
        cloud.setReconnectInterval(3000); cloud.enableHeartbeat(5000, 2000, 2); begun = true;
      }
      if (begun) cloud.loop();
    }
    if (ready && wifi) {
      if (uint32_t(millis() - lastState) >= 200) {
        lastState = millis(); GateSnapshot state = {}; PeerHealth health = {};
        xQueuePeek(stateQueue, &state, 0); xQueuePeek(healthQueue, &health, 0);
        JsonDocument packet; packet["type"] = "telemetry";
        packet["data"]["state"] = state.state; packet["data"]["angle"] = state.angle;
        packet["data"]["targetAngle"] = state.targetAngle; packet["data"]["panelOnline"] = health.online;
        packet["data"]["outboxCount"] = outbox.size();
        if (state.commandId[0]) { packet["data"]["commandId"] = state.commandId; packet["data"]["commandPhase"] = state.commandPhase; }
        String text; serializeJson(packet, text); cloud.sendTXT(text);
      }
      if (uint32_t(millis() - lastEvent) >= 400) {
        lastEvent = millis(); const String queued = outbox.next();
        if (!queued.isEmpty()) {
          JsonDocument event, packet;
          if (!deserializeJson(event, queued)) { packet["type"] = "event"; packet["event"].set(event.as<JsonObject>()); String text; serializeJson(packet, text); cloud.sendTXT(text); }
        }
      }
      if (uint32_t(millis() - lastClock) >= 30000) { lastClock = millis(); requestAt = millis(); cloud.sendTXT("{\"type\":\"clock_request\"}"); }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void execute(const ServoCommand& command) {
  const uint32_t now = millis();
  if (!strcmp(lastCommandId, command.id)) {
    if (!seenCommandThisBoot) { saveServoEvent("command_rejected", command.id, "Comando anterior ao reboot; nao sera repetido."); seenCommandThisBoot = true; }
    return;
  }
  if (int32_t(now - command.validUntil) >= 0) { saveServoEvent("command_rejected", command.id, "Comando expirado."); return; }
  if (gate.moving()) { saveServoEvent("command_rejected", command.id, "Servo em movimento."); return; }
  if (!prefs.putString("lastcmd", command.id)) { saveServoEvent("command_rejected", command.id, "Falha no NVS."); return; }
  strlcpy(lastCommandId, command.id, sizeof(lastCommandId)); seenCommandThisBoot = true;
  strlcpy(reportId, command.id, sizeof(reportId));
  const auto result = gate.start(command.open, now);
  strlcpy(reportPhase, result == GateController::STARTED ? "running" : "completed", sizeof(reportPhase));
  publishState();
  saveServoEvent(result == GateController::STARTED ? "command_started" : "command_done", command.id, command.open ? "open" : "close");
}
void setup() {
  Serial.begin(115200); snprintf(bootId, sizeof(bootId), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  stateQueue = xQueueCreate(1, sizeof(GateSnapshot)); commandQueue = xQueueCreate(4, sizeof(ServoCommand)); healthQueue = xQueueCreate(1, sizeof(PeerHealth));
  if (!stateQueue || !commandQueue || !healthQueue) { Serial.println("Sem memoria"); while (true) delay(1000); }
  outbox.begin();
  if (!prefs.begin("portao-servo", false)) { Serial.println("NVS indisponivel"); while (true) delay(1000); }
  strlcpy(lastCommandId, prefs.getString("lastcmd", "").c_str(), sizeof(lastCommandId));
  servoOutput.begin(); publishState(); PeerHealth health = {}; xQueueOverwrite(healthQueue, &health);
  saveServoEvent("boot", nullptr, "Servo iniciou na posicao fechada comandada; estimativa sem sensor.");
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (xTaskCreatePinnedToCore(peerTask, "painel-uart", 8192, nullptr, 1, nullptr, 0) != pdPASS ||
      xTaskCreatePinnedToCore(cloudTask, "servidor-ws", 12288, nullptr, 1, nullptr, 0) != pdPASS) { Serial.println("Falha nas tarefas"); while (true) delay(1000); }
  Serial.println("ESP32 SERVO pronto. O = abrir; X = fechar pelo Serial para teste local.");
  Serial.printf("UART2 painel: GPIO %d TX, GPIO %d RX, %lu baud.\n", peer_uart::SERVO_TX_PIN, peer_uart::SERVO_RX_PIN, (unsigned long)peer_uart::BAUD_RATE);
}
void loop() {
  if (Serial.available()) {
    const char input = Serial.read();
    if (input == 'O' || input == 'o' || input == 'X' || input == 'x') {
      ServoCommand c = {}; snprintf(c.id, sizeof(c.id), "local-%s-%08lx", bootId, (unsigned long)esp_random());
      c.open = input == 'O' || input == 'o'; c.validUntil = millis() + 5000; execute(c);
    }
  }
  ServoCommand command; if (xQueueReceive(commandQueue, &command, 0) == pdTRUE) execute(command);
  const int before = gate.angle();
  if (gate.tick(millis())) {
    strlcpy(reportPhase, "completed", sizeof(reportPhase)); strlcpy(completionId, reportId, sizeof(completionId));
    saveServoEvent(gate.state() == GateController::OPEN ? "gate_opened" : "gate_closed", reportId); publishState();
  }
  if (before != gate.angle()) { servoOutput.write(gate.angle()); publishState(); }
  delay(1);
}
