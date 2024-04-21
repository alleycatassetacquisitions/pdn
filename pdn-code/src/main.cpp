#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <HardwareSerial.h>
#include <OneButton.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <arduino-timer.h>
#include <UUID.h>

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

const int BAUDRATE = 57600;

//GAME ROLE
boolean isHunter = !true;
// boolean isHunter = true;

byte deviceID = 49;
String DEBUG_MODE_SUBSTR = "";

// DISPLAY

U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display(U8G2_R0, displayCS, displayDC,
                                               displayRST);

unsigned long frameBegin = 0;
unsigned long frameDuration = 0;

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
void activationIdleAnimation(int brightness);
bool updateUi(void *);

void setGraphRight(int value);
void setGraphLeft(int value);
void setTransmitLight(boolean on);

// MOTOR

void setMotorOutput(int value);
int motorSpeed = 0;

// SERIAL
bool debugCommsAvailable();
bool gameCommsAvailable();
void writeGameComms(byte command);
void writeGameString(String command);
byte readGameComms();
void writeDebugString(String command);
String readGameString(char terminator);
String readDebugString(char terminator);
byte peekGameComms();
byte peekDebugComms();
void writeDebugByte(byte command);

void monitorTX();
void monitorRX();

void writeTxTransaction(byte command);
void writeRxTransaction(byte command);

void flushComms();
void clearComms();

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
long bountyDelay[] = {300000, 900000};
long overchargeDelay[] = {180000, 300000};
long debugDelay = 3000;
bool activationInitiated = false;
bool beginActivationSequence = false;

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
byte handshakeState = 0;
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
int TWO = 2000;
int ONE = 5000;

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
unsigned long start = 0;
unsigned long now = 0;
unsigned long timer = 0;

void setTimer(unsigned long timerDelay);
bool timerExpired();
void invalidateTimer();
unsigned long getElapsedTime();

// GAME STATE MACHINE
const byte INITIATE = 0;
const byte DORMANT = 1;
const byte ACTIVATED = 2;
const byte HANDSHAKE = 3;
const byte DUEL_ALERT = 4;
const byte DUEL_COUNTDOWN = 5;
const byte DUEL = 6;
const byte WIN = 7;
const byte LOSE = 8;

byte QD_STATE = DORMANT;

byte newState = 0;
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

byte APP_STATE = QD_GAME;

// ScoreDataStructureThings
String userID = "init_uuid";
String current_match_id = "init_match_uuid";
String current_opponent_id = "init_opponent_uuid";

UUID uuidGenerator;
String generateUuid();

struct Match {
  String match_id;
  String hunter;
  String bounty;
  bool winner_is_hunter; // Indicates if the winner is the hunter

  String toJson() const {
    // Create a JSON object for the match
    StaticJsonDocument<128> doc;
    JsonObject matchObj = doc.to<JsonObject>();
    matchObj["match_id"] = match_id;
    matchObj["hunter"] = hunter;
    matchObj["bounty"] = bounty;
    matchObj["capture"] = winner_is_hunter;

    // Serialize the JSON object to a string
    String json;
    serializeJson(matchObj, json);
    return json;
  }

  void fromJson(const String &json) {
    // Parse the JSON string into a JSON object
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, json);

    // Check if parsing was successful
    if (!error) {
      // Retrieve values from the JSON object
      match_id = doc["match_id"].as<String>();
      hunter = doc["hunter"].as<String>();
      bounty = doc["bounty"].as<String>();
      winner_is_hunter = doc["capture"];
    } else {
      Serial.println("Failed to parse JSON");
    }
  }
};

String stripWhitespace(String input) {
  String output;
  for (int i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    // Check if the character is not a whitespace
    if (!isWhitespace(c)) {
      output += c; // Append non-whitespace character to the output string
    }
  }
  return output;
}

bool isWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

#define MAX_MATCHES 1000 // Maximum number of matches allowed

#define MATCH_SIZE sizeof(Match)

Match matches[MAX_MATCHES];
int numMatches = 0;

String dumpMatchesToJson() {
  StaticJsonDocument<512> doc;
  JsonArray matchesArray = doc.to<JsonArray>();
  for (int i = 0; i < numMatches; i++) {
    matchesArray.add(matches[i].toJson());
  }
  String output;
  serializeJson(matchesArray, output);
  return output;
}

void setUserID() {
  userID = generateUuid();
}

String generateUuid() {
  uuidGenerator.generate();
  return uuidGenerator.toCharArray();
}

String getUserID() {
  return userID;
}

void clearUserID() {
  userID = "default";
}

void addMatch(bool winner_is_hunter) {
  if (numMatches < MAX_MATCHES) {
    // Create a Match object
    if(isHunter) {
      matches[numMatches].hunter = getUserID();
      matches[numMatches].bounty = current_opponent_id;
    } else {
      matches[numMatches].hunter = current_opponent_id;
      matches[numMatches].bounty = getUserID();
    }
    matches[numMatches].match_id = current_match_id;
    matches[numMatches].winner_is_hunter = winner_is_hunter;
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
void updateQDState(byte futureState);
void commitQDState();
void resetState();

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

  uuidGenerator.setVariant4Mode();
  uuidGenerator.seed(random(999999999), random(999999999));

  setupDevice();

  Serial1.begin(BAUDRATE, SERIAL_8E2, TXr, TXt, true);

  Serial2.begin(BAUDRATE, SERIAL_8E2, RXr, RXt, true);

  if (isHunter) {
    currentPalette = hunterColors;
  } else {
    currentPalette = bountyColors;
  }

  // primary.attachClick(primaryButtonClick);
  // primary.attachDoubleClick(primaryButtonDoubleClick);
  // primary.attachLongPressStop(primaryButtonLongPress);
  // secondary.attachClick(secondaryButtonClick);
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
  display.setFont(u8g2_font_smart_patrol_nbp_tf);
  drawDebugLabels();
  drawDebugState("OFF", "OFF", "N/A", "N/A", "00%", "OFF", "OFF");
  display.sendBuffer();

  uiRefresh.every(16, updateUi);

  delay(3000);

  clearComms();
}

void loop(void) {
  now = millis();
  uiRefresh.tick();
  // primary.tick();

  if (APP_STATE == QD_GAME) {
    quickDrawGame();
  } else if (APP_STATE == DEBUG) {
    debugEvents();
  } else if (APP_STATE == SET_USER) {
    setUserID();
  } else if (APP_STATE == CLEAR_USER) {
    clearUserID();
  }
  checkForAppState();
}

// DISPLAY

// LEDS
byte leftIndex = 9;
byte rightIndex = 0;

void activationIdleAnimation(int brightness) {
  if (random8() % 7 == 0) {
    displayLights[random8() % (numDisplayLights - 1)] +=
        ColorFromPalette(currentPalette, random8(), brightness, LINEARBLEND);
  }
  fadeToBlackBy(displayLights, numDisplayLights, 2);

  for (int i = 0; i < numGripLights; i++) {
    if (random8() % 65 == 0) {
      gripLights[i] +=
          ColorFromPalette(currentPalette, random8(), brightness, LINEARBLEND);
    }
  }
  fadeToBlackBy(gripLights, numGripLights, 2);
}

void animateLights() {

  if(APP_STATE == DEBUG) {
    activationIdleAnimation(35);
  } else {
    switch(QD_STATE) {

    case INITIATE:
      FastLED.showColor(currentPalette[0]);
      break;
    case DORMANT:
      FastLED.showColor(currentPalette[0]);
      break;
    case ACTIVATED:
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

      activationIdleAnimation((int)pwm_val);
      break;
    case HANDSHAKE:
      FastLED.showColor(currentPalette[3]);
      break;
    case DUEL_ALERT:
      FastLED.showColor(bountyColors[1]);
      break;
    case DUEL_COUNTDOWN:
      break;
    case DUEL:
      break;
    case WIN:
      break;
    case LOSE:
      break;
    }
  }
}

byte screenCounter = 0;
bool updateUi(void *) {
  animateLights();

  display.clearBuffer();
  display.setCursor(0, 48);
  if (Serial1.available() > 0) {
    display.print(u8x8_u8toa(Serial1.peek(), 3));
    display.setCursor(36, 48);
    display.print(u8x8_u8toa(Serial1.available(), 3));
  }
  display.setCursor(0, 64);
  if (Serial2.available() > 0) {
    display.print(u8x8_u8toa(Serial2.peek(), 3));
    display.setCursor(36, 64);
    display.print(u8x8_u8toa(Serial2.available(), 3));
  }
  display.setCursor(16, 32);

  switch(APP_STATE) {
    case DEBUG:
      display.print("DEBUG: " + DEBUG_MODE_SUBSTR);
      display.setCursor(0, 16);
      display.print(u8x8_u8toa(screenCounter++, 3));
      break;
    case QD_GAME:
      switch (QD_STATE) {
        case INITIATE:
          display.print("INITIATE");
          display.setCursor(16, 48);
          display.print(u8x8_utoa(screenCounter++));
          break;

        case DORMANT:
          display.print("DORMANT");
          display.setCursor(16, 48);
          display.print(u8x8_utoa(screenCounter++));
          break;

        case ACTIVATED:
          display.print("ACTIVATED");
          display.setCursor(0, 16);
          display.print(u8x8_u8toa(screenCounter++, 3));
          
          display.setCursor(64, 16);
          display.print(getUserID());

          break;

        case HANDSHAKE:
          display.print("HANDSHAKE");
          display.setCursor(0, 16);
          display.print(u8x8_u8toa(screenCounter++, 3));
          display.setCursor(64, 14);
          display.print(u8x8_u8toa(handshakeState, 3));
          display.setCursor(0, 48);
          if (Serial1.available() > 0) {
            display.print(u8x8_u8toa(Serial1.peek(), 3));
            display.setCursor(36, 48);
            display.print(u8x8_u8toa(Serial1.available(), 3));
          }
          display.setCursor(0, 64);
          if (Serial2.available() > 0) {
            display.print(u8x8_u8toa(Serial2.peek(), 3));
            display.setCursor(36, 64);
            display.print(u8x8_u8toa(Serial2.available(), 3));
          }

          display.setCursor(80, 64);
          // if(current_match_id[current_match_id.length()-1] == '\0'){
          //   display.print(");
          // } else {
            display.print(current_match_id.length());
          // }
          break;

        case DUEL_ALERT:
          display.print("ALERT");
          break;

        case DUEL_COUNTDOWN:
          display.print("COUNTDOWN");
          break;

        case DUEL:
          display.print("DUEL");
          break;

        case WIN:
          display.print("WIN");
          break;

        case LOSE:
          display.print("LOSE");
          break;
        }
  }

  display.sendBuffer();
  FastLED.show();

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
  if (QD_STATE == DORMANT) {
    if (!activationInitiated) {
      setupActivation();
    }

    if (shouldActivate() && !beginActivationSequence) {
      beginActivationSequence = true;
    }

    if (beginActivationSequence) {
      if (activationSequence()) {
        updateQDState(ACTIVATED);
      }
    }
  } else if (QD_STATE == ACTIVATED) {
    // if(bvbDuel) {
    //   activationOvercharge();
    // } else {
    // this is sending BATTLE_MSG
    activationIdle();
    // }

    if (initiateHandshake()) {
      updateQDState(HANDSHAKE);
    }
  } else if (QD_STATE == HANDSHAKE) {
    if (handshake()) {
      updateQDState(DUEL_ALERT);
    } else if (handshakeTimedOut) {
      updateQDState(ACTIVATED);
    }
  } else if (QD_STATE == DUEL_ALERT) {
    alertDuel();
    if (alertCount == 9) {
      updateQDState(DUEL_COUNTDOWN);
    }
  } else if (QD_STATE == DUEL_COUNTDOWN) {
    duelCountdown();
    if (doBattle) {
      updateQDState(DUEL);
    }
  } else if (QD_STATE == DUEL) {
    duel();
    if (captured) {
      updateQDState(LOSE);
    } else if (wonBattle) {
      updateQDState(WIN);
    } else if (duelTimedOut) {
      updateQDState(ACTIVATED);
    }
  } else if (QD_STATE == WIN) {
    if (!reset) {
      duelOver();
    } else {
      updateScore(true);
      clearComms();
      updateQDState(DORMANT);
    }
  } else if (QD_STATE == LOSE) {
    if (!reset) {
      duelOver();
    } else {
      updateScore(false);
      clearComms();
      updateQDState(DORMANT);
    }
  }

  commitQDState();
}

void checkForAppState() {

  if (requestSwitchAppState()) {
    if (debugCommsAvailable()) {
      String command = fetchDebugCommand();
      if (validateCommand(command, ENTER_DEBUG)) {
        Serial.println("Switching to Debug");
        APP_STATE = DEBUG;
        writeDebugByte(DEBUG_DELIMITER);
        currentPalette = idleColors;
        resetState();
      } else if (validateCommand(command, START_GAME) && APP_STATE == DEBUG) {
        Serial.println("Switching to Game");

        if (isHunter) {
          currentPalette = hunterColors;
        } else {
          currentPalette = bountyColors;
        }

        APP_STATE = QD_GAME;
        QD_STATE = DORMANT;
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
    Serial.print("Command Received: ");
    Serial.println(command);
    if (validateCommand(command, SETUP_DEVICE)) {
      DEBUG_MODE_SUBSTR = "SETUP";
      setupDevice();
    } else if (validateCommand(command, CHECK_IN)) {
      DEBUG_MODE_SUBSTR = "CHECK_IN";
      Serial.println("CHECK_IN");
      checkInDevice();
    } else if (validateCommand(command, SET_ACTIVATION)) {
      DEBUG_MODE_SUBSTR = "SET_ACTIVATION";
      Serial.println("SET_ACTIVATION");
      setActivationDelay();
    }
    else {
      DEBUG_MODE_SUBSTR = "wait";
    }
  }
}

void setupActivation() {
  if (isHunter) {
    // hunters have minimal activation delay
    setTimer(100);
    //also go ahead and initialize the next match id here.
    current_match_id = generateUuid();
  } else {
    if (debugDelay > 0) {
      setTimer(debugDelay);
    } else {
      randomSeed(analogRead(A0));
      long timer = random(bountyDelay[0], bountyDelay[1]);
      Serial.print("timer: ");
      Serial.println(timer);
      setTimer(timer);
    }
  }

  activationInitiated = true;
}

bool shouldActivate() { return timerExpired(); }

byte activateMotorCount = 0;
bool activateMotor = false;

bool activationSequence() {
  if (timerExpired()) {
    if (activateMotorCount < 20) {
      if (activateMotor) {
        setMotorOutput(255);
      } else {
        setMotorOutput(0);
      }

      setTimer(100);
      activateMotorCount++;
      activateMotor = !activateMotor;
      return false;
    } else {
      activateMotorCount = 0;
      activateMotor = false;
      return true;
    }
  }
  return false;
}

void activationIdle() {
  // msgDelay was to prevent this from broadcasting every loop.
  if (msgDelay == 0) {
    if(isHunter) 
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

byte HANDSHAKE_TIMEOUT_START_STATE = 0;
byte HANDSHAKE_SEND_ROLE_SHAKE_STATE = 1;
byte HANDSHAKE_WAIT_ROLE_SHAKE_STATE = 2;
byte HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE = 3;
byte HANDSHAKE_STATE_FINAL_ACK = 4;
// when this functions returns true, its the signal to change state
bool handshake() {
  // dont transition gamestate, just handshake sub-fsm
  if (handshakeState == HANDSHAKE_TIMEOUT_START_STATE) {
    setTimer(HANDSHAKE_TIMEOUT);
    handshakeState = HANDSHAKE_SEND_ROLE_SHAKE_STATE;
  } else if (handshakeState == HANDSHAKE_SEND_ROLE_SHAKE_STATE) {
    if (timerExpired()) {
      handshakeTimedOut = true;
      return false;
    }

    if (isHunter) {
      writeGameComms(HUNTER_SHAKE);
    } else {
      writeGameComms(BOUNTY_SHAKE);
    }

    handshakeState = HANDSHAKE_WAIT_ROLE_SHAKE_STATE;
  } else if(handshakeState == HANDSHAKE_WAIT_ROLE_SHAKE_STATE) {
    if(timerExpired()) {
      handshakeTimedOut = true;
      return false;
    }
    //While waiting to see the shake, if we get battle message,
    //then the other device is behind, we need to send it another
    //battle message ack.
    if(peekGameComms() == HUNTER_BATTLE_MESSAGE && !isHunter) 
    {
      //it's confused.
      while(peekGameComms() == HUNTER_BATTLE_MESSAGE) {
        readGameComms();
      }
      writeGameComms(BOUNTY_BATTLE_MESSAGE);
      writeGameComms(BOUNTY_SHAKE);
    } 
    
    else if(peekGameComms() == BOUNTY_BATTLE_MESSAGE && isHunter) 
    {
      //also confused.
      while(peekGameComms() == BOUNTY_BATTLE_MESSAGE) {
        readGameComms();
      }
      writeGameComms(HUNTER_BATTLE_MESSAGE);
      writeGameComms(HUNTER_SHAKE);
    } 
    
    if(peekGameComms() == BOUNTY_SHAKE && isHunter) 
    {
      writeGameString(current_match_id);         // Sending match_id
      writeGameString(getUserID());              // Sending userID
      while(peekGameComms() == BOUNTY_SHAKE) {
          readGameComms();
      } 
      handshakeState = HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE;
    }
    else if(peekGameComms() == HUNTER_SHAKE && !isHunter)
    {
      writeGameString(getUserID());
      while(peekGameComms() == HUNTER_SHAKE) {
        readGameComms();
      }
      handshakeState = HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE;
    }

  } else if (handshakeState == HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE) {
    if (timerExpired()) {
      handshakeTimedOut = true;
      return false;
    }

    if (isHunter) 
    {
      if(gameCommsAvailable()) {
        current_opponent_id = readGameString('\r');
        writeGameComms(HUNTER_HANDSHAKE_FINAL_ACK);
        handshakeState = HANDSHAKE_STATE_FINAL_ACK;
        return false;
      }
    }
    else if (!isHunter) 
    {
      if(gameCommsAvailable()) {
        current_match_id    = readGameString('\r');
        readGameString('\n');
        current_opponent_id = readGameString('\r');
        writeGameComms(BOUNTY_HANDSHAKE_FINAL_ACK);
        handshakeState = HANDSHAKE_STATE_FINAL_ACK;
        return false;
      }
    }
  } else if (handshakeState == HANDSHAKE_STATE_FINAL_ACK) {
    if (timerExpired()) {
      handshakeTimedOut = true;
      return false;
    }
    
    if(!isHunter && peekGameComms() == HUNTER_HANDSHAKE_FINAL_ACK) {
      return true;
    }

    if(isHunter && peekGameComms() == BOUNTY_HANDSHAKE_FINAL_ACK) {
      return true;
    }
  }

  return false;
}

void alertDuel() {
  if (alertCount == 0) {
    buttonBrightness = 255;
  }

  if (timerExpired()) {
    if (buttonBrightness == 255) {
      buttonBrightness = 0;
    } else {
      buttonBrightness = 255;
    }

    alertCount++;
    FastLED.setBrightness(buttonBrightness);
    setTimer(alertFlashTime);
  }
}

void duelCountdown() {
  if (timerExpired()) {
    if (countdownStage == 4) {
      setTimer(FOUR);
      FastLED.setBrightness(255);
      countdownStage = 3;
    } else if (countdownStage == 3) {
      setTimer(THREE);
      // setLED(buttonLED, 0);
      // setLED(interiorLED, 0);
      countdownStage = 2;
    } else if (countdownStage == 2) {
      setTimer(TWO);
      // setLED(loseLED, 0);
      countdownStage = 1;
    } else if (countdownStage == 1) {
      setTimer(ONE);
      // setLED(winLED, 0);
      countdownStage = 0;
    } else if (countdownStage == 0) {
      doBattle = true;
    }
  }
}

void duel() {
  FastLED.setBrightness(255);

  if (peekGameComms() == ZAP) {
    readGameComms();
    writeGameComms(YOU_DEFEATED_ME);
    captured = true;
    return;
  } else if (peekGameComms() == YOU_DEFEATED_ME) {
    readGameComms();
    wonBattle = true;
    return;
  } else if (peekGameComms() != -1) {
    readGameComms();
  }

  if (isButtonPressed() && sendZapSignal) {
    sendZapSignal = false;
    writeGameComms(ZAP);
  }

  if (startDuelTimer) {
    setTimer(DUEL_TIMEOUT);
    startDuelTimer = false;
  }

  if (timerExpired()) {
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

  if (timerExpired()) {
    setTimer(500);
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

// SERIAL
// GAME COMMS

byte peekGameComms() {
  if (isHunter) {
    return Serial1.peek();
  }

  return Serial2.peek();
}

bool gameCommsAvailable() {
  return (isHunter && (Serial1.available() > 0)) ||
         (!isHunter && (Serial2.available() > 0));
}

void writeGameComms(byte command) {
  if (isHunter) {
    Serial1.write(command);
  } else {
    Serial2.write(command);
  }
}

byte readGameComms() {
  if (isHunter) {
    return Serial1.read();
  }
  // else they are a bounty.
  return Serial2.read();
}

void writeGameString(String command) {
  if (isHunter) {
    Serial1.println(command);
  } else {
    Serial2.println(command);
  }
}

String readGameString(char terminator) {
  if(isHunter) 
  {
    return Serial1.readStringUntil(terminator);
  } 
    
  return Serial2.readStringUntil(terminator);
}

//DEBUG SERIAL
byte peekDebugComms() {
  return Serial1.peek();
}

bool debugCommsAvailable() {
  return Serial1.available() > 0;
}

void writeDebugByte(byte data) {
  Serial1.write(data);
}

byte readDebugByte() {
  return Serial1.read();
}

void writeDebugString(String command) {
  Serial1.println(command);
}

String readDebugString(char terminator) {
    return Serial1.readStringUntil(terminator);
}

void flushComms() {
  while (Serial1.available() > 0 && !isValidMessageSerial1()) {
    Serial1.read();
  }

  while (Serial2.available() > 0 && !isValidMessageSerial2()) {
    Serial2.read();
  }
}

void clearComms()
{
  while (Serial1.available() > 0) {
    Serial1.read();
  }

  while (Serial2.available() > 0) {
    Serial2.read();
  }
}

bool requestSwitchAppState() {
  if (debugCommsAvailable()) {
    char command = (char)peekDebugComms();
    Serial.print("Checking App State: ");
    Serial.print(" Current buffer size: ");
    Serial.print(debugCommsAvailable());
    Serial.print(" Command: ");
    Serial.print(command);
    Serial.print(" As Byte: ");
    Serial.println(peekDebugComms());
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
    Serial.print("Validating DEBUG: ");
    Serial.println((char)command);
    return ((char)command == SETUP_DEVICE || (char)command == SET_ACTIVATION ||
            (char)command == CHECK_IN || (char)command == DEBUG_DELIMITER);
  } else {
    if (QD_STATE == ACTIVATED) { 
      return (command == BOUNTY_BATTLE_MESSAGE && isHunter);
    } else if (QD_STATE == HANDSHAKE) {
      return (command == BOUNTY_SHAKE && handshakeState < HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE) || (command == BOUNTY_HANDSHAKE_FINAL_ACK);
    } else if (QD_STATE == DUEL) {
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
    if (QD_STATE == ACTIVATED) {
      return (command == HUNTER_BATTLE_MESSAGE && !isHunter);
    } else if (QD_STATE == HANDSHAKE) {
      return (command == HUNTER_SHAKE && handshakeState < HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE) || (command == HUNTER_HANDSHAKE_FINAL_ACK);
    } else if (QD_STATE == DUEL) {
      return (command == ZAP || command == YOU_DEFEATED_ME);
    }
  // }

  return false;
}

bool debugCommandReceived() {
  if (debugCommsAvailable()) {
    char command = (char)peekDebugComms();
    return (command == SETUP_DEVICE || command == SET_ACTIVATION ||
            command == CHECK_IN);
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
  clearUserID();
  setUserID();
  //writeComms(deviceID);
  //writeCommsString(getUserID());
}

void checkInDevice() {
  // Print JSON string to serial
  String jsonStr = dumpMatchesToJson();
  writeDebugByte(DEBUG_DELIMITER);
  writeDebugString("jsonStr:");
  writeDebugString(jsonStr);
  writeDebugByte(DEBUG_DELIMITER);
}

void setActivationDelay() {
  if (debugCommsAvailable()) {
    String activationDelay = fetchDebugData();
    debugDelay = strtoul(activationDelay.c_str(), NULL, 10);
    Serial.print("Set Activation Delay: ");
    Serial.println(debugDelay);
  }
}

// time functions
unsigned long getElapsedTime() { return now - start; }

bool timerExpired() { return timer <= getElapsedTime(); }

void invalidateTimer() { timer = 0; }

void setTimer(unsigned long timerDelay) {
  timer = timerDelay;
  start = millis();
}

// state functions
void updateQDState(byte futureState) {
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
  countdownStage = COUNTDOWN_STAGES;
  handshakeState = 0;
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
  clearComms();
  invalidateTimer();
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
  display.setCursor(80, 10);
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
  isHunter = !isHunter;

  if (isHunter) {
    currentPalette = hunterColors;
  } else {
    currentPalette = bountyColors;
  }
}

void secondaryButtonLongPress() {
  if (isHunter) {
    writeTxTransaction(1);
  } else {
    writeRxTransaction(1);
  }
}