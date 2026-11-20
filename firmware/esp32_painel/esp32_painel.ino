/* ESP32 do painel KJW: display, botao, rele/LED e dois buzzers.
 * UART2 com o ESP32 do servo: GPIO 27 RX, GPIO 33 TX e GND comum.
 * Arduino-ESP32 3.x, ArduinoJson 7, Adafruit GFX e Adafruit ST7735/ST7789.
 */
#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_arduino_version.h>
#include <esp_system.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sys/time.h>
#include "config.h"
#include "Outbox.h"
#include "InputTimers.h"
#include "ChiptunePlayer.h"
#include "BuzzerOutput.h"
#include "PanelIndicator.h"
#include "PeerUart.h"
#include "PanelDisplay.h"

#if ESP_ARDUINO_VERSION_MAJOR < 3 || !defined(CONFIG_IDF_TARGET_ESP32)
#error "Use ESP32 classico WROOM-32 com pacote Espressif 3.x."
#endif

Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
painel_lcd::PanelDisplay lcd(tft);
Preferences lcdPrefs;
bool lcdPrefsReady = false;
time_t lastBellAt = 0;
Outbox outbox;
PanelIndicator indicator;
panelio::ButtonDebouncer button;
ChiptunePlayer chip;
BuzzerOutput buzzer;
panelio::PulseTimer beepTimer, probeTimer, bellTimer;
QueueHandle_t packetQueue;
char bootId[24];
uint32_t eventSequence = 0;
bool historyError = false, soundError = false;
enum AudioMode { SILENT, CHIPTUNE, PROBE };
AudioMode audioMode = SILENT;

void toneOutput(uint16_t frequency) {
    if (!buzzer.tone(frequency)) {
        soundError = true;
        Serial.printf("ERRO PWM: GPIO %u, frequencia %u Hz.\n", unsigned(PIN_PASSIVE_BUZZER), unsigned(frequency));
    }
}

void playChiptune() {
    audioMode = CHIPTUNE;
    chip.start(millis());
    toneOutput(chip.frequency());
    Serial.printf("CAMPAINHA 8 BITS: %u notas, %lu ms, GPIO %u.\n", unsigned(CHIP_COUNT),
                  (unsigned long)ChiptunePlayer::durationMs(), unsigned(PIN_PASSIVE_BUZZER));
}

void updateAudio() {
    const uint32_t now = millis();
    if (audioMode == CHIPTUNE && chip.update(now)) {
        toneOutput(chip.frequency());
        if (!chip.playing()) {
            audioMode = SILENT;
            Serial.println("Fim da campainha 8 bits.");
        }
    } else if (audioMode == PROBE && !probeTimer.active(now)) {
        toneOutput(0);
        audioMode = SILENT;
    }
    digitalWrite(PIN_ACTIVE_BUZZER, beepTimer.active(now) == ACTIVE_BUZZER_ACTIVE_HIGH ? HIGH : LOW);
}

void saveDoorbell() {
    JsonDocument event;
    char id[64];
    snprintf(id, sizeof(id), "p-%s-%08lx", bootId, (unsigned long)++eventSequence);
    event["id"] = id;
    event["type"] = "doorbell";
    event["source"] = "panel";
    stampEvent(event);
    if (!outbox.save(event)) {
        historyError = true;
        Serial.println("ERRO: toque nao gravado na fila local.");
    }

    const time_t when = static_cast<time_t>(event["occurredAt"].as<int64_t>() / 1000);
    if (when != lastBellAt) {
        lastBellAt = when;
        if (lcdPrefsReady && lcdPrefs.putLong64("lastbell", lastBellAt) != sizeof(int64_t)) {
            Serial.println("LCD: nao foi possivel salvar a hora do ultimo toque.");
        }
    }
}

void peerTask(void*) {
    Serial2.setRxBufferSize(peer_uart::RX_BUFFER_BYTES);
    Serial2.begin(peer_uart::BAUD_RATE, SERIAL_8N1, peer_uart::PANEL_RX_PIN, peer_uart::PANEL_TX_PIN);
    static peer_uart::LineReader receiver;
    char servoBootId[peer_uart::BOOT_ID_LENGTH + 1] = {};
    uint32_t lastHello = millis() - peer_uart::HELLO_INTERVAL_MS;
    uint32_t lastReady = 0, lastEvent = 0;
    for (;;) {
        if (servoBootId[0] && !peer_uart::isFresh(millis(), lastReady)) {
            servoBootId[0] = '\0';
            const PanelPacket offline = {};
            xQueueOverwrite(packetQueue, &offline);
            Serial.println("SERVO UART: sem resposta.");
        }
        while (receiver.read(Serial2)) {
            JsonDocument message;
            if (deserializeJson(message, receiver.data(), receiver.size())) continue;
            const char* kind = message["type"] | "";
            if (strcmp(kind, "peer_ready") == 0) {
                const char* remoteBootId = message["servoBootId"] | "";
                if (message["protocol"].as<int>() != peer_uart::PROTOCOL_VERSION ||
                    strcmp(message["panelBootId"] | "", bootId) ||
                    strlen(remoteBootId) != peer_uart::BOOT_ID_LENGTH) continue;
                if (strcmp(servoBootId, remoteBootId)) Serial.println("SERVO UART: conectado.");
                strlcpy(servoBootId, remoteBootId, sizeof(servoBootId));
                lastReady = millis();
                const int64_t seconds = message["epochSeconds"].as<int64_t>();
                if (seconds > 1600000000 && seconds != time(nullptr)) {
                    const struct timeval clock = {static_cast<time_t>(seconds), 0};
                    settimeofday(&clock, nullptr);
                }
                continue;
            }
            if (!servoBootId[0]) continue;
            if (strcmp(kind, "peer_event_ack") == 0) {
                outbox.ack(message["id"] | "");
            } else if (strcmp(kind, "gate_state") == 0) {
                const JsonObjectConst data = message["data"].as<JsonObjectConst>();
                const char* state = data["state"] | "";
                if (strcmp(state, "closed") && strcmp(state, "opening") &&
                    strcmp(state, "open") && strcmp(state, "closing")) continue;
                PanelPacket packet = {};
                packet.link = true;
                packet.receivedAt = millis();
                strlcpy(packet.state, state, sizeof(packet.state));
                packet.angle = data["angle"] | 0;
                packet.targetAngle = data["targetAngle"] | 0;
                strlcpy(packet.completionId, data["completionId"] | "", sizeof(packet.completionId));
                xQueueOverwrite(packetQueue, &packet);
            }
        }

        const uint32_t now = millis();
        if (uint32_t(now - lastHello) >= peer_uart::HELLO_INTERVAL_MS) {
            lastHello = now;
            JsonDocument hello;
            hello["type"] = "peer_auth";
            hello["protocol"] = peer_uart::PROTOCOL_VERSION;
            hello["panelId"] = PANEL_ID;
            hello["key"] = PEER_SHARED_KEY;
            hello["panelBootId"] = bootId;
            hello["seenServoBootId"] = servoBootId;
            peer_uart::send(Serial2, hello);
        }
        if (servoBootId[0] && uint32_t(now - lastEvent) >= 500) {
            lastEvent = now;
            const String pending = outbox.next();
            JsonDocument event;
            if (!pending.isEmpty() && !deserializeJson(event, pending)) {
                JsonDocument packet;
                packet["type"] = "peer_event";
                packet["event"].set(event.as<JsonObjectConst>());
                peer_uart::send(Serial2, packet);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void setup() {
    Serial.begin(115200);
    snprintf(bootId, sizeof(bootId), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? HIGH : LOW);
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_ACTIVE_BUZZER, ACTIVE_BUZZER_ACTIVE_HIGH ? LOW : HIGH);
    pinMode(PIN_ACTIVE_BUZZER, OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    if (!buzzer.begin(PIN_PASSIVE_BUZZER)) {
        Serial.println("ERRO: PWM do buzzer nao iniciou");
        while (true) delay(1000);
    }

    packetQueue = xQueueCreate(1, sizeof(PanelPacket));
    if (!packetQueue) {
        Serial.println("Sem memoria");
        while (true) delay(1000);
    }
    SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
    tft.init(240, 320);
    tft.setRotation(1);
    lcdPrefsReady = lcdPrefs.begin("painel-lcd", false);
    if (lcdPrefsReady) lastBellAt = static_cast<time_t>(lcdPrefs.getLong64("lastbell", 0));
    lcd.begin();
    lcd.update(millis(), {time(nullptr), lastBellAt, nullptr, false, false, false});

    historyError = !outbox.begin();
    if (xTaskCreatePinnedToCore(peerTask, "servo-uart", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
        Serial.println("Falha na tarefa UART");
        while (true) delay(1000);
    }
    Serial.println("PAINEL PRONTO: C=campainha 8 bits; T=tom de 2 kHz por 2 s; S=parar. Serial 115200.");
    Serial.printf("Botao: GPIO %u -> GND. Buzzer passivo: GPIO %u -> GND.\n", unsigned(PIN_BUTTON), unsigned(PIN_PASSIVE_BUZZER));
    Serial.printf("UART2 servo: GPIO %d TX, GPIO %d RX, %lu baud.\n", peer_uart::PANEL_TX_PIN,
                  peer_uart::PANEL_RX_PIN, (unsigned long)peer_uart::BAUD_RATE);
}

void loop() {
    updateAudio();
    if (Serial.available()) {
        const char c = Serial.read();
        if (c == 'C' || c == 'c') playChiptune();
        else if (c == 'T' || c == 't') {
            chip.stop();
            audioMode = PROBE;
            probeTimer.start(millis(), 2000);
            toneOutput(2000);
            Serial.println("TESTE: onda quadrada de 2 kHz, 50%, GPIO 32, por 2 segundos.");
        } else if (c == 'S' || c == 's') {
            chip.stop();
            audioMode = SILENT;
            toneOutput(0);
        }
    }

    PanelPacket packet;
    while (xQueueReceive(packetQueue, &packet, 0) == pdTRUE) {
        if (indicator.apply(packet)) beepTimer.start(millis(), 250);
    }
    digitalWrite(PIN_RELAY, indicator.ledOn() == RELAY_ACTIVE_LOW ? LOW : HIGH);
    static uint32_t lastBell = uint32_t(0) - BELL_COOLDOWN_MS;
    if (button.update(digitalRead(PIN_BUTTON) == LOW, millis()) &&
        uint32_t(millis() - lastBell) >= BELL_COOLDOWN_MS) {
        lastBell = millis();
        Serial.println("BOTAO: campainha pressionada.");
        playChiptune();
        bellTimer.start(millis(), ChiptunePlayer::durationMs() + 500);
        saveDoorbell();
    }

    const uint32_t now = millis();
    const char* gateState = indicator.live(now) ? indicator.snapshot().state : nullptr;
    lcd.update(now, {time(nullptr), lastBellAt, gateState, bellTimer.active(now), historyError, soundError});
    delay(1);
}
