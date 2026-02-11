#pragma once

#include <string>

// Fixed channel for ESP-NOW communication
// All devices must use the same channel for reliable ESP-NOW
constexpr uint8_t ESPNOW_CHANNEL = 6;

constexpr uint8_t primaryButtonPin = 15;
constexpr uint8_t secondaryButtonPin = 16;
constexpr uint8_t motorPin = 17;
constexpr uint8_t RXr = 41;
constexpr uint8_t RXt = 40;
constexpr uint8_t TXt = 39; //MIDDLE BAND ON AUDIO CABLE
constexpr uint8_t TXr = 38; //TIP OF AUDIO CABLE
constexpr uint8_t displayLightsPin = 13;
constexpr uint8_t gripLightsPin = 21;
constexpr uint8_t displayCS = 10;
constexpr uint8_t displayDC = 9;
constexpr uint8_t displayRST = 14;

constexpr uint8_t VIBRATION_MAX = 255;
constexpr uint8_t VIBRATION_OFF = 0;

constexpr uint8_t BRIGHTNESS_MAX = 255;
constexpr uint8_t BRIGHTNESS_OFF = 0;

constexpr uint8_t numDisplayLights = 13;
constexpr uint8_t numGripLights = 6;

constexpr uint16_t BAUDRATE = 19200;

constexpr unsigned long DUEL_NO_RESULT_TIME = 123456789;

// COMMANDS
const std::string BOUNTY_BATTLE_MESSAGE = "bbm";
const std::string HUNTER_BATTLE_MESSAGE = "hbm";
const std::string BOUNTY_SHAKE = "bs";
const std::string HUNTER_SHAKE = "hs";
const std::string HUNTER_HANDSHAKE_FINAL_ACK = "hhfa";
const std::string BOUNTY_HANDSHAKE_FINAL_ACK = "bhfa";
const std::string ZAP = "zap";
const std::string YOU_DEFEATED_ME = "ydm";

const std::string SERIAL_HEARTBEAT = "hb";

const std::string SEND_MATCH_ID = "smid";
const std::string SEND_USER_ID = "suid";
const std::string SEND_MAC_ADDRESS = "smac";

constexpr char STRING_TERM = '\r';
constexpr char STRING_START = '*';

constexpr uint16_t TRANSMIT_QUEUE_MAX_SIZE = 1024;

const std::string TEST_BOUNTY_ID = "9999";
const std::string TEST_HUNTER_ID = "8888";
const std::string BROADCAST_WIFI = "1111";

const std::string FORCE_MATCH_UPLOAD = "6969";

// FDN serial protocol messages
const std::string FDN_DEVICE_ID = "fdn:";
const std::string FDN_ACK = "fack";

// NVS key for device mode persistence
constexpr const char* DEVICE_MODE_KEY = "device_mode";

// NPC result storage
constexpr const char* NPC_RESULT_COUNT_KEY = "npc_count";
constexpr const char* NPC_RESULT_KEY = "npc_res_";
constexpr uint8_t MAX_NPC_RESULTS = 64;

//STORAGE
constexpr const char* PREF_NAMESPACE = "matches";
constexpr const char* PREF_COUNT_KEY = "count";
constexpr const char* PREF_MATCH_KEY = "match_";  // Will be appended with index
constexpr uint8_t MAX_MATCHES = 255;
