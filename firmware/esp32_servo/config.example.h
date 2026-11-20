#pragma once
#include <stdint.h>

constexpr char WIFI_SSID[] = "Rede_Bancada_Exemplo";
constexpr char WIFI_PASSWORD[] = "SUBSTITUA_PELA_SENHA_LOCAL";
constexpr uint8_t PIN_SERVO = 13;
constexpr int CLOSED_ANGLE = 0;
constexpr int OPEN_ANGLE = 90;
constexpr uint16_t SERVO_MIN_US = 500;
constexpr uint16_t SERVO_MAX_US = 2400;
constexpr uint32_t SERVO_STEP_MS = 20;
constexpr uint32_t SERVO_SETTLE_MS = 300;

constexpr char PEER_SHARED_KEY[] = "SUBSTITUA_PELA_CHAVE_GERADA";
constexpr char PANEL_ID[] = "painel-01";

constexpr char SERVER_HOST[] = "192.168.50.10";
constexpr uint16_t SERVER_PORT = 3000;
constexpr bool SERVER_USE_TLS = false;
constexpr char SERVO_ID[] = "servo-01";
constexpr char SERVO_API_TOKEN[] = "SUBSTITUA_PELA_CHAVE_DO_SERVO";
