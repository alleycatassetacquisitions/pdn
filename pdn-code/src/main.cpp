#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <FastLED.h>
#include <HardwareSerial.h>
#include <OneButton.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <arduino-timer.h>

#define primaryButtonPin 15
#define secondaryButtonPin 16
#define motorPin 17
#define RXt 41
#define RXr 40
#define TXr 39
#define TXt 38
#define displayLightsPin 13
#define gripLightsPin 21
#define displayCS 10
#define displayDC 9
#define displayRST 14

#define numDisplayLights 13
#define numGripLights 6

const int BAUDRATE = 115200;

boolean isHunter = false;

byte deviceID = 49;

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
bool commsAvailable();
void writeComms(byte command);
void writeCommsString(String command);
byte readComms();
String readDebugString(char terminator);
byte peekComms();

void monitorTX();
void monitorRX();

void writeTxTransaction(byte command);
void writeRxTransaction(byte command);

void flushComms();

byte lastTxPacket = 0;
byte lastRxPacket = 0;

const byte BATTLE_MESSAGE = 100;
const byte BOUNTY_SHAKE = 101;
const byte HUNTER_SHAKE = 102;
const byte ZAP = 103;
const byte YOU_DEFEATED_ME = 104;

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

const int HANDSHAKE_DELAY = 1000;
const int HANDSHAKE_TIMEOUT = 1000;

// STATE - DUEL_ALERT
int alertFlashTime = 75;
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
const char DEBUG_DELIMITER = '-';

byte APP_STATE = QD_GAME;

// ScoreDataStructureThings
String current_match_id;
String current_opponent_id;

struct Match {
  String match_id;
  String hunter;
  String bounty;
  bool winner_is_hunter; // Indicates if the winner is the hunter
};

#define MAX_MATCHES 1000 // Maximum number of matches allowed

// EEPROM
#define EEPROM_MATCH_SIZE sizeof(Match)
#define EEPROM_NUM_MATCHES_ADDRESS 0
#define EEPROM_USERID_ADDRESS (EEPROM_NUM_MATCHES_ADDRESS + sizeof(int))
#define EEPROM_MATCHES_ADDRESS (EEPROM_USERID_ADDRESS + (38 * sizeof(char)))

Match matches[MAX_MATCHES];
int numMatches = 0;

String generateUUIDv4() {
  // Generates a random UUIDv4 string
  String uuid = "";
  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      uuid += "-";
    } else if (i == 14) {
      uuid += "4";
    } else if (i == 19) {
      uuid +=
          char(random(8) % 4 + 8); // Generates a random hex character in [89ab]
    } else {
      uuid += char(random(16) % 16 + (i < 20 ? 0 : 8));
    }
  }
  return uuid;
}

void writeMatchArrayToEEPROM() {
  // Calculate the EEPROM address
  int address = EEPROM_MATCHES_ADDRESS;

  // Write the Match array to EEPROM
  for (int i = 0; i < numMatches; i++) {
    EEPROM.put(address, matches[i]);
    address += EEPROM_MATCH_SIZE;
  }

  // Write the number of matches to EEPROM
  EEPROM.put(EEPROM_NUM_MATCHES_ADDRESS, numMatches);
}

void readMatchArrayFromEEPROM() {
  // Calculate the EEPROM address
  int address = EEPROM_MATCHES_ADDRESS;

  // Read the Match array from EEPROM
  for (int i = 0; i < numMatches; i++) {
    EEPROM.get(address, matches[i]);
    address += EEPROM_MATCH_SIZE;
  }

  // Read the number of matches from EEPROM
  EEPROM.get(EEPROM_NUM_MATCHES_ADDRESS, numMatches);
}

void addMatch(const String &match_id, const String &hunter,
              const String &bounty, bool winner_is_hunter) {
  if (numMatches < MAX_MATCHES) {
    // Sync matches
    readMatchArrayFromEEPROM();
    // Create a Match object
    matches[numMatches].match_id = match_id;
    matches[numMatches].hunter = hunter;
    matches[numMatches].bounty = bounty;
    matches[numMatches].winner_is_hunter = winner_is_hunter;
    numMatches++;
    // Sync matches
    writeMatchArrayToEEPROM();
  }
}

String dumpMatchesToJson() {
  // Sync State
  readMatchArrayFromEEPROM();

  StaticJsonDocument<512> doc;
  JsonArray matchesArray = doc.to<JsonArray>();
  for (int i = 0; i < numMatches; i++) {
    JsonObject matchObj = matchesArray.createNestedObject();
    matchObj["match_id"] = matches[i].match_id;
    matchObj["hunter"] = matches[i].hunter;
    matchObj["bounty"] = matches[i].bounty;
    matchObj["capture"] = matches[i].winner_is_hunter;
  }
  String output;
  serializeJson(matchesArray, output);
  return output;
}

void setUserID() {
  String userID = generateUUIDv4(); // Set the userID UUID using the
                                    // generateUUIDv4() function

  // Write the userID to EEPROM
  EEPROM.put(EEPROM_USERID_ADDRESS, userID);
}

String getUserID() {
  String userID;
  // Read the userID from EEPROM
  EEPROM.get(EEPROM_USERID_ADDRESS, userID);
  return userID;
}

void clearUserID() {
  // Clear the userID in EEPROM by writing an empty String
  String emptyString;
  EEPROM.put(EEPROM_USERID_ADDRESS, emptyString);
}

void clearMatchesFromEEPROM() {
  // Clear the Match array in EEPROM by writing default values
  for (int i = 0; i < MAX_MATCHES; i++) {
    Match emptyMatch;
    EEPROM.put(EEPROM_MATCHES_ADDRESS + i * EEPROM_MATCH_SIZE, emptyMatch);
  }

  // Clear the number of matches in EEPROM by writing zero
  int zero = 0;
  EEPROM.put(EEPROM_NUM_MATCHES_ADDRESS, zero);

  // Reset the global variables
  numMatches = 0;
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
bool handshake(String &match_id, String &opponentID);
void alertDuel();
void duelCountdown();
void duel();
void duelOver();

void updateScore(String match_id, String opponent, boolean win);

bool requestSwitchAppState();
bool isValidMessageSerial1();
bool isValidMessageSerial2();
bool commandReceived();
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

  Serial1.begin(BAUDRATE, SERIAL_8N1, TXr, TXt, true);

  Serial2.begin(BAUDRATE, SERIAL_8N1, RXr, RXt, true);

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

  flushComms();
}

void loop(void) {
  now = millis();
  uiRefresh.tick();
  primary.tick();

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
    if (QD_STATE == INITIATE) {
      FastLED.showColor(currentPalette[0]);
    } else if (QD_STATE == DORMANT) {
      FastLED.showColor(currentPalette[0]);
    } else if (QD_STATE == ACTIVATED) {
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
    } else if (QD_STATE == HANDSHAKE) {
      FastLED.showColor(currentPalette[3]);
    } else if (QD_STATE == DUEL_ALERT) {
      FastLED.showColor(bountyColors[1]);
    } else if (QD_STATE == DUEL_COUNTDOWN) {
    } else if (QD_STATE == DUEL) {
    } else if (QD_STATE == WIN) {
    } else if (QD_STATE == LOSE) {
    }
  }
}

byte screenCounter = 0;
bool updateUi(void *) {
  animateLights();

  display.clearBuffer();
  display.setCursor(16, 32);

  switch(APP_STATE) {
    case DEBUG:
      display.print("DEBUG");
      display.setCursor(2, 48);
      display.print("1:");
      if(commsAvailable()) {
        display.print(readDebugString('\n'));
      }
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
          display.setCursor(0, 48);
          if (Serial1.available() > 1) {
            display.print(u8x8_u8toa(Serial1.peek(), 3));
            display.setCursor(36, 48);
            display.print(u8x8_u8toa(Serial1.available(), 3));
          }
          display.setCursor(0, 64);
          if (Serial2.available() > 1) {
            display.print(u8x8_u8toa(Serial2.peek(), 3));
            display.setCursor(36, 64);
            display.print(u8x8_u8toa(Serial2.available(), 3));
          }
          break;

        case HANDSHAKE:
          display.print("HANDSHAKE");
          display.setCursor(0, 16);
          display.print(u8x8_u8toa(screenCounter++, 3));
          display.setCursor(64, 14);
          display.print(u8x8_u8toa(handshakeState, 3));
          display.setCursor(0, 48);
          if (Serial1.available() > 1) {
            display.print(u8x8_u8toa(Serial1.peek(), 3));
            display.setCursor(36, 48);
            display.print(u8x8_u8toa(Serial1.available(), 3));
          }
          display.setCursor(0, 64);
          if (Serial2.available() > 1) {
            display.print(u8x8_u8toa(Serial2.peek(), 3));
            display.setCursor(36, 64);
            display.print(u8x8_u8toa(Serial2.available(), 3));
          }
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

// SERIAL

void writeComms(byte command) {
  if (isHunter) {
    Serial1.write(command);
  } else {
    Serial2.write(command);
  }
}

void writeCommsString(String command) {
  if (isHunter) {
    Serial1.println(command);
  } else {
    Serial2.println(command);
  }
}

byte readComms() {
  if (isHunter) {
    return Serial1.read();
  }
  // else they are a bounty.
  return Serial2.read();
}

String readDebugString(char terminator) {
  if (isHunter) {
    return Serial1.readStringUntil(terminator);
  }

  return Serial2.readStringUntil(terminator);
}

byte peekComms() {
  if (isHunter) {
    return Serial1.peek();
  }

  return Serial2.peek();
}

bool commsAvailable() {
  return (isHunter && (Serial1.available() > 1)) ||
         (!isHunter && (Serial2.available() > 1));
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
    if (handshake(current_match_id, current_opponent_id)) {
      updateQDState(DUEL_ALERT);
    } else if (handshakeTimedOut) {
      updateQDState(ACTIVATED);
    }
  } else if (QD_STATE == DUEL_ALERT) {
    alertDuel();
    if (alertCount == 10) {
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
      updateScore(current_match_id, current_opponent_id, true);
      updateQDState(DORMANT);
    }
  } else if (QD_STATE == LOSE) {
    if (!reset) {
      duelOver();
    } else {
      updateScore(current_match_id, current_opponent_id, false);
      updateQDState(DORMANT);
    }
  }

  commitQDState();
}

void checkForAppState() {

  if (requestSwitchAppState()) {
    if (commsAvailable()) {
      String command = fetchDebugCommand();
      if (validateCommand(command, ENTER_DEBUG)) {
        Serial.println("Switching to Debug");
        APP_STATE = DEBUG;
        writeComms(DEBUG_DELIMITER);
        currentPalette = idleColors;
        resetState();
      } else if (validateCommand(command, START_GAME) && APP_STATE == DEBUG) {
        Serial.println("Switching to Game");
        APP_STATE = QD_GAME;
        QD_STATE = DORMANT;
        resetState();
      }
    }
  }

  flushComms();
}

byte brt = 255;

void debugEvents() {
  // todo: Debug Display

  if (commandReceived()) {
    // setTimer(HANDSHAKE_TIMEOUT);
    bool clean_cmd_queue = false;
    while(!clean_cmd_queue) // && !timerExpired())
    {
      String command = fetchDebugCommand();
      Serial.print("Command Received: ");
      Serial.println(command);
      if (validateCommand(command, SETUP_DEVICE)) {
        clean_cmd_queue = true;
        setupDevice();
      } else if (validateCommand(command, CHECK_IN)) {
        clean_cmd_queue = true;
        Serial.println("CHECK_IN");
        checkInDevice();
      } else if (validateCommand(command, SET_ACTIVATION)) {
        clean_cmd_queue = true;
        Serial.println("SET_ACTIVATION");
        setActivationDelay();
      }
    }
  }
}

void setupActivation() {
  if (isHunter) {
    // hunters have minimal activation delay
    setTimer(100);
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
    writeComms(BATTLE_MESSAGE);
  }
  msgDelay = msgDelay + 1;
}

bool initiateHandshake() {
  if (commsAvailable() && peekComms() == BATTLE_MESSAGE) {
    readComms();
    writeComms(BATTLE_MESSAGE);
    return true;
  }

  return false;
}

byte HANDSHAKE_TIMEOUT_START_STATE = 0;
byte HANDSHAKE_SEND_ROLE_SHAKE_STATE = 1;
byte HANDSHAKE_WAIT_ROLE_SHAKE_STATE = 2;
byte HANDSHAKE_RECIEVE_ROLE_SHAKE_STATE = 3;
// when this functions returns true, its the signal to change state
bool handshake(String &match_id, String &opponent_id) {
  // dont transition gamestate, just handshake sub-fsm
  if (handshakeState == HANDSHAKE_TIMEOUT_START_STATE) {
    setTimer(HANDSHAKE_TIMEOUT);
    handshakeState = HANDSHAKE_SEND_ROLE_SHAKE_STATE;
    return false;
  } else if (handshakeState == HANDSHAKE_SEND_ROLE_SHAKE_STATE) {
    if (timerExpired()) {
      FastLED.setBrightness(0);
      handshakeTimedOut = true;
      return false;
    }

    if (isHunter) {
      writeComms(HUNTER_SHAKE);
    } else {
      writeComms(BOUNTY_SHAKE);
    }

    handshakeState = HANDSHAKE_RECIEVE_ROLE_SHAKE_STATE;
    return true;
  } else if (handshakeState == HANDSHAKE_RECIEVE_ROLE_SHAKE_STATE) {

    byte command = peekComms();
    if (isHunter && command == BOUNTY_SHAKE) {
      char match_uuid_str[37];
      match_id = generateUUIDv4();              // Generate match_id UUID
      match_id.toCharArray(match_uuid_str, 37); // Convert String to char array
      writeComms(HUNTER_SHAKE);
      writeCommsString(match_uuid_str);      // Sending match_id
      writeCommsString(getUserID().c_str()); // Sending userID
      return true;
    } else if (!isHunter && command == HUNTER_SHAKE) {
      // bvbDuel = (command == BOUNTY_SHAKE);
      writeComms(BOUNTY_SHAKE);
      match_id = readDebugString('\0');
      opponent_id = readDebugString('\0');
      return true;
    }
    return false;
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

  if (peekComms() == ZAP) {
    readComms();
    writeComms(YOU_DEFEATED_ME);
    captured = true;
    return;
  } else if (peekComms() == YOU_DEFEATED_ME) {
    readComms();
    wonBattle = true;
    return;
  } else if (peekComms() != -1) {
    readComms();
  }

  if (isButtonPressed() && sendZapSignal) {
    sendZapSignal = false;
    writeComms(ZAP);
  }

  if (startDuelTimer) {
    setTimer(DUEL_TIMEOUT);
    startDuelTimer = false;
  }

  if (timerExpired()) {
    FastLED.setBrightness(0);
    bvbDuel = false;
    duelTimedOut = true;
  }
}

void duelOver() {
  if (startBattleFinish) {
    startBattleFinish = false;
    FastLED.setBrightness(0);
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

// EEPROM functions
void updateScore(String match_id, String opponent, boolean win) {
  String userID = getUserID();
  String hunter_uuid = isHunter ? userID : opponent;
  String bounty_uuid = !isHunter ? userID : opponent;
  addMatch(match_id, hunter_uuid, bounty_uuid, win);
}

// serial functions
void flushComms() {
  while (Serial1.available() > 1 && !isValidMessageSerial1()) {
    Serial1.read();
  }

  while (Serial2.available() > 1 && !isValidMessageSerial2()) {
    Serial2.read();
  }
}

bool requestSwitchAppState() {
  if (commsAvailable()) {
    char command = (char)peekComms();
    Serial.print("Checking App State: ");
    Serial.print(" Current buffer size: ");
    Serial.print(commsAvailable());
    Serial.print(" Command: ");
    Serial.print(command);
    Serial.print(" As Byte: ");
    Serial.println(peekComms());
    return (command == ENTER_DEBUG || command == START_GAME);
  } else {
    return false;
  }
}

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
      return command == BATTLE_MESSAGE;
    } else if (QD_STATE == HANDSHAKE) {
      return (command == BOUNTY_SHAKE || command == HUNTER_SHAKE);
    } else if (QD_STATE == DUEL) {
      return (command == ZAP || command == YOU_DEFEATED_ME);
    }
  }

  return false;
}

bool isValidMessageSerial2() {
  byte command = Serial2.peek();
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
      return command == BATTLE_MESSAGE;
    } else if (QD_STATE == HANDSHAKE) {
      return (command == BOUNTY_SHAKE || command == HUNTER_SHAKE);
    } else if (QD_STATE == DUEL) {
      return (command == ZAP || command == YOU_DEFEATED_ME);
    }
  }

  return false;
}

bool commandReceived() {
  if (commsAvailable()) {
    char command = (char)peekComms();
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
  clearMatchesFromEEPROM();
  clearUserID();
  setUserID();
  writeComms(deviceID);
  writeCommsString(getUserID());
}

void checkInDevice() {
  writeComms(deviceID);

  writeComms(DEBUG_DELIMITER);
  // Print JSON string to serial
  String jsonStr = dumpMatchesToJson();
  writeCommsString(jsonStr);
  writeComms(DEBUG_DELIMITER);
  writeComms(isHunter);
  writeComms(DEBUG_DELIMITER);
}

void setActivationDelay() {
  if (commsAvailable()) {
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
  flushComms();
  invalidateTimer();
  setMotorOutput(0);
}

// void monitorTX()
// {
//   if (Serial1.available())
//   {
//     lastTxPacket += Serial1.read();
//   }
// }

// void monitorRX()
// {
//   if (Serial2.available())
//   {
//     lastRxPacket += Serial2.read();
//   }
// }

// void writeTxTransaction(byte command)
// {
//   Serial1.write(command);
// }

// void writeRxTransaction(byte command)
// {
//   Serial2.write(command);
// }

// void activationOvercharge() {
//   if(msgDelay == 0) {
//     comms().write(BATTLE_MESSAGE);
//   }
//   msgDelay = msgDelay + 1;

//   if (timerExpired()) {
//     if(overchargeStep == 0) {
//       if ((millis() % 20) == 0) {

//         buttonBrightness++;

//         float pwm_val = 255.0 * (1.0 - abs((2.0 * (buttonBrightness /
//         smoothingPoints)) - 1.0)); setAllLED(pwm_val);

//         if (buttonBrightness == 255) {
//           overchargeStep = 1;
//         }
//       }
//     } else if(overchargeStep == 1) {
//       setAllLED(0);
//       setTimer(200);
//       overchargeStep = 2;
//     } else if(overchargeStep == 2) {
//       setAllLED(0);
//       if(overchargeFlickers % 2 == 0) {
//         setLED(buttonLED, random(0,50));
//         setLED(loseLED, random(50,100));
//         setTimer(50);
//       } else {
//         setLED(winLED, random(0, 125));
//         setLED(interiorLED, random(200,255));
//         setTimer(50);
//       }
//       if(overchargeFlickers == 9) {
//         overchargeStep = 3;
//         overchargeFlickers = 0;
//         buttonBrightness = 255;
//         breatheUp = true;
//       } else {
//         overchargeFlickers = overchargeFlickers + 1;
//       }
//     } else if(overchargeStep == 3) {
//       if(overchargeFlickers < 2 && (millis() % 10) == 0) {
//         buttonBrightness--;

//         float pwm_val = 255.0 * (1.0 - abs((2.0 * (buttonBrightness /
//         smoothingPoints)) - 1.0)); setAllLED(pwm_val);

//         if (buttonBrightness == 0) {
//           overchargeStep = 4;
//         }
//       }
//     } else if(overchargeStep == 4) {
//       setTimer(idleLEDBreak/2);
//       overchargeStep = 0;
//       overchargeFlickers = 0;
//       buttonBrightness = 0;
//     }
//   }
// }

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
  primaryPresses += 1;
  setGraphLeft(primaryPresses % 7);
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