#pragma once
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

// FIXME: These appear to be unused so should be safe to remove
// but it would be best for whomever first added them to be consulted
// if possible.

//void monitorTX();
//void monitorRX();

void writeTxTransaction(byte command);
void writeRxTransaction(byte command);

void clearComms();
