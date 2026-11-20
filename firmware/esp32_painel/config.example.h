#pragma once
#include <stdint.h>

constexpr char PANEL_ID[] = "painel-01";
constexpr char PEER_SHARED_KEY[] = "SUBSTITUA_PELA_CHAVE_GERADA";

constexpr uint8_t PIN_TFT_CS = 21, PIN_TFT_DC = 17, PIN_TFT_RST = 16;
constexpr uint8_t PIN_TFT_MOSI = 23, PIN_TFT_SCK = 18;
constexpr uint8_t PIN_BUTTON = 14, PIN_RELAY = 26;
constexpr uint8_t PIN_ACTIVE_BUZZER = 25, PIN_PASSIVE_BUZZER = 32;
constexpr bool RELAY_ACTIVE_LOW = true;
constexpr bool ACTIVE_BUZZER_ACTIVE_HIGH = true;
constexpr uint32_t BELL_COOLDOWN_MS = 2000;
