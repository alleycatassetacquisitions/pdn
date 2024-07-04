// SERIAL
// GAME COMMS

#include <Arduino.h>
#include <HardwareSerial.h>

#include "../include/comms.hpp"

#include "../include/player.hpp"
#include "../include/states.hpp"
extern Player playerInfo;

/*
  the two validation methods are quite similar, with some notable differences.
  Serial1 is used for hunter communications and for debug events.
  Serial2 is used for bounty communications.

  Debug events on Serial2 are assumed to be invalid.
  
  During the quick draw game, we validate the battle and handshake messages against what role
  the device is currently in.
*/
static bool isValidMessageSerial1() {
  byte command = Serial1.peek();
  if ((char)command == ENTER_DEBUG || (char)command == START_GAME) {
    return true;
  }

  if (APP_STATE == AppState::DEBUG) {
    // Serial.print("Validating DEBUG: ");
    // Serial.println((char)command);
    return ((char)command == SETUP_DEVICE || (char)command == SET_ACTIVATION ||
            (char)command == CHECK_IN || (char)command == DEBUG_DELIMITER  || (char)command == GET_DEVICE_ID);
  } else {
    if (QD_STATE == QdState::ACTIVATED) { 
      return (command == BOUNTY_BATTLE_MESSAGE && playerInfo.isHunter());
    } else if (QD_STATE == QdState::HANDSHAKE) {
      return (command == BOUNTY_SHAKE && handshakeState < HandshakeState::HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE) ||
        (command == BOUNTY_HANDSHAKE_FINAL_ACK);
    } else if (QD_STATE == QdState::DUEL) {
      return (command == ZAP || command == YOU_DEFEATED_ME);
    }
  }

  return false;
}

static bool isValidMessageSerial2() {
  byte command = Serial2.peek();
    if (QD_STATE == QdState::ACTIVATED) {
      return (command == HUNTER_BATTLE_MESSAGE && !playerInfo.isHunter());
    } else if (QD_STATE == QdState::HANDSHAKE) {
      return (command == HUNTER_SHAKE && handshakeState < HandshakeState::HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE) || 
        (command == HUNTER_HANDSHAKE_FINAL_ACK);
    } else if (QD_STATE == QdState::DUEL) {
      return (command == ZAP || command == YOU_DEFEATED_ME);
    }
  // }

  return false;
}

//PUBLIC APIs
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

void flushComms() {
  while (Serial1.available() > 0 && !isValidMessageSerial1()) {
    Serial1.read();
  }

  while (Serial2.available() > 0 && !isValidMessageSerial2()) {
    Serial2.read();
  }
}
