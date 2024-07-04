#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <OneButton.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <arduino-timer.h>
#include <UUID.h>
#include <images.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "../include/simple-timer.hpp"
#include "../include/player.hpp"
#include "../include/match.hpp"
#include "../include/comms.hpp"
#include "../include/states.hpp"

#define primaryButtonPin 15
#define secondaryButtonPin 16
#define motorPin 17
#define RXr 41
#define RXt 40
#define TXt 39 //MIDDLE BAND ON AUDIO CABLE
#define TXr 38 //TIP OF AUDIO CABLE
#define displayLightsPin 13
#define gripLightsPin 21
#define displayCS 10
#define displayDC 9
#define displayRST 14

#define numDisplayLights 13
#define numGripLights 6

const int BAUDRATE = 19200;

//GAME ROLE
Player playerInfo;

String deviceID = "";
String DEBUG_MODE_SUBSTR = "";

// DISPLAY

U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display(U8G2_R0, displayCS, displayDC,
                                               displayRST);

unsigned long frameBegin = 0;
unsigned long frameDuration = 0;
bool displayIsDirty = false;

void drawDebugLabels();
void drawDebugState(char *button1State, char *button2State, char *txData,
                    char *rxData, char *motorSpeed, char *led1Pattern,
                    char *led2Pattern);
void updatePrimaryButtonState();
void updateSecondaryButtonState();
void updateMotorState();
void updateTXState();
void updateRXState();
void updateFramerate();

// BUTTONS

OneButton primary = OneButton(primaryButtonPin, true, true);
OneButton secondary = OneButton(secondaryButtonPin, true, true);

int primaryPresses = 0;
int secondaryPresses = 0;

void primaryButtonClick();
void primaryButtonDoubleClick();
void primaryButtonLongPress();

void secondaryButtonClick();
void secondaryButtonDoubleClick();
void secondaryButtonLongPress();

bool isButtonPressed();

// LEDs

CRGB displayLights[numDisplayLights];
boolean displayLightsOnOff[numDisplayLights] = {true, true, true, true, true,
                                                true, true, true, true, true,
                                                true, true, true};
CRGB gripLights[numGripLights];
boolean gripLightsOnOff[numGripLights] = {true, true, true, true, true, true};

uint8_t displayColorIndex = 0;
uint8_t gripColorIndex = 0;

CRGBPalette16 bountyColors = CRGBPalette16(
    CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Orange, CRGB::Red, CRGB::Red,
    CRGB::Red, CRGB::Orange, CRGB::Orange, CRGB::Red, CRGB::Red, CRGB::Red,
    CRGB::Orange, CRGB::Red, CRGB::Red, CRGB::Red);

CRGBPalette16 hunterColors = CRGBPalette16(
    CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen,
    CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen);

CRGBPalette16 idleColors = CRGBPalette16(
  CRGB::DarkGreen, CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkBlue, 
  CRGB::Red, CRGB::Yellow, CRGB::Red, CRGB::Yellow, 
  CRGB::DarkGreen, CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkBlue, 
  CRGB::Red, CRGB::Yellow, CRGB::Red, CRGB::Yellow
);

CRGBPalette16 currentPalette = bountyColors;

void animateLights();
void activationIdleAnimation();
bool updateUi(void *);
const unsigned char* getImageForAllegiance(int index);

void setGraphRight(int value);
void setGraphLeft(int value);
void updateCountdownState();
void setTransmitLight(boolean on);

// MOTOR

void setMotorOutput(int value);
int motorSpeed = 0;

//FIXME: This requires stream validation so can't easily
//be moved to separate file yet, but should eventually be.
void flushComms();

byte lastTxPacket = 0;
byte lastRxPacket = 0;

const byte BOUNTY_BATTLE_MESSAGE = 128;
const byte HUNTER_BATTLE_MESSAGE = 129;
const byte BOUNTY_SHAKE = 130;
const byte HUNTER_SHAKE = 131;
const byte HUNTER_HANDSHAKE_FINAL_ACK = 132;
const byte BOUNTY_HANDSHAKE_FINAL_ACK = 133;
const byte ZAP = 134;
const byte YOU_DEFEATED_ME = 135;

// TIMERS
auto uiRefresh = timer_create_default();

// LED Variables
byte buttonBrightness = 0;
byte interiorBrightness = 0;
byte loseBrightness = 0;
byte winBrightness = 0;

// GAME

// STATE - DORMANT
unsigned long bountyDelay[] = {300000, 900000};
unsigned long overchargeDelay[] = {180000, 300000};
unsigned long debugDelay = 3000;
bool activationInitiated = false;
bool beginActivationSequence = true;

// STATE - ACTIVATED
const float smoothingPoints = 255;
byte ledBrightness = 65;
float pwm_val = 0;
bool breatheUp = true;
long idleLEDBreak = 5000;
byte msgDelay = 0;

// STATE - ACTIVATED OVERCHARGE
byte overchargeStep = 0;
byte overchargeFlickers = 0;

// STATE - HANDSHAKE
enum class HandshakeState : byte
{
  HANDSHAKE_TIMEOUT_START_STATE = 0,
  HANDSHAKE_SEND_ROLE_SHAKE_STATE = 1,
  HANDSHAKE_WAIT_ROLE_SHAKE_STATE = 2,
  HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE = 3,
  HANDSHAKE_STATE_FINAL_ACK = 4
};

HandshakeState handshakeState = HandshakeState::HANDSHAKE_TIMEOUT_START_STATE;
/**
Handshake states:
0 - start timer to delay sending handshake signal for 1000 ms
1 - wait for handshake delay to expire
2 - start timeout timer
3 - begin sending shake message and check for timeout
**/

bool handshakeTimedOut = false;

const int HANDSHAKE_TIMEOUT = 5000;

// STATE - DUEL_ALERT
int alertFlashTime = 250;
byte alertCount = 0;

// STATE - DUEL_COUNTDOWN
bool doBattle = false;
const byte COUNTDOWN_STAGES = 4;
byte countdownStage = COUNTDOWN_STAGES;
int FOUR = 2000;
int THREE = 2000;
int TWO = 1000;
int ONE = 3000;

// STATE - DUEL
bool captured = false;
bool wonBattle = false;
bool startDuelTimer = true;
bool sendZapSignal = true;
bool duelTimedOut = false;
bool bvbDuel = false;
const int DUEL_TIMEOUT = 4000;

// STATE - WIN
bool startBattleFinish = true;
byte finishBattleBlinkCount = 0;
byte FINISH_BLINKS = 10;

// STATE - LOSE
bool initiatePowerDown = true;

bool reset = false;

// TIMER
SimpleTimer stateTimer;

QdState newState = QdState::INITIATE;
bool stateChangeReady = false;

// CONFIGURATION & DEBUG

// APPLICATION STATES
const byte DEBUG = 10;
const byte QD_GAME = 11;
const byte SET_USER = 12;
const byte CLEAR_USER = 13;

// SERIAL COMMANDS
const char ENTER_DEBUG = '!';
const char START_GAME = '@';
const char SETUP_DEVICE = '#';
const char SET_ACTIVATION = '^';
const char CHECK_IN = '%';
const char DEBUG_DELIMITER = '&';
const char GET_DEVICE_ID = '*';

byte APP_STATE = QD_GAME;

// ScoreDataStructureThings
//String userID = "init_uuid";
String current_match_id = "init_match_uuid";
String current_opponent_id = "init_opponent_uuid";

UUID uuidGenerator;
String generateUuid();



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

#define MAX_MATCHES 1000 // Maximum number of matches allowed

#define MATCH_SIZE sizeof(Match)

Match matches[MAX_MATCHES];
int numMatches = 0;

String dumpMatchesToJson() {
  StaticJsonDocument<512> doc;
  JsonArray matchesArray = doc.to<JsonArray>();
  StaticJsonDocument<128> match;
  JsonObject matchObj;
  for (int i = 0; i < numMatches; i++) {
    matchObj = match.to<JsonObject>();
    matches[i].fillJsonObject(matchObj);
    matchesArray.add(matchObj);
  }
  String output;
  serializeJson(matchesArray, output);
  return output;
}

String generateUuid() {
  uuidGenerator.generate();
  return uuidGenerator.toCharArray();
}

void addMatch(bool winner_is_hunter) {
  if (numMatches < MAX_MATCHES) {
    // Create a Match object
    if(playerInfo.isHunter()) {
      matches[numMatches].setupMatch(current_match_id, playerInfo.getUserID(), current_opponent_id);
    } else {
      matches[numMatches].setupMatch(current_match_id, current_opponent_id, playerInfo.getUserID());
    }
    matches[numMatches].setWinner(winner_is_hunter);
    numMatches++;
  }
}

String debugOutput = "";

void quickDrawGame();
void checkForAppState();
void debugEvents();
void setupActivation();
bool shouldActivate();
bool activationSequence();
void activationIdle();
void activationOvercharge();
bool initiateHandshake();
bool handshake();
void alertDuel();
void duelCountdown();
void duel();
void duelOver();

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
void updateQDState(QdState futureState);
void commitQDState();
void resetState();

int wins = 0;

void initializePins() {

  gpio_reset_pin(GPIO_NUM_38);
  gpio_reset_pin(GPIO_NUM_39);
  gpio_reset_pin(GPIO_NUM_40);
  gpio_reset_pin(GPIO_NUM_41);

  gpio_pad_select_gpio(GPIO_NUM_38);
  gpio_pad_select_gpio(GPIO_NUM_39);
  gpio_pad_select_gpio(GPIO_NUM_40);
  gpio_pad_select_gpio(GPIO_NUM_41);

  // init display
  pinMode(displayCS, OUTPUT);
  pinMode(displayDC, OUTPUT);
  digitalWrite(displayCS, 0);
  digitalWrite(displayDC, 0);

  pinMode(motorPin, OUTPUT);

  pinMode(TXt, OUTPUT);
  pinMode(TXr, INPUT);

  pinMode(RXt, OUTPUT);
  pinMode(RXr, INPUT);

  pinMode(displayLightsPin, OUTPUT);
  pinMode(gripLightsPin, OUTPUT);
}

void setup(void) {

  initializePins();

  WiFi.begin();

  uuidGenerator.setVariant4Mode();
  uuidGenerator.seed(random(999999999), random(999999999)); 

  Serial1.begin(BAUDRATE, SERIAL_8E2, TXr, TXt, true);

  Serial2.begin(BAUDRATE, SERIAL_8E2, RXr, RXt, true);

  if (playerInfo.isHunter()) {
    currentPalette = hunterColors;
  } else {
    currentPalette = bountyColors;
  }

  primary.attachClick(primaryButtonClick);
  // primary.attachDoubleClick(primaryButtonDoubleClick);
  // primary.attachLongPressStop(primaryButtonLongPress);
  secondary.attachClick(primaryButtonClick);
  // secondary.attachDoubleClick(secondaryButtonDoubleClick);
  // secondary.attachLongPressStop(secondaryButtonLongPress);

  FastLED
      .addLeds<WS2812B, displayLightsPin, GRB>(displayLights, numDisplayLights)
      .setCorrection(TypicalSMD5050);
  FastLED.addLeds<WS2812B, gripLightsPin, GRB>(gripLights, numGripLights)
      .setCorrection(TypicalSMD5050);
  FastLED.setBrightness(65);

  display.begin();

  display.clearBuffer();
  display.setContrast(125);
  display.setFont(u8g2_font_prospero_nbp_tf);
  drawDebugLabels();
  display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexLogo));
  display.drawXBM(64, 0, 128, 64, getImageForAllegiance(indexStamp));
  display.sendBuffer();

  uiRefresh.every(16, updateUi);

  delay(3000);

  clearComms();
}

void loop(void) {
  SimpleTimer::updateTime();
  primary.tick();
  secondary.tick();

  if (APP_STATE == QD_GAME) {
    quickDrawGame();
  } else if (APP_STATE == DEBUG) {
    debugEvents();
  } else if (APP_STATE == SET_USER) {
    playerInfo.setUserID(uuidGenerator);
  } else if (APP_STATE == CLEAR_USER) {
    playerInfo.clearUserID();
  }
  checkForAppState();
  uiRefresh.tick();
  animateLights();
}

// DISPLAY

// LEDS
byte leftIndex = 9;
byte rightIndex = 0;

void dormantAnimation() {
  if (breatheUp) {
    ledBrightness++;
  } else {
    ledBrightness--;
  }
  pwm_val =
      255.0 * (1.0 - abs((2.0 * (ledBrightness / smoothingPoints)) - 1.0));

  if (ledBrightness == 255) {
    breatheUp = false;
  } else if (ledBrightness == 0) {
    breatheUp = true;
  }

  displayLights[numDisplayLights-1] = ColorFromPalette(bountyColors, 0, pwm_val, LINEARBLEND);
}

void activationIdleAnimation() {
  if (breatheUp) {
    ledBrightness++;
  } else {
    ledBrightness--;
  }
  pwm_val =
      255.0 * (1.0 - abs((2.0 * (ledBrightness / smoothingPoints)) - 1.0));

  if (ledBrightness == 255) {
    breatheUp = false;
  } else if (ledBrightness == 0) {
    breatheUp = true;
  }

  if (random8() % 7 == 0) {
    displayLights[random8() % (numDisplayLights - 1)] +=
        ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND);
  }
  fadeToBlackBy(displayLights, numDisplayLights, 2);

  for (int i = 0; i < numGripLights; i++) {
    if (random8() % 65 == 0) {
      gripLights[i] +=
          ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND);
    }
  }
  fadeToBlackBy(gripLights, numGripLights, 2);
}

void animateLights() {

  if(APP_STATE == DEBUG) {
    EVERY_N_MILLIS(16) {
      activationIdleAnimation();
    }
  } else {
    switch(QD_STATE) {
      case QdState::INITIATE :
        break;
      case QdState::DORMANT:
        EVERY_N_MILLIS(16) {
          dormantAnimation();
        }
        break;
      case QdState::ACTIVATED:
        EVERY_N_MILLIS(16) {
          activationIdleAnimation();
        }
        break;
      case QdState::HANDSHAKE:
        displayLights[numDisplayLights -1] = ColorFromPalette(currentPalette, 0);
        break;
      case QdState::DUEL_ALERT:
        displayLights[numDisplayLights -1] = ColorFromPalette(currentPalette, 0);
        break;
      case QdState::DUEL_COUNTDOWN:
        EVERY_N_MILLIS(200) {
          fadeToBlackBy(gripLights, numGripLights, 3);
        }
        updateCountdownState();
        break;
      case QdState::DUEL:
        FastLED.setBrightness(255);
        FastLED.showColor(currentPalette[0], 255);
        break;
      case QdState::WIN:
        EVERY_N_MILLIS(4) {
          activationIdleAnimation();
          if(random8() % 20 < 2) {
            displayLights[random() % 13] += CRGB::White;
            gripLights[random() % 6] += CRGB::White;
          }
        }
        break;
      case QdState::LOSE:
        EVERY_N_MILLIS(750) {
          FastLED.setBrightness(FastLED.getBrightness()-2);
        }
        break;
    }
  }

  FastLED.show();
}

byte screenCounter = 0;

const unsigned char* getImageForAllegiance(int index) {
  switch(playerInfo.getAllegiance()) {
    case Allegiance::ENDLINE:
      return endlineImages[index];
    case Allegiance::HELIX:
      return helixImages[index];
    case Allegiance::RESISTANCE:
      return resistanceImages[index];
    default:
      return alleycatImages[index];
  }
}

int width = 0;
int offset = 0;
String winString = "";

bool updateUi(void *) {

  if(displayIsDirty){
    display.clearBuffer();

    switch(APP_STATE) {
      case DEBUG:
        display.print("DEBUG: " + DEBUG_MODE_SUBSTR);
        display.setCursor(0, 16);
        display.print(u8x8_u8toa(screenCounter++, 3));
        break;
      case QD_GAME:
        switch (QD_STATE) {
          case QdState::INITIATE:
            display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexLogo));
            display.drawXBM(64, 0, 128, 64, getImageForAllegiance(indexStamp));
            break;

          case QdState::DORMANT:
            display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexLogo));
            display.drawXBM(64, 0, 128, 64, getImageForAllegiance(indexStamp));
            break;

          case QdState::ACTIVATED:
            display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexIdle));
            display.setFont(u8g2_font_prospero_nbp_tf);
            display.setDrawColor(0);
            winString = String(wins);
            if(playerInfo.isHunter()) {
              width = display.getStrWidth("CAPTURES");
              offset = 64+(64-width)/2;
              display.setCursor(offset, 24);
              display.print("CAPTURES");
            } else {
              width = display.getStrWidth("EVADES");
              offset = 64+(64-width)/2;
              display.setCursor(offset, 24);
              display.print("EVADES");
            }
            
            display.setFont(u8g2_font_smart_patrol_nbp_tf);
            width = display.getUTF8Width(winString.c_str());
            offset = 64+(64-width)/2;
            display.setCursor(92, 50);
            display.print(winString);

            display.setDrawColor(1);
            break;

          case QdState::HANDSHAKE:
            display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexConnect));
            break;

          case QdState::DUEL_ALERT:
            display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexConnect));
            break;

          case QdState::DUEL_COUNTDOWN:
            if(countdownStage > 3) {
              display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexConnect)); 
            } else if(countdownStage == 3) {
              display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexThree)); 
            } else if(countdownStage == 2) {
              display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexTwo));
            } else if(countdownStage <= 1) {
              display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexOne));
            }
            break;

          case QdState::DUEL:
            display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexDraw));
            break;

          case QdState::WIN:
            display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexWin));
            break;

          case QdState::LOSE:
            display.drawXBM(0, 0, 128, 64, getImageForAllegiance(indexLose));
            break;
          }
    }

    display.sendBuffer();
    displayIsDirty = false;
  }

  return true; // for our timer to continue repeating.
}

void setGraphRight(int value) {
  gripLightsOnOff[3] = value < 1;
  gripLightsOnOff[4] = value < 2;
  gripLightsOnOff[5] = value < 3;
  displayLightsOnOff[11] = value < 4;
  displayLightsOnOff[10] = value < 5;
  displayLightsOnOff[9] = value < 6;
  displayLightsOnOff[8] = value < 7;
  displayLightsOnOff[7] = value < 8;
  displayLightsOnOff[6] = value < 9;
}

void setGraphLeft(int value) {
  gripLightsOnOff[2] = value < 1;
  gripLightsOnOff[1] = value < 2;
  gripLightsOnOff[0] = value < 3;
  displayLightsOnOff[0] = value < 4;
  displayLightsOnOff[1] = value < 5;
  displayLightsOnOff[2] = value < 6;
  displayLightsOnOff[3] = value < 7;
  displayLightsOnOff[4] = value < 8;
  displayLightsOnOff[5] = value < 9;
}

void updateCountdownState() {
  if(countdownStage == 3) {
    displayLights[11] = CRGB::Black;
    displayLights[10] = CRGB::Black;
    displayLights[0] = CRGB::Black;
    displayLights[1] = CRGB::Black;
  } else if(countdownStage == 2) {
    displayLights[9] = CRGB::Black;
    displayLights[8] = CRGB::Black;
    displayLights[2] = CRGB::Black;
    displayLights[3] = CRGB::Black;
  } else if(countdownStage == 1) {
    displayLights[7] = CRGB::Black;
    displayLights[6] = CRGB::Black;
    displayLights[4] = CRGB::Black;
    displayLights[5] = CRGB::Black;
  }
}

void setTransmitLight(boolean on) { displayLightsOnOff[12] = on; }

// MOTOR

void setMotorOutput(int value) {

  if (value > 255) {
    value = 255;
  } else if (value < 0) {
    value = 0;
  }

  analogWrite(motorPin, value);
}

void quickDrawGame() {
  if (QD_STATE == QdState::DORMANT) {
    if (!activationInitiated) {
      setupActivation();
    }

    if (shouldActivate()) {
      updateQDState(QdState::ACTIVATED);
    }
  } else if (QD_STATE == QdState::ACTIVATED) {
    if (beginActivationSequence) {
      if (activationSequence()) {
        beginActivationSequence = false; 
      }
    }

    activationIdle();
    if (initiateHandshake()) {
      updateQDState(QdState::HANDSHAKE);
    }
  } else if (QD_STATE == QdState::HANDSHAKE) {
    if (handshake()) {
      updateQDState(QdState::DUEL_ALERT);
    } else if (handshakeTimedOut) {
      updateQDState(QdState::ACTIVATED);
    }
  } else if (QD_STATE == QdState::DUEL_ALERT) {
    alertDuel();
    if (alertCount == 9) {
      updateQDState(QdState::DUEL_COUNTDOWN);
    }
  } else if (QD_STATE == QdState::DUEL_COUNTDOWN) {
    duelCountdown();
    if (doBattle) {
      updateQDState(QdState::DUEL);
    }
  } else if (QD_STATE == QdState::DUEL) {
    duel();
    if (captured) {
      updateQDState(QdState::LOSE);
    } else if (wonBattle) {
      updateQDState(QdState::WIN);
    } else if (duelTimedOut) {
      updateQDState(QdState::ACTIVATED);
    }
  } else if (QD_STATE == QdState::WIN) {
    if (!reset) {
      duelOver();
    } else {
      updateScore(true);
      clearComms();
      updateQDState(QdState::DORMANT);
    }
  } else if (QD_STATE == QdState::LOSE) {
    if (!reset) {
      duelOver();
    } else {
      updateScore(false);
      clearComms();
      updateQDState(QdState::DORMANT);
    }
  }

  commitQDState();
}

void checkForAppState() {

  if (requestSwitchAppState()) {
    if (debugCommsAvailable()) {
      String command = fetchDebugCommand();
      if (validateCommand(command, ENTER_DEBUG)) {
        APP_STATE = DEBUG;
        writeDebugByte(DEBUG_DELIMITER);
        currentPalette = idleColors;
        resetState();
      } else if (validateCommand(command, START_GAME) && APP_STATE == DEBUG) {
        if (playerInfo.isHunter()) {
          currentPalette = hunterColors;
        } else {
          currentPalette = bountyColors;
        }
        APP_STATE = QD_GAME;
        QD_STATE = QdState::DORMANT;
        resetState();
      }
    }
  }

  flushComms();
}

void debugEvents() {
  // todo: Debug Display

  if (debugCommandReceived()) {
    String command = fetchDebugCommand();
    if (validateCommand(command, SETUP_DEVICE)) {
      DEBUG_MODE_SUBSTR = "SETUP";
      setupDevice();
    } else if (validateCommand(command, CHECK_IN)) {
      DEBUG_MODE_SUBSTR = "CHECK_IN";
      checkInDevice();
    } else if (validateCommand(command, SET_ACTIVATION)) {
      DEBUG_MODE_SUBSTR = "SET_ACTIVATION";
      setActivationDelay();
    } else if(validateCommand(command, GET_DEVICE_ID)) {
      getDeviceId();
    }
    else {
      DEBUG_MODE_SUBSTR = "wait";
    }
  }
}

void setupActivation() {
  if (playerInfo.isHunter()) {
    // hunters have minimal activation delay
    stateTimer.setTimer(5000);
    //also go ahead and initialize the next match id here.
    current_match_id = generateUuid();
  } else {
    if (debugDelay > 0) {
      stateTimer.setTimer(debugDelay);
    } else {
      long timer = random(bountyDelay[0], bountyDelay[1]);
      stateTimer.setTimer(timer);
    }
  }

  activationInitiated = true;
}

bool shouldActivate() { return stateTimer.expired(); }

byte activateMotorCount = 0;
bool activateMotor = false;

bool activationSequence() {
  if (stateTimer.expired()) {
    if (activateMotorCount < 19) {
      if (activateMotor) {
        setMotorOutput(255);
      } else {
        setMotorOutput(0);
      }

      stateTimer.setTimer(100);
      activateMotorCount++;
      activateMotor = !activateMotor;
      return false;
    } else {
      activateMotorCount = 0;
      setMotorOutput(0);
      activateMotor = false;
      return true;
    }
  }
  return false;
}

void activationIdle() {
  // msgDelay was to prevent this from broadcasting every loop.
  if (msgDelay == 0) {
    if(playerInfo.isHunter()) 
    {
      writeGameComms(HUNTER_BATTLE_MESSAGE);
    } 
    else
    {
      writeGameComms(BOUNTY_BATTLE_MESSAGE);
    } 
  }
  msgDelay = msgDelay + 1;
}

bool initiateHandshake() {
  bool isHunter = playerInfo.isHunter();
  if (gameCommsAvailable()) {
    if(peekGameComms() == BOUNTY_BATTLE_MESSAGE && isHunter) {
      readGameComms();
      writeGameComms(HUNTER_BATTLE_MESSAGE);
      return true;
    } else if(peekGameComms() == HUNTER_BATTLE_MESSAGE && !isHunter) {
      readGameComms();
      writeGameComms(BOUNTY_BATTLE_MESSAGE);
      return true;
    }
  }

  return false;
}

// when this functions returns true, its the signal to change state
bool handshake() {
  // dont transition gamestate, just handshake sub-fsm
  if (handshakeState == HandshakeState::HANDSHAKE_TIMEOUT_START_STATE) {
    stateTimer.setTimer(HANDSHAKE_TIMEOUT);
    handshakeState = HandshakeState::HANDSHAKE_SEND_ROLE_SHAKE_STATE;
  } else if (handshakeState == HandshakeState::HANDSHAKE_SEND_ROLE_SHAKE_STATE) {
    if (stateTimer.expired()) {
      handshakeTimedOut = true;
      return false;
    }

    if (playerInfo.isHunter()) {
      writeGameComms(HUNTER_SHAKE);
    } else {
      writeGameComms(BOUNTY_SHAKE);
    }

    handshakeState = HandshakeState::HANDSHAKE_WAIT_ROLE_SHAKE_STATE;
  } else if(handshakeState == HandshakeState::HANDSHAKE_WAIT_ROLE_SHAKE_STATE) {
    if(stateTimer.expired()) {
      handshakeTimedOut = true;
      return false;
    }
    //While waiting to see the shake, if we get battle message,
    //then the other device is behind, we need to send it another
    //battle message ack.
    if(peekGameComms() == HUNTER_BATTLE_MESSAGE && !playerInfo.isHunter()) 
    {
      //it's confused.
      while(peekGameComms() == HUNTER_BATTLE_MESSAGE) {
        readGameComms();
      }
      writeGameComms(BOUNTY_BATTLE_MESSAGE);
      writeGameComms(BOUNTY_SHAKE);
    } 
    
    else if(peekGameComms() == BOUNTY_BATTLE_MESSAGE && playerInfo.isHunter()) 
    {
      //also confused.
      while(peekGameComms() == BOUNTY_BATTLE_MESSAGE) {
        readGameComms();
      }
      writeGameComms(HUNTER_BATTLE_MESSAGE);
      writeGameComms(HUNTER_SHAKE);
    } 
    
    if(peekGameComms() == BOUNTY_SHAKE && playerInfo.isHunter()) 
    {
      writeGameString(current_match_id);         // Sending match_id
      writeGameString(playerInfo.getUserID());              // Sending userID
      while(peekGameComms() == BOUNTY_SHAKE) {
          readGameComms();
      } 
      handshakeState = HandshakeState::HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE;
    }
    else if(peekGameComms() == HUNTER_SHAKE && !playerInfo.isHunter())
    {
      writeGameString(playerInfo.getUserID());
      while(peekGameComms() == HUNTER_SHAKE) {
        readGameComms();
      }
      handshakeState = HandshakeState::HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE;
    }

  } else if (handshakeState == HandshakeState::HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE) {
    if (stateTimer.expired()) {
      handshakeTimedOut = true;
      return false;
    }

    if (playerInfo.isHunter()) 
    {
      if(gameCommsAvailable()) {
        current_opponent_id = readGameString('\r');
        writeGameComms(HUNTER_HANDSHAKE_FINAL_ACK);
        handshakeState = HandshakeState::HANDSHAKE_STATE_FINAL_ACK;
        return false;
      }
    }
    else if (!playerInfo.isHunter()) 
    {
      if(gameCommsAvailable()) {
        current_match_id    = readGameString('\r');
        readGameString('\n');
        current_opponent_id = readGameString('\r');
        writeGameComms(BOUNTY_HANDSHAKE_FINAL_ACK);
        handshakeState = HandshakeState::HANDSHAKE_STATE_FINAL_ACK;
        return false;
      }
    }
  } else if (handshakeState == HandshakeState::HANDSHAKE_STATE_FINAL_ACK) {
    if (stateTimer.expired()) {
      handshakeTimedOut = true;
      return false;
    }
    
    if(!playerInfo.isHunter() && peekGameComms() == HUNTER_HANDSHAKE_FINAL_ACK) {
      return true;
    }

    if(playerInfo.isHunter() && peekGameComms() == BOUNTY_HANDSHAKE_FINAL_ACK) {
      return true;
    }
  }

  return false;
}

void alertDuel() {
  if (alertCount == 0) {
    buttonBrightness = 255;
  }

  if (stateTimer.expired()) {
    if (buttonBrightness == 255) {
      buttonBrightness = 0;
    } else {
      buttonBrightness = 255;
    }

    alertCount++;
    FastLED.setBrightness(buttonBrightness);
    stateTimer.setTimer(alertFlashTime);
  }
}

void duelCountdown() {
  if (stateTimer.expired()) {
    if (countdownStage == 4) {
      FastLED.showColor(currentPalette[0], 255);
      stateTimer.setTimer(FOUR);
      displayIsDirty = true;
      countdownStage = 3;
    } else if (countdownStage == 3) {
      FastLED.showColor(currentPalette[0], 150);
      stateTimer.setTimer(THREE);
      displayIsDirty = true;
      countdownStage = 2;
    } else if (countdownStage == 2) {
      FastLED.showColor(currentPalette[0], 75);
      stateTimer.setTimer(TWO);
      displayIsDirty = true;
      countdownStage = 1;
    } else if (countdownStage == 1) {
      FastLED.showColor(currentPalette[0], 0);
      stateTimer.setTimer(ONE);
      displayIsDirty = true;
      countdownStage = 0;
    } else if (countdownStage == 0) {
      doBattle = true;
    }
  }
}

void duel() {

  if (peekGameComms() == ZAP) {
    readGameComms();
    writeGameComms(YOU_DEFEATED_ME);
    captured = true;
    return;
  } else if (peekGameComms() == YOU_DEFEATED_ME) {
    readGameComms();
    wonBattle = true;
    return;
  } else {
    readGameComms();
  }

  // primary.tick();

  if (startDuelTimer) {
    stateTimer.setTimer(DUEL_TIMEOUT);
    startDuelTimer = false;
  }

  if (stateTimer.expired()) {
    // FastLED.setBrightness(0);
    bvbDuel = false;
    duelTimedOut = true;
  }
}

void duelOver() {
  if (startBattleFinish) {
    startBattleFinish = false;
    setMotorOutput(255);
  }

  if (stateTimer.expired()) {
    stateTimer.setTimer(500);
    if (finishBattleBlinkCount < FINISH_BLINKS) {
      if (finishBattleBlinkCount % 2 == 0) {
        setMotorOutput(0);
      } else {
        setMotorOutput(255);
      }
      finishBattleBlinkCount = finishBattleBlinkCount + 1;
    } else {
      setMotorOutput(0);
      reset = true;
    }
  }
}

bool isButtonPressed() {
  return digitalRead(primaryButtonPin) == LOW ||
         digitalRead(secondaryButtonPin) == LOW;
}

void updateScore(boolean win) {
  addMatch(win);
}

/*
byte readDebugByte() {
  return Serial1.read();
}
*/

void flushComms() {
  while (Serial1.available() > 0 && !isValidMessageSerial1()) {
    Serial1.read();
  }

  while (Serial2.available() > 0 && !isValidMessageSerial2()) {
    Serial2.read();
  }
}


bool requestSwitchAppState() {
  if (debugCommsAvailable()) {
    char command = (char)peekDebugComms();
    // Serial.print("Checking App State: ");
    // Serial.print(" Current buffer size: ");
    // Serial.print(debugCommsAvailable());
    // Serial.print(" Command: ");
    // Serial.print(command);
    // Serial.print(" As Byte: ");
    // Serial.println(peekDebugComms());
    return (command == ENTER_DEBUG || command == START_GAME);
  } else {
    return false;
  }
}


/*
  the two validation methods are quite similar, with some notable differences.
  Serial1 is used for hunter communications and for debug events.
  Serial2 is used for bounty communications.

  Debug events on Serial2 are assumed to be invalid.
  
  During the quick draw game, we validate the battle and handshake messages against what role
  the device is currently in.
*/
bool isValidMessageSerial1() {
  byte command = Serial1.peek();
  if ((char)command == ENTER_DEBUG || (char)command == START_GAME) {
    return true;
  }

  if (APP_STATE == DEBUG) {
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

// byte HANDSHAKE_TIMEOUT_START_STATE = 0;
// byte HANDSHAKE_SEND_ROLE_SHAKE_STATE = 1;
// byte HANDSHAKE_WAIT_ROLE_SHAKE_STATE = 2;
// byte HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE = 3;
// byte HANDSHAKE_STATE_FINAL_ACK = 4;

bool isValidMessageSerial2() {
  byte command = Serial2.peek();
  // if ((char)command == ENTER_DEBUG || (char)command == START_GAME) {
  //   return true;
  // }

  // if (APP_STATE == DEBUG) {
  //   Serial.print("Validating DEBUG: ");
  //   Serial.println((char)command);
  //   return ((char)command == SETUP_DEVICE || (char)command == SET_ACTIVATION ||
  //           (char)command == CHECK_IN || (char)command == DEBUG_DELIMITER);
  // } else {
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

bool debugCommandReceived() {
  if (debugCommsAvailable()) {
    char command = (char)peekDebugComms();
    return (command == SETUP_DEVICE || command == SET_ACTIVATION ||
            command == CHECK_IN || command == GET_DEVICE_ID);
  } else {
    return false;
  }
}

bool validateCommand(String a, char b) {
  char array[a.length() + 1];
  a.toCharArray(array, a.length() + 1);
  return b == array[0];
}

String fetchDebugData() { return readDebugString('\n'); }

String fetchDebugCommand() { return readDebugString(DEBUG_DELIMITER); }

void setupDevice() {
  if(debugCommsAvailable) {
    writeDebugByte(DEBUG_DELIMITER);

    String playerJson = readDebugString('\n');
    playerJson = stripWhitespace(playerJson);
    playerInfo.fromJson(playerJson);

    memset(matches, 0, sizeof(matches));
    wins = 0;

    writeDebugByte(DEBUG_DELIMITER);
  }
}

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

void getDeviceId() {
  writeDebugByte(DEBUG_DELIMITER);
  // uint8_t mac[6]; 
  // esp_read_mac(mac, ESP_MAC_WIFI_STA);
  writeDebugString(WiFi.macAddress());
  writeDebugByte(DEBUG_DELIMITER);

}

void checkInDevice() {
  // Print JSON string to serial
  String jsonStr = dumpMatchesToJson();
  writeDebugByte(DEBUG_DELIMITER);
  writeDebugString(jsonStr);
  writeDebugByte(DEBUG_DELIMITER);
}

void setActivationDelay() {
  if (debugCommsAvailable()) {
    String activationDelay = fetchDebugData();
    debugDelay = strtoul(activationDelay.c_str(), NULL, 10);
    // Serial.print("Set Activation Delay: ");
    // Serial.println(debugDelay);
  }
}

// state functions
void updateQDState(QdState futureState) {
  newState = futureState;
  stateChangeReady = true;
}

void commitQDState() {
  if (stateChangeReady) {
    QD_STATE = newState;
    resetState();
  }
  stateChangeReady = false;
}

void resetState() {
  reset = false;
  activationInitiated = false;
  beginActivationSequence = true;
  countdownStage = COUNTDOWN_STAGES;
  handshakeState = HandshakeState::HANDSHAKE_TIMEOUT_START_STATE;
  handshakeTimedOut = false;
  startDuelTimer = true;
  sendZapSignal = true;
  duelTimedOut = false;
  captured = false;
  wonBattle = false;
  overchargeStep = 0;
  overchargeFlickers = 0;
  startBattleFinish = true;
  initiatePowerDown = true;
  doBattle = false;
  alertCount = 0;
  finishBattleBlinkCount = 0;
  ledBrightness = 65;
  displayIsDirty = true;
  FastLED.clear(true);
  FastLED.setBrightness(65);
  clearComms();
  stateTimer.invalidate();
  setMotorOutput(0);
}

void drawDebugLabels() {
  display.drawStr(16, 10, "BTN 1:");
  display.drawStr(16, 20, "BTN 2:");
  display.drawStr(16, 30, "TX:");
  display.drawStr(16, 40, "RX:");
  display.drawStr(16, 50, "MOTOR:");
  display.drawStr(16, 60, "FRAME:");
}

void drawDebugState(char *button1State, char *button2State, char *txData,
                    char *rxData, char *motorSpeed, char *led1Pattern,
                    char *fps) {
  display.drawStr(80, 10, button1State);
  display.drawStr(80, 20, button2State);
  display.drawStr(80, 30, txData);
  display.drawStr(80, 40, rxData);
  display.drawStr(80, 50, motorSpeed);
  display.drawStr(80, 60, fps);
}

void updatePrimaryButtonState() {
  display.setCursor(2, 48);
  display.print(u8x8_u8toa(primaryPresses, 3));
}

void updateSecondaryButtonState() {
  display.setCursor(80, 20);
  display.print(u8x8_u8toa(secondaryPresses, 3));
}

void updateMotorState() {
  display.setCursor(80, 50);
  display.print(u8x8_u8toa(motorSpeed, 3));
}

void updateTXState() {
  display.setCursor(80, 30);
  display.print(u8x8_u8toa(lastTxPacket, 3));
}

void updateRXState() {
  display.setCursor(80, 40);
  display.print(u8x8_u8toa(lastRxPacket, 3));
}

void updateFramerate() {
  int duration = frameDuration;
  display.setCursor(80, 60);
  display.print(u8x8_u8toa(duration, 3));
}

// BUTTONS

void primaryButtonClick() {
  primaryPresses++;
  switch(QD_STATE) {
    case QdState::DUEL:
      if(sendZapSignal) {
        sendZapSignal = false;
        writeGameComms(ZAP);
      }
      break;
    default:
      break;
  }
}

void secondaryButtonClick() {
  secondaryPresses += 1;
  setGraphRight(secondaryPresses % 7);
}

void primaryButtonDoubleClick() { setTransmitLight(!displayLightsOnOff[12]); }

void secondaryButtonDoubleClick() {
  if (motorSpeed == 0) {
    motorSpeed = 65;
  } else if (motorSpeed == 65) {
    motorSpeed = 155;
  } else if (motorSpeed == 155) {
    motorSpeed = 255;
  } else if (motorSpeed == 255) {
    motorSpeed = 0;
  }
}

void primaryButtonLongPress() {
  playerInfo.toggleHunter();

  if (playerInfo.isHunter()) {
    currentPalette = hunterColors;
  } else {
    currentPalette = bountyColors;
  }
}

void secondaryButtonLongPress() {
  if (playerInfo.isHunter()) {
    writeTxTransaction(1);
  } else {
    writeRxTransaction(1);
  }
}