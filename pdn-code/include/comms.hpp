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

void writeTxTransaction(byte command);
void writeRxTransaction(byte command);

void clearComms();
