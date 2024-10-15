#include <Arduino.h>
#include <arduino-timer.h>
#include <WiFi.h>

#include "../include/simple-timer.hpp"
#include "../lib/pdn-libs/player.hpp"
#include "state-machine.hpp"
#include "../include/device/pdn.hpp"
#include "../include/game/quickdraw.hpp"
#include "../include/id-generator.hpp"

//GAME ROLE
Device* PDN = PDN::GetInstance();
IdGenerator* idGenerator = IdGenerator::GetInstance();
Player* player;
Quickdraw game = Quickdraw(player, PDN);

String DEBUG_MODE_SUBSTR = "";

// DISPLAY
bool displayIsDirty = false;

void drawDebugLabels();
void drawDebugState(char *button1State, char *button2State, char *txData,
                    char *rxData, char *motorSpeed, char *led1Pattern,
                    char *led2Pattern);
void updatePrimaryButtonState();
void updateSecondaryButtonState();
void updateTXState();
void updateRXState();

// BUTTONS
int primaryPresses = 0;
int secondaryPresses = 0;

void primaryButtonClick();

CRGBPalette16 currentPalette = bountyColors;

void animateLights();
void activationIdleAnimation();
bool updateUi(void *);
const unsigned char* getImageForAllegiance(int index);

void updateCountdownState();

// TIMERS
auto uiRefresh = timer_create_default();


String stripWhitespace(String input) {
  String output;
  for (int i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    // Check if the character is not a whitespace
    if (!isspace(c) || c > 127) {
      output += c; // Append non-whitespace character to the output string
    }
  }
  return output;
}

// String dumpMatchesToJson() {
//   StaticJsonDocument<512> doc;
//   JsonArray matchesArray = doc.to<JsonArray>();
//   StaticJsonDocument<128> match;
//   JsonObject matchObj;
//   for (int i = 0; i < numMatches; i++) {
//     matchObj = match.to<JsonObject>();
//     matches[i].fillJsonObject(matchObj);
//     matchesArray.add(matchObj);
//   }
//   String output;
//   serializeJson(matchesArray, output);
//   return output;
// }

String debugOutput = "";

void updateScore(boolean win);

bool requestSwitchAppState();
bool isValidMessageSerial1();
bool isValidMessageSerial2();
bool debugCommandReceived();
bool validateCommand(String a, char b);
String fetchDebugData();
String fetchDebugCommand();
void setupDevice();
void checkInDevice();
void setActivationDelay();
void getDeviceId();


int wins = 0;

void setup(void) {

  PDN->begin();

  WiFi.begin();

  // if (game.playerInfo.isHunter()) {
  //   currentPalette = hunterColors;
  // } else {
  //   currentPalette = bountyColors;
  // }
  //
  // PDN->attachPrimaryButtonClick(primaryButtonClick);
  // PDN->attachSecondaryButtonClick(primaryButtonClick);
  //
  // drawDebugLabels();
  // PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexLogo));
  // PDN->getDisplay().getScreen().drawXBM(64, 0, 128, 64, getImageForAllegiance(indexStamp));
  // PDN->getDisplay().sendBuffer();
  //
  // uiRefresh.every(16, updateUi);
  //
  delay(3000);
}

void loop(void) {
  SimpleTimer::updateTime();
  PDN->loop();

  // if (APP_STATE == AppState::QD_GAME) {
  //   game.quickDrawGame();
  // } else if (APP_STATE == AppState::DEBUG) {
  //   debugEvents();
  // } else if (APP_STATE == AppState::SET_USER) {
  //   game.playerInfo.setUserID(idGenerator->generateId());
  // } else if (APP_STATE == AppState::CLEAR_USER) {
  //   game.playerInfo.clearUserID();
  // }
  // checkForAppState();
  // uiRefresh.tick();
  // animateLights();
  game.loop();
}

// DISPLAY

// void dormantAnimation() {
//   if (game.breatheUp) {
//     game.ledBrightness++;
//   } else {
//     game.ledBrightness--;
//   }
//   game.pwm_val =
//       255.0 * (1.0 - abs((2.0 * (game.ledBrightness / game.smoothingPoints)) - 1.0));
//
//   if (game.ledBrightness == 255) {
//     game.breatheUp = false;
//   } else if (game.ledBrightness == 0) {
//     game.breatheUp = true;
//   }
//
//   PDN->getDisplayLights().setTransmitLight(ColorFromPalette(bountyColors, 0, game.pwm_val, LINEARBLEND));
// }
//
// void activationIdleAnimation() {
//   if (game.breatheUp) {
//     game.ledBrightness++;
//   } else {
//     game.ledBrightness--;
//   }
//   game.pwm_val =
//       255.0 * (1.0 - abs((2.0 * (game.ledBrightness / game.smoothingPoints)) - 1.0));
//
//   if (game.ledBrightness == 255) {
//     game.breatheUp = false;
//   } else if (game.ledBrightness == 0) {
//     game.breatheUp = true;
//   }
//
//   if (random8() % 7 == 0) {
//     PDN->getDisplayLights().addToLight(
//         random8() % (numDisplayLights - 1),
//         ColorFromPalette(currentPalette, random8(), game.pwm_val, LINEARBLEND)
//       );
//   }
//   PDN->getDisplayLights().fade(2);
//
//   for (int i = 0; i < numGripLights; i++) {
//     if (random8() % 65 == 0) {
//       PDN->getGripLights().addToLight(
//         i,
//         ColorFromPalette(currentPalette, random8(), game.pwm_val, LINEARBLEND)
//       );
//     }
//   }
//   PDN->getGripLights().fade(2);
// }
//
// void animateLights() {
//
//   if(APP_STATE == AppState::DEBUG) {
//     EVERY_N_MILLIS(16) {
//       activationIdleAnimation();
//     }
//   } else {
//     switch(QD_STATE) {
//       case QdState::INITIATE :
//         break;
//       case QdState::DORMANT:
//         EVERY_N_MILLIS(16) {
//           dormantAnimation();
//         }
//         break;
//       case QdState::ACTIVATED:
//         EVERY_N_MILLIS(16) {
//           activationIdleAnimation();
//         }
//         break;
//       case QdState::HANDSHAKE:
//         PDN->getDisplayLights().setTransmitLight(ColorFromPalette(currentPalette, 0));
//         break;
//       case QdState::DUEL_ALERT:
//         PDN->getDisplayLights().setTransmitLight(ColorFromPalette(currentPalette, 0));
//         break;
//       case QdState::DUEL_COUNTDOWN:
//         EVERY_N_MILLIS(200) {
//           PDN->getGripLights().fade(3);
//         }
//         updateCountdownState();
//         break;
//       case QdState::DUEL:
//         PDN->setGlobalBrightness(255);
//         PDN->setGlobablLightColor(currentPalette[0]);
//         break;
//       case QdState::WIN:
//         EVERY_N_MILLIS(4) {
//           activationIdleAnimation();
//           if(random8() % 20 < 2) {
//             PDN->getDisplayLights().addToLight(random() % 13, CRGB::White);
//             PDN->getGripLights().addToLight(random() % 6, CRGB::White);
//           }
//         }
//         break;
//       case QdState::LOSE:
//         EVERY_N_MILLIS(750) {
//           PDN->setGlobalBrightness(FastLED.getBrightness()-2);
//         }
//         break;
//     }
//   }
//
//   FastLED.show();
// }
//
// byte screenCounter = 0;
//
// const unsigned char* getImageForAllegiance(int index) {
//   switch(game.playerInfo.getAllegiance()) {
//     case Allegiance::ENDLINE:
//       return endlineImages[index];
//     case Allegiance::HELIX:
//       return helixImages[index];
//     case Allegiance::RESISTANCE:
//       return resistanceImages[index];
//     default:
//       return alleycatImages[index];
//   }
// }
//
// int width = 0;
// int offset = 0;
// String winString = "";
//
// bool updateUi(void *) {
//
//   if(displayIsDirty){
//     PDN->getDisplay().clearBuffer();
//
//     switch(APP_STATE) {
//       case AppState::DEBUG:
//         PDN->getDisplay().getScreen().print("DEBUG: " + DEBUG_MODE_SUBSTR);
//         PDN->getDisplay().getScreen().setCursor(0, 16);
//         PDN->getDisplay().getScreen().print(u8x8_u8toa(screenCounter++, 3));
//         break;
//       case AppState::QD_GAME:
//         switch (QD_STATE) {
//           case QdState::INITIATE:
//             PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexLogo));
//             PDN->getDisplay().getScreen().drawXBM(64, 0, 128, 64, getImageForAllegiance(indexStamp));
//             break;
//
//           case QdState::DORMANT:
//             PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexLogo));
//             PDN->getDisplay().getScreen().drawXBM(64, 0, 128, 64, getImageForAllegiance(indexStamp));
//             break;
//
//           case QdState::ACTIVATED:
//             PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexIdle));
//             PDN->getDisplay().getScreen().setFont(u8g2_font_prospero_nbp_tf);
//             PDN->getDisplay().getScreen().setDrawColor(0);
//             winString = String(wins);
//             if(game.playerInfo.isHunter()) {
//               width = PDN->getDisplay().getScreen().getStrWidth("CAPTURES");
//               offset = 64+(64-width)/2;
//               PDN->getDisplay().getScreen().setCursor(offset, 24);
//               PDN->getDisplay().getScreen().print("CAPTURES");
//             } else {
//               width = PDN->getDisplay().getScreen().getStrWidth("EVADES");
//               offset = 64+(64-width)/2;
//               PDN->getDisplay().getScreen().setCursor(offset, 24);
//               PDN->getDisplay().getScreen().print("EVADES");
//             }
//
//             PDN->getDisplay().getScreen().setFont(u8g2_font_smart_patrol_nbp_tf);
//             width = PDN->getDisplay().getScreen().getUTF8Width(winString.c_str());
//             offset = 64+(64-width)/2;
//             PDN->getDisplay().getScreen().setCursor(92, 50);
//             PDN->getDisplay().getScreen().print(winString);
//
//             PDN->getDisplay().getScreen().setDrawColor(1);
//             break;
//
//           case QdState::HANDSHAKE:
//             PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexConnect));
//             break;
//
//           case QdState::DUEL_ALERT:
//             PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexConnect));
//             break;
//
//           case QdState::DUEL_COUNTDOWN:
//             if(game.countdownStage > 3) {
//               PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexConnect));
//             } else if(game.countdownStage == 3) {
//               PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexThree));
//             } else if(game.countdownStage == 2) {
//               PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexTwo));
//             } else if(game.countdownStage <= 1) {
//               PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexOne));
//             }
//             break;
//
//           case QdState::DUEL:
//             PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexDraw));
//             break;
//
//           case QdState::WIN:
//             PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexWin));
//             break;
//
//           case QdState::LOSE:
//             PDN->getDisplay().getScreen().drawXBM(0, 0, 128, 64, getImageForAllegiance(indexLose));
//             break;
//           }
//     }
//
//     PDN->getDisplay().getScreen().sendBuffer();
//     displayIsDirty = false;
//   }
//
//   return true; // for our timer to continue repeating.
// }
//
// void updateCountdownState() {
//   // if(countdownStage == 3) {
//   //   displayLights[11] = CRGB::Black;
//   //   displayLights[10] = CRGB::Black;
//   //   displayLights[0] = CRGB::Black;
//   //   displayLights[1] = CRGB::Black;
//   // } else if(countdownStage == 2) {
//   //   displayLights[9] = CRGB::Black;
//   //   displayLights[8] = CRGB::Black;
//   //   displayLights[2] = CRGB::Black;
//   //   displayLights[3] = CRGB::Black;
//   // } else if(countdownStage == 1) {
//   //   displayLights[7] = CRGB::Black;
//   //   displayLights[6] = CRGB::Black;
//   //   displayLights[4] = CRGB::Black;
//   //   displayLights[5] = CRGB::Black;
//   // }
// }
//
// void setTransmitLight(boolean on) { PDN->getDisplayLights().setTransmitLight(on); }
//
// void checkForAppState() {
//
//   if (requestSwitchAppState()) {
//     if (debugCommsAvailable()) {
//       String command = fetchDebugCommand();
//       if (validateCommand(command, ENTER_DEBUG)) {
//         APP_STATE = AppState::DEBUG;
//         writeDebugByte(DEBUG_DELIMITER);
//         currentPalette = idleColors;
//         resetState();
//       } else if (validateCommand(command, START_GAME) && APP_STATE == AppState::DEBUG) {
//         if (game.playerInfo.isHunter()) {
//           currentPalette = hunterColors;
//         } else {
//           currentPalette = bountyColors;
//         }
//         APP_STATE = AppState::QD_GAME;
//         QD_STATE = QdState::DORMANT;
//         resetState();
//       }
//     }
//   }
//
//   PDN->flushComms();
// }
//
// void debugEvents() {
//   // todo: Debug Display
//
//   if (debugCommandReceived()) {
//     String command = fetchDebugCommand();
//     if (validateCommand(command, SETUP_DEVICE)) {
//       DEBUG_MODE_SUBSTR = "SETUP";
//       setupDevice();
//     } else if (validateCommand(command, CHECK_IN)) {
//       DEBUG_MODE_SUBSTR = "CHECK_IN";
//       checkInDevice();
//     } else if (validateCommand(command, SET_ACTIVATION)) {
//       DEBUG_MODE_SUBSTR = "SET_ACTIVATION";
//       setActivationDelay();
//     } else if(validateCommand(command, GET_DEVICE_ID)) {
//       getDeviceId();
//     }
//     else {
//       DEBUG_MODE_SUBSTR = "wait";
//     }
//   }
// }
//
// void updateScore(boolean win) {
//   addMatch(win);
// }
//
// bool requestSwitchAppState() {
//   if (debugCommsAvailable()) {
//     char command = (char)peekDebugComms();
//     // Serial.print("Checking App State: ");
//     // Serial.print(" Current buffer size: ");
//     // Serial.print(debugCommsAvailable());
//     // Serial.print(" Command: ");
//     // Serial.print(command);
//     // Serial.print(" As Byte: ");
//     // Serial.println(peekDebugComms());
//     return (command == ENTER_DEBUG || command == START_GAME);
//   } else {
//     return false;
//   }
// }
//
// bool debugCommandReceived() {
//   if (debugCommsAvailable()) {
//     char command = (char)peekDebugComms();
//     return (command == SETUP_DEVICE || command == SET_ACTIVATION ||
//             command == CHECK_IN || command == GET_DEVICE_ID);
//   } else {
//     return false;
//   }
// }
//
// bool validateCommand(String a, char b) {
//   char array[a.length() + 1];
//   a.toCharArray(array, a.length() + 1);
//   return b == array[0];
// }
//
// String fetchDebugData() { return readDebugString('\n'); }
//
// String fetchDebugCommand() { return readDebugString(DEBUG_DELIMITER); }

// void setupDevice() {
//   if(debugCommsAvailable) {
//     writeDebugByte(DEBUG_DELIMITER);
//
//     String playerJson = readDebugString('\n');
//     playerJson = stripWhitespace(playerJson);
//     game.setPlayerInfo(playerJson);
//
//     memset(matches, 0, sizeof(matches));
//     wins = 0;
//
//     writeDebugByte(DEBUG_DELIMITER);
//   }
// }

String mac2String(byte ar[]) {
  String s;
  for (byte i = 0; i < 6; ++i)
  {
    char buf[3];
    sprintf(buf, "%02X", ar[i]); // J-M-L: slight modification, added the 0 in the format for padding 
    s += buf;
    if (i < 5) s += ':';
  }
  return s;
}
//
// void getDeviceId() {
//   writeDebugByte(DEBUG_DELIMITER);
//   // uint8_t mac[6];
//   // esp_read_mac(mac, ESP_MAC_WIFI_STA);
//   writeDebugString(WiFi.macAddress());
//   writeDebugByte(DEBUG_DELIMITER);
//
// }
//
// void checkInDevice() {
//   // Print JSON string to serial
//   String jsonStr = dumpMatchesToJson();
//   writeDebugByte(DEBUG_DELIMITER);
//   writeDebugString(jsonStr);
//   writeDebugByte(DEBUG_DELIMITER);
// }

// void setActivationDelay() {
//   if (debugCommsAvailable()) {
//     String activationDelay = fetchDebugData();
//     debugDelay = strtoul(activationDelay.c_str(), NULL, 10);
//     // Serial.print("Set Activation Delay: ");
//     // Serial.println(debugDelay);
//   }
// }

// void drawDebugLabels() {
//   PDN->getDisplay().getScreen().drawStr(16, 10, "BTN 1:");
//   PDN->getDisplay().getScreen().drawStr(16, 20, "BTN 2:");
//   PDN->getDisplay().getScreen().drawStr(16, 30, "TX:");
//   PDN->getDisplay().getScreen().drawStr(16, 40, "RX:");
//   PDN->getDisplay().getScreen().drawStr(16, 50, "MOTOR:");
//   PDN->getDisplay().getScreen().drawStr(16, 60, "FRAME:");
// }
//
// void drawDebugState(char *button1State, char *button2State, char *txData,
//                     char *rxData, char *motorSpeed, char *led1Pattern,
//                     char *fps) {
//   PDN->getDisplay().getScreen().drawStr(80, 10, button1State);
//   PDN->getDisplay().getScreen().drawStr(80, 20, button2State);
//   PDN->getDisplay().getScreen().drawStr(80, 30, txData);
//   PDN->getDisplay().getScreen().drawStr(80, 40, rxData);
//   PDN->getDisplay().getScreen().drawStr(80, 50, motorSpeed);
//   PDN->getDisplay().getScreen().drawStr(80, 60, fps);
// }
//
// void updatePrimaryButtonState() {
//   PDN->getDisplay().getScreen().setCursor(2, 48);
//   PDN->getDisplay().getScreen().print(u8x8_u8toa(primaryPresses, 3));
// }
//
// void updateSecondaryButtonState() {
//   PDN->getDisplay().getScreen().setCursor(80, 20);
//   PDN->getDisplay().getScreen().print(u8x8_u8toa(secondaryPresses, 3));
// }

// BUTTONS