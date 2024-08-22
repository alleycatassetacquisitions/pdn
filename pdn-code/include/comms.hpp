#pragma once

// COMMANDS
const byte BOUNTY_BATTLE_MESSAGE = 128;
const byte HUNTER_BATTLE_MESSAGE = 129;
const byte BOUNTY_SHAKE = 130;
const byte HUNTER_SHAKE = 131;
const byte HUNTER_HANDSHAKE_FINAL_ACK = 132;
const byte BOUNTY_HANDSHAKE_FINAL_ACK = 133;
const byte ZAP = 134;
const byte YOU_DEFEATED_ME = 135;

// SERIAL
bool gameCommsAvailable();
void writeGameComms(byte command);
void writeGameString(String command);
byte readGameComms();
String readGameString(char terminator);
byte peekGameComms();

// DEBUG COMMS
bool debugCommsAvailable();
void writeDebugString(String command);
String readDebugString(char terminator);
byte peekDebugComms();
void writeDebugByte(byte command);

void writeTxTransaction(byte command);
void writeRxTransaction(byte command);