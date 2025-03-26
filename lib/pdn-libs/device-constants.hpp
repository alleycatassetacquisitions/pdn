#pragma once

#include <string>

using namespace std;

const int primaryButtonPin = 15;
const int secondaryButtonPin = 16;
const int motorPin = 17;
const int RXr = 41;
const int RXt = 40;
const int TXt = 39; //MIDDLE BAND ON AUDIO CABLE
const int TXr = 38; //TIP OF AUDIO CABLE
const int displayLightsPin = 13;
const int gripLightsPin = 21;
const int displayCS = 10;
const int displayDC = 9;
const int displayRST = 14;

const int VIBRATION_MAX = 255;
const int VIBRATION_OFF = 0;

const int BRIGHTNESS_MAX = 255;
const int BRIGHTNESS_OFF = 0;

const int numDisplayLights = 13;
const int numGripLights = 6;

const int BAUDRATE = 19200;

const unsigned long DUEL_NO_RESULT_TIME = 123456789;

// COMMANDS
const string BOUNTY_BATTLE_MESSAGE = "bbm";
const string HUNTER_BATTLE_MESSAGE = "hbm";
const string BOUNTY_SHAKE = "bs";
const string HUNTER_SHAKE = "hs";
const string HUNTER_HANDSHAKE_FINAL_ACK = "hhfa";
const string BOUNTY_HANDSHAKE_FINAL_ACK = "bhfa";
const string ZAP = "zap";
const string YOU_DEFEATED_ME = "ydm";

const string SERIAL_HEARTBEAT = "hb";

const string SEND_MATCH_ID = "smid";
const string SEND_USER_ID = "suid";
const string SEND_MAC_ADDRESS = "smac";

const char STRING_TERM = '\r';
const char STRING_START = '*';

const int TRANSMIT_QUEUE_MAX_SIZE = 1024;

const string TEST_BOUNTY_ID = "9999";
const string TEST_HUNTER_ID = "8888";