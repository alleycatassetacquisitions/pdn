#pragma once

#include <cstdint>
#include <string>

//MOVE TO ESPNOW-DRIVER
constexpr uint8_t ESPNOW_CHANNEL = 6;

//KEEP AS DEVICE-CONSTANTS MOVED TO PDN.
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

//MOVE TO HAPTICS-DRIVER
constexpr uint8_t VIBRATION_MAX = 255;
constexpr uint8_t VIBRATION_OFF = 0;

//MOVE TO LIGHT-DRIVER
constexpr uint8_t BRIGHTNESS_MAX = 255;
constexpr uint8_t BRIGHTNESS_OFF = 0;

//MOVE TO PDN-LIGHT-CONFIG
constexpr uint8_t numDisplayLights = 13;
constexpr uint8_t numGripLights = 6;

//MOVE TO SERIAL-DRIVER
constexpr uint16_t BAUDRATE = 19200;

//MOVE TO MATCH-MANAGER
constexpr unsigned long DUEL_NO_RESULT_TIME = 123456789;

//MOVE TO SERIAL-DRIVER
const std::string SERIAL_HEARTBEAT = "hb";

const std::string SEND_MAC_ADDRESS = "smac";
const char PORT_SEPARATOR = '#';
const char DEVICE_TYPE_SEPARATOR = 't';

constexpr char STRING_TERM = '\r';
constexpr char STRING_START = '*';

constexpr uint16_t TRANSMIT_QUEUE_MAX_SIZE = 1024;

//MOVE TO CONSTS IN PLAYER-REGISTRATION
const std::string TEST_BOUNTY_ID = "9999";
const std::string TEST_HUNTER_ID = "8888";
const std::string BROADCAST_WIFI = "1111";

const std::string FORCE_MATCH_UPLOAD = "6969";

//MOVE TO STORAGE-DRIVER
constexpr const char* PREF_NAMESPACE = "matches";
constexpr const char* PREF_COUNT_KEY = "count";
constexpr const char* PREF_MATCH_KEY = "match_";  // Will be appended with index
constexpr uint8_t MAX_MATCHES = 255;
