#ifndef COMMS_CONSTANTS_H_
#define COMMS_CONSTANTS_H_
/*
 * monkey with a crowbar. Figure out how to do this correctly.
 */
#include <string>

using namespace std;

// COMMANDS
const string BOUNTY_BATTLE_MESSAGE = "bbm";
const string HUNTER_BATTLE_MESSAGE = "hbm";
const string BOUNTY_SHAKE = "bs";
const string HUNTER_SHAKE = "hs";
const string HUNTER_HANDSHAKE_FINAL_ACK = "hhfa";
const string BOUNTY_HANDSHAKE_FINAL_ACK = "bhfa";
const string ZAP = "zap";
const string YOU_DEFEATED_ME = "ydm";

const string SEND_MATCH_ID = "smid";
const string SEND_USER_ID = "suid";

const char STRING_TERM = '\r';
const char STRING_START = '*';

const int TRANSMIT_QUEUE_MAX_SIZE = 1024;

#endif
