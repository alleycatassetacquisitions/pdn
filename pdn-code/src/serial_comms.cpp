// SERIAL
// GAME COMMS

#include <Arduino.h>
#include <HardwareSerial.h>

#include "../include/player.hpp"
extern Player playerInfo;

bool gameCommsAvailable() {
  bool isHunter = playerInfo.isHunter();
  return (isHunter && (Serial1.available() > 0)) ||
         (!isHunter && (Serial2.available() > 0));
}

void writeGameComms(byte command) {
  bool isHunter = playerInfo.isHunter();
  if (isHunter) {
    Serial1.write(command);
  } else {
    Serial2.write(command);
  }
}

void writeGameString(String command) {
  bool isHunter = playerInfo.isHunter();
  if (isHunter) {
    Serial1.println(command);
  } else {
    Serial2.println(command);
  }
}

byte readGameComms() {
  bool isHunter = playerInfo.isHunter();
  if (isHunter) {
    return Serial1.read();
  }
  // else they are a bounty.
  return Serial2.read();
}

String readGameString(char terminator) {
  bool isHunter = playerInfo.isHunter();
  if(isHunter) 
  {
    return Serial1.readStringUntil(terminator);
  } 
    
  return Serial2.readStringUntil(terminator);
}

byte peekGameComms() {
  bool isHunter = playerInfo.isHunter();
  if (isHunter) {
    return Serial1.peek();
  }

  return Serial2.peek();
}

//DEBUG SERIAL
bool debugCommsAvailable() {
  return Serial1.available() > 0;
}

void writeDebugString(String command) {
  Serial1.println(command);
}

String readDebugString(char terminator) {
    return Serial1.readStringUntil(terminator);
}

byte peekDebugComms() {
  return Serial1.peek();
}

void writeDebugByte(byte data) {
  Serial1.write(data);
}

//STREAM UTILS
void clearComms()
{
  Serial1.flush();
  Serial2.flush();
}
