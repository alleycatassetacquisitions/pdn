#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <FastLED.h>
#include <HardwareSerial.h>
#include <OneButton.h>
#include <arduino-timer.h>
#include <EEPROM.h>

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

boolean isHunter = true;

byte deviceID = 49;

// DISPLAY

U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display(U8G2_R0, displayCS, displayDC, displayRST);

unsigned long frameBegin = 0;
unsigned long frameDuration = 0;

void drawDebugLabels();
void drawDebugState(char *button1State, char *button2State, char *txData, char *rxData, char *motorSpeed, char *led1Pattern, char *led2Pattern);
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
boolean displayLightsOnOff[numDisplayLights] = {true, true, true, true, true, true, true, true, true, true, true, true, true};
CRGB gripLights[numGripLights];
boolean gripLightsOnOff[numGripLights] = {true, true, true, true, true, true};

uint8_t displayColorIndex = 0;
uint8_t gripColorIndex = 0;

CRGBPalette16 bountyColors = CRGBPalette16(
    CRGB::Red,
    CRGB::Red,
    CRGB::Red,
    CRGB::Orange,
    CRGB::Red,
    CRGB::Red,
    CRGB::Red,
    CRGB::Orange,
    CRGB::Orange,
    CRGB::Red,
    CRGB::Red,
    CRGB::Red,
    CRGB::Orange,
    CRGB::Red,
    CRGB::Red,
    CRGB::Red);

CRGBPalette16 hunterColors = CRGBPalette16(
    CRGB::DarkGreen,
    CRGB::DarkGreen,
    CRGB::DarkGreen,
    CRGB::DarkBlue,
    CRGB::DarkGreen,
    CRGB::DarkGreen,
    CRGB::DarkGreen,
    CRGB::DarkBlue,
    CRGB::DarkBlue,
    CRGB::DarkGreen,
    CRGB::DarkGreen,
    CRGB::DarkGreen,
    CRGB::DarkBlue,
    CRGB::DarkGreen,
    CRGB::DarkGreen,
    CRGB::DarkGreen);

CRGBPalette16 currentPalette = hunterColors;

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
void writeComms(byte command);
void writeCommsString(String command);
byte readComms();
String readCommsString(char terminator);
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
unsigned long debugDelay = 3000;
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

void setTimer(long timerDelay);
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

// SERIAL COMMANDS
const char ENTER_DEBUG = '!';
const char START_GAME = '@';
const char SETUP_DEVICE = '#';
const char SET_ACTIVATION = '^';
const char CHECK_IN = '%';
const char DEBUG_DELIMITER = '-';

byte APP_STATE = QD_GAME;

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

// EEPROM

byte winPointsAddress = 0;
byte losePointsAddress = 0;

void updateScore(bool win);

bool requestSwitchAppState();
void flushGarbageData();
bool isValidMessage();
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

void initializePins()
{

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

void setup(void)
{

  initializePins();

  Serial.begin(19200, SERIAL_8N1);
  Serial1.begin(19200, SERIAL_8N1, TXr, TXt);

  Serial2.begin(19200, SERIAL_8N1, RXr, RXt);

  if (isHunter)
  {
    // comms = Serial1;
    currentPalette = hunterColors;
  }
  else
  {
    currentPalette = bountyColors;
    // comms = Serial2;
  }

  // primary.attachClick(primaryButtonClick);
  // primary.attachDoubleClick(primaryButtonDoubleClick);
  // primary.attachLongPressStop(primaryButtonLongPress);
  // secondary.attachClick(secondaryButtonClick);
  // secondary.attachDoubleClick(secondaryButtonDoubleClick);
  // secondary.attachLongPressStop(secondaryButtonLongPress);

  FastLED.addLeds<WS2812B, displayLightsPin, GRB>(displayLights, numDisplayLights).setCorrection(TypicalSMD5050);
  FastLED.addLeds<WS2812B, gripLightsPin, GRB>(gripLights, numGripLights);
  FastLED.setBrightness(65);

  display.begin();

  display.clearBuffer();
  display.setContrast(255);
  display.setFont(u8g2_font_smart_patrol_nbp_tf);
  drawDebugLabels();
  drawDebugState("OFF", "OFF", "N/A", "N/A", "00%", "OFF", "OFF");
  display.sendBuffer();

  uiRefresh.every(16, updateUi);
}

void loop(void)
{
  now = millis();
  uiRefresh.tick();

  if (APP_STATE == QD_GAME)
  {
    quickDrawGame();
  }
  else if (APP_STATE == DEBUG)
  {
    debugEvents();
  }
  checkForAppState();
}

// DISPLAY


// LEDS
byte leftIndex = 9;
byte rightIndex = 0;

void activationIdleAnimation(int brightness)
{
  if (random8() % 7 == 0) {
    displayLights[random8() % (numDisplayLights-1)] += ColorFromPalette(currentPalette, random8(), brightness, LINEARBLEND);
  }
  fadeToBlackBy(displayLights, numDisplayLights, 2);

  for (int i = 0; i < numGripLights; i++)
  {
    if (random8() % 65 == 0) {
      gripLights[i] += ColorFromPalette(currentPalette, random8(), brightness, LINEARBLEND);
    }
  }
  fadeToBlackBy(gripLights, numGripLights, 2);
}


void animateLights()
{

  if (QD_STATE == INITIATE)
  {
  }
  else if (QD_STATE == DORMANT)
  {
  }
  else if (QD_STATE == ACTIVATED)
  {
    if (breatheUp)
    {
      ledBrightness++;
    }
    else
    {
      ledBrightness--;
    }
    pwm_val = 255.0 * (1.0 - abs((2.0 * (ledBrightness / smoothingPoints)) - 1.0));

    if (ledBrightness == 255)
    {
      breatheUp = false;
    }
    else if (ledBrightness == 0)
    {
      breatheUp = true;
    }

    activationIdleAnimation((int)pwm_val);
  }
  else if (QD_STATE == HANDSHAKE)
  {
  }
  else if (QD_STATE == DUEL_ALERT)
  {
  }
  else if (QD_STATE == DUEL_COUNTDOWN)
  {
  }
  else if (QD_STATE == DUEL)
  {
  }
  else if (QD_STATE == WIN)
  {
  }
  else if (QD_STATE == LOSE)
  {
  }
}

byte screenCounter = 0;
bool updateUi(void *)
{

  animateLights();

  display.clearBuffer();

  display.setCursor(16, 32);

  if (QD_STATE == INITIATE)
  {
    display.print("INITIATE");
  }
  else if (QD_STATE == DORMANT)
  {
    display.print("DORMANT");
    display.setCursor(16, 48);
    display.print(u8x8_utoa(screenCounter++));
  }
  else if (QD_STATE == ACTIVATED)
  {
    display.print("ACTIVATED");
    display.setCursor(16, 48);
    display.print(u8x8_utoa(screenCounter++));
  }
  else if (QD_STATE == HANDSHAKE)
  {
    display.print("HANDSHAKE");
  }
  else if (QD_STATE == DUEL_ALERT)
  {
    display.print("ALERT");
  }
  else if (QD_STATE == DUEL_COUNTDOWN)
  {
    display.print("COUNTDOWN");
  }
  else if (QD_STATE == DUEL)
  {
    display.print("DUEL");
  }
  else if (QD_STATE == WIN)
  {
    display.print("WIN");
  }
  else if (QD_STATE == LOSE)
  {
    display.print("LOSE");
  }

  display.sendBuffer();
  FastLED.show();

  return true; // for our timer to continue repeating.
}

void setGraphRight(int value)
{
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

void setGraphLeft(int value)
{
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

void setTransmitLight(boolean on)
{
  displayLightsOnOff[12] = on;
}

// MOTOR

void setMotorOutput(int value)
{

  if (value > 255)
  {
    value = 255;
  }
  else if (value < 0)
  {
    value = 0;
  }

  analogWrite(motorPin, value);
}

// SERIAL

void writeComms(byte command) {
  if(isHunter) {
    Serial1.write(command);
  } else {
    Serial2.write(command);
  }
}

void writeCommsString(String command) {
  if(isHunter) {
    Serial1.println(command);
  } else {
    Serial2.println(command);
  }
}

byte readComms() {
  if(isHunter) {
    return Serial1.read();
  } else {
    return Serial2.read();
  }
}

String readCommsString(char terminator) {
  if(isHunter) {
    return Serial1.readStringUntil(terminator);
  } else { 
    return Serial2.readStringUntil(terminator);
  }
}

byte peekComms() {
  if(isHunter) {
    return Serial1.peek();
  } else {
    return Serial2.peek();
  }
}

bool commsAvailable() {
  return (isHunter && Serial1.available()) || (!isHunter && Serial2.available());
}

void quickDrawGame()
{
  if (QD_STATE == DORMANT)
  {
    if (!activationInitiated)
    {
      setupActivation();
    }

    if (shouldActivate() && !beginActivationSequence)
    {
      beginActivationSequence = true;
    }

    if (beginActivationSequence)
    {
      if (activationSequence())
      {
        updateQDState(ACTIVATED);
      }
    }
  }
  else if (QD_STATE == ACTIVATED)
  {
    // if(bvbDuel) {
    //   activationOvercharge();
    // } else {
    activationIdle();
    // }

    if (initiateHandshake())
    {
      updateQDState(HANDSHAKE);
    }
  }
  else if (QD_STATE == HANDSHAKE)
  {
    if (handshake())
    {
      updateQDState(DUEL_ALERT);
    }
    else if (handshakeTimedOut)
    {
      updateQDState(ACTIVATED);
    }
  }
  else if (QD_STATE == DUEL_ALERT)
  {
    alertDuel();
    if (alertCount == 10)
    {
      updateQDState(DUEL_COUNTDOWN);
    }
  }
  else if (QD_STATE == DUEL_COUNTDOWN)
  {
    duelCountdown();
    if (doBattle)
    {
      updateQDState(DUEL);
    }
  }
  else if (QD_STATE == DUEL)
  {
    duel();
    if (captured)
    {
      updateQDState(LOSE);
    }
    else if (wonBattle)
    {
      updateQDState(WIN);
    }
    else if (duelTimedOut)
    {
      updateQDState(ACTIVATED);
    }
  }
  else if (QD_STATE == WIN)
  {
    if (!reset)
    {
      duelOver();
    }
    else
    {
      updateScore(true);
      updateQDState(DORMANT);
    }
  }
  else if (QD_STATE == LOSE)
  {
    if (!reset)
    {
      duelOver();
    }
    else
    {
      updateScore(false);
      updateQDState(DORMANT);
    }
  }

  commitQDState();
}

void checkForAppState()
{

  if (requestSwitchAppState())
  {
    String command = fetchDebugCommand();
    if (validateCommand(command, ENTER_DEBUG))
    {
      Serial.println("Switching to Debug");
      APP_STATE = DEBUG;
      writeComms(DEBUG_DELIMITER);
      writeComms(deviceID);
      resetState();
    }
    else if (validateCommand(command, START_GAME) && APP_STATE == DEBUG)
    {
      Serial.println("Switching to Game");
      APP_STATE = QD_GAME;
      QD_STATE = DORMANT;
      resetState();
    }
  }

  flushGarbageData();
}

byte brt = 255;

void debugEvents()
{
  // todo: Debug Display

  if (commandReceived())
  {
    String command = fetchDebugCommand();
    Serial.print("Command Received: ");
    Serial.println(command);
    if (validateCommand(command, SETUP_DEVICE))
    {
      setupDevice();
    }
    else if (validateCommand(command, CHECK_IN))
    {
      Serial.println("CHECK_IN");
      checkInDevice();
    }
    else if (validateCommand(command, SET_ACTIVATION))
    {
      Serial.println("SET_ACTIVATION");
      setActivationDelay();
    }
  }
}

void setupActivation()
{
  if (isHunter)
  {
    // hunters have minimal activation delay
    setTimer(5000);
  }
  else
  {
    if (debugDelay > 0)
    {
      setTimer(debugDelay);
    }
    else if (bvbDuel)
    {
      randomSeed(analogRead(A0));
      setTimer(random(overchargeDelay[0], overchargeDelay[1]));
    }
    else
    {
      randomSeed(analogRead(A0));
      long timer = random(bountyDelay[0], bountyDelay[1]);
      Serial.print("timer: ");
      Serial.println(timer);
      setTimer(timer);
    }
  }

  activationInitiated = true;
}

bool shouldActivate()
{
  return timerExpired();
}

byte activateMotorCount = 0;
bool activateMotor = false;

bool activationSequence()
{
  if (timerExpired())
  {
    if (activateMotorCount < 20)
    {
      if (activateMotor)
      {
        setMotorOutput(255);
      }
      else
      {
        setMotorOutput(0);
      }

      setTimer(100);
      activateMotorCount++;
      activateMotor = !activateMotor;
      return false;
    }
    else
    {
      activateMotorCount = 0;
      activateMotor = false;
      return true;
    }
  }
  return false;
}

void activationIdle()
{
  // msgDelay was to prevent this from broadcasting every loop.
  if(msgDelay == 0) {
    writeComms(BATTLE_MESSAGE);
  }
  msgDelay = msgDelay + 1;
}

bool initiateHandshake()
{
  if (commsAvailable() > 0 && peekComms() == BATTLE_MESSAGE)
  {
    readComms();
    writeComms(BATTLE_MESSAGE);
    return true;
  }

  return false;
}

bool handshake()
{

  if (handshakeState == 0)
  {

    setTimer(HANDSHAKE_DELAY);
    handshakeState = 1;
  }
  else if (handshakeState == 1)
  {

    if (timerExpired())
    {
      handshakeState = 2;
    }
  }
  else if (handshakeState == 2)
  {

    setTimer(HANDSHAKE_TIMEOUT);
    handshakeState = 3;
  }
  else if (handshakeState == 3)
  {
    if (timerExpired())
    {
      FastLED.setBrightness(0);
      handshakeTimedOut = true;
      return false;
    }

    if (isHunter)
    {
      writeComms(HUNTER_SHAKE);
    }
    else
    {
      writeComms(BOUNTY_SHAKE);
    }
  }

  byte command = peekComms();
  if (isHunter && command == BOUNTY_SHAKE)
  {
    writeComms(HUNTER_SHAKE);
    return true;
  }
  else if (!isHunter && command == BOUNTY_SHAKE || command == HUNTER_SHAKE)
  {
    bvbDuel = (command == BOUNTY_SHAKE);
    writeComms(BOUNTY_SHAKE);
    return true;
  }

  return false;
}

void alertDuel()
{
  if (alertCount == 0)
  {
    buttonBrightness = 255;
  }

  if (timerExpired())
  {
    if (buttonBrightness == 255)
    {
      buttonBrightness = 0;
    }
    else
    {
      buttonBrightness = 255;
    }

    alertCount++;
    FastLED.setBrightness(buttonBrightness);
    setTimer(alertFlashTime);
  }
}

void duelCountdown()
{
  if (timerExpired())
  {
    if (countdownStage == 4)
    {
      setTimer(FOUR);
      FastLED.setBrightness(255);
      countdownStage = 3;
    }
    else if (countdownStage == 3)
    {
      setTimer(THREE);
      // setLED(buttonLED, 0);
      // setLED(interiorLED, 0);
      countdownStage = 2;
    }
    else if (countdownStage == 2)
    {
      setTimer(TWO);
      // setLED(loseLED, 0);
      countdownStage = 1;
    }
    else if (countdownStage == 1)
    {
      setTimer(ONE);
      // setLED(winLED, 0);
      countdownStage = 0;
    }
    else if (countdownStage == 0)
    {
      doBattle = true;
    }
  }
}

void duel()
{
  FastLED.setBrightness(255);

  if (peekComms() == ZAP)
  {
    readComms();
    writeComms(YOU_DEFEATED_ME);
    captured = true;
    return;
  }
  else if (peekComms() == YOU_DEFEATED_ME)
  {
    readComms();
    wonBattle = true;
    return;
  }
  else if (peekComms() != -1)
  {
    readComms();
  }

  if (isButtonPressed() && sendZapSignal)
  {
    sendZapSignal = false;
    writeComms(ZAP);
  }

  if (startDuelTimer)
  {
    setTimer(DUEL_TIMEOUT);
    startDuelTimer = false;
  }

  if (timerExpired())
  {
    FastLED.setBrightness(0);
    bvbDuel = false;
    duelTimedOut = true;
  }
}

void duelOver()
{
  if (startBattleFinish)
  {
    startBattleFinish = false;
    FastLED.setBrightness(0);
    setMotorOutput(255);
  }

  if (timerExpired())
  {
    setTimer(500);
    if (finishBattleBlinkCount < FINISH_BLINKS)
    {
      if (finishBattleBlinkCount % 2 == 0)
      {
        setMotorOutput(0);
      }
      else
      {
        setMotorOutput(255);
      }
      finishBattleBlinkCount = finishBattleBlinkCount + 1;
    }
    else
    {
      setMotorOutput(0);
      reset = true;
    }
  }
}

bool isButtonPressed()
{
  return digitalRead(primaryButtonPin) == LOW || digitalRead(secondaryButtonPin) == LOW;
}

// EEPROM functions
void updateScore(boolean win)
{
  byte address;
  if (win)
  {
    address = winPointsAddress;
  }
  else
  {
    address = losePointsAddress;
  }
  byte currentScore = EEPROM.read(address);
  currentScore = currentScore + 1;
  EEPROM.write(address, currentScore);
}

// serial functions

void flushComms()
{
  Serial1.flush();
  Serial2.flush();
}

bool requestSwitchAppState()
{
  if (commsAvailable())
  {
    char command = (char)peekComms();
    Serial.print("Checking App State: ");
    Serial.print(" Current buffer size: ");
    Serial.print(commsAvailable());
    Serial.print(" Command: ");
    Serial.print(command);
    Serial.print(" As Byte: ");
    Serial.println(peekComms());
    return (command == ENTER_DEBUG || command == START_GAME);
  }
  else
  {
    return false;
  }
}

void flushGarbageData()
{
  while (commsAvailable() && !isValidMessage())
  {
    flushComms();
  }
}

bool isValidMessage()
{
  byte command = peekComms();
  if ((char)command == ENTER_DEBUG || (char)command == START_GAME)
  {
    return true;
  }

  if (APP_STATE == DEBUG)
  {
    Serial.print("Validating DEBUG: ");
    Serial.println((char)command);
    return ((char)command == SETUP_DEVICE || (char)command == SET_ACTIVATION || (char)command == CHECK_IN || (char)command == DEBUG_DELIMITER);
  }
  else
  {
    if (QD_STATE == ACTIVATED)
    {
      return command == BATTLE_MESSAGE;
    }
    else if (QD_STATE == HANDSHAKE)
    {
      return (command == BOUNTY_SHAKE || command == HUNTER_SHAKE);
    }
    else if (QD_STATE == DUEL)
    {
      return (command == ZAP || command == YOU_DEFEATED_ME);
    }
  }

  return false;
}

bool commandReceived()
{
  if (commsAvailable())
  {
    char command = (char)peekComms();
    return (command == SETUP_DEVICE || command == SET_ACTIVATION || command == CHECK_IN);
  }
  else
  {
    return false;
  }
}

bool validateCommand(String a, char b)
{
  char array[a.length() + 1];
  a.toCharArray(array, a.length() + 1);
  return b == array[0];
}

String fetchDebugData()
{
  if (commsAvailable())
  {
    return readCommsString('\n');
  }
  else
  {
    return "";
  }
}

String fetchDebugCommand()
{
  if (commsAvailable())
  {
    return readCommsString(DEBUG_DELIMITER);
  }
  else
  {
    return "";
  }
}

void setupDevice()
{
  EEPROM.put(winPointsAddress, 0);
  EEPROM.put(losePointsAddress, 0);
  writeComms(deviceID);
}

void checkInDevice()
{
  byte winPoints;
  byte losePoints;

  EEPROM.get(winPointsAddress, winPoints);
  EEPROM.get(losePointsAddress, losePoints);

  Serial.print(" Checking In: ");
  Serial.print(deviceID);
  Serial.print(" Win Points: ");
  Serial.print(winPoints);
  Serial.print(" Losses: ");
  Serial.println(losePoints);

  writeComms(deviceID);

  writeComms(DEBUG_DELIMITER);
  writeComms(winPoints);
  writeComms(DEBUG_DELIMITER);
  writeComms(losePoints);
  writeComms(DEBUG_DELIMITER);
  writeComms(isHunter);
  writeComms(DEBUG_DELIMITER);
}

void setActivationDelay()
{
  String activationDelay = fetchDebugData();
  debugDelay = strtoul(activationDelay.c_str(), NULL, 10);
  Serial.print("Set Activation Delay: ");
  Serial.println(debugDelay);
}

// time functions
unsigned long getElapsedTime()
{
  return now - start;
}

bool timerExpired()
{
  return timer <= getElapsedTime();
}

void invalidateTimer()
{
  timer = 0;
}

void setTimer(long timerDelay)
{
  timer = timerDelay;
  start = millis();
}

// state functions
void updateQDState(byte futureState)
{
  newState = futureState;
  stateChangeReady = true;
}

void commitQDState()
{
  if (stateChangeReady)
  {
    QD_STATE = newState;
    resetState();
  }
  stateChangeReady = false;
}

void resetState()
{
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

//         float pwm_val = 255.0 * (1.0 - abs((2.0 * (buttonBrightness / smoothingPoints)) - 1.0));
//         setAllLED(pwm_val);

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

//         float pwm_val = 255.0 * (1.0 - abs((2.0 * (buttonBrightness / smoothingPoints)) - 1.0));
//         setAllLED(pwm_val);

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

void drawDebugLabels()
{
  display.drawStr(16, 10, "BTN 1:");
  display.drawStr(16, 20, "BTN 2:");
  display.drawStr(16, 30, "TX:");
  display.drawStr(16, 40, "RX:");
  display.drawStr(16, 50, "MOTOR:");
  display.drawStr(16, 60, "FRAME:");
}

void drawDebugState(char *button1State, char *button2State, char *txData, char *rxData, char *motorSpeed, char *led1Pattern, char *fps)
{
  display.drawStr(80, 10, button1State);
  display.drawStr(80, 20, button2State);
  display.drawStr(80, 30, txData);
  display.drawStr(80, 40, rxData);
  display.drawStr(80, 50, motorSpeed);
  display.drawStr(80, 60, fps);
}

void updatePrimaryButtonState()
{
  display.setCursor(80, 10);
  display.print(u8x8_u8toa(primaryPresses, 3));
}

void updateSecondaryButtonState()
{
  display.setCursor(80, 20);
  display.print(u8x8_u8toa(secondaryPresses, 3));
}

void updateMotorState()
{
  display.setCursor(80, 50);
  display.print(u8x8_u8toa(motorSpeed, 3));
}

void updateTXState()
{
  display.setCursor(80, 30);
  display.print(u8x8_u8toa(lastTxPacket, 3));
}

void updateRXState()
{
  display.setCursor(80, 40);
  display.print(u8x8_u8toa(lastRxPacket, 3));
}

void updateFramerate()
{
  int duration = frameDuration;
  display.setCursor(80, 60);
  display.print(u8x8_u8toa(duration, 3));
}

// BUTTONS

void primaryButtonClick()
{
  primaryPresses += 1;
  setGraphLeft(primaryPresses % 7);
}

void secondaryButtonClick()
{
  secondaryPresses += 1;
  setGraphRight(secondaryPresses % 7);
}

void primaryButtonDoubleClick()
{
  setTransmitLight(!displayLightsOnOff[12]);
}

void secondaryButtonDoubleClick()
{
  if (motorSpeed == 0)
  {
    motorSpeed = 65;
  }
  else if (motorSpeed == 65)
  {
    motorSpeed = 155;
  }
  else if (motorSpeed == 155)
  {
    motorSpeed = 255;
  }
  else if (motorSpeed == 255)
  {
    motorSpeed = 0;
  }
}

void primaryButtonLongPress()
{
  isHunter = !isHunter;

  if (isHunter)
  {
    currentPalette = hunterColors;
  }
  else
  {
    currentPalette = bountyColors;
  }
}

void secondaryButtonLongPress()
{
  if (isHunter)
  {
    writeTxTransaction(1);
  }
  else
  {
    writeRxTransaction(1);
  }
}