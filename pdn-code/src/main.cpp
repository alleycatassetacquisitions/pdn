#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <FastLED.h>
#include <HardwareSerial.h>
#include <OneButton.h>

#define primaryButtonPin 15
#define secondaryButtonPin 16
#define motorPin 17
#define TXt 41
#define TXr 40
#define RXt 39
#define RXr 38
#define displayLightsPin 13
#define gripLightsPin 21
#define displayCS 10
#define displayDC 9
#define displayRST 14

#define numDisplayLights 13
#define numGripLights 6

boolean isHunter = true;

//DISPLAY

U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display(U8G2_R0, displayCS, displayDC, displayRST);

void drawDebugLabels();
void drawDebugState(char* button1State, char* button2State, char* txData, char* rxData, char* motorSpeed, char* led1Pattern, char* led2Pattern);
void updatePrimaryButtonState();
void updateSecondaryButtonState();
void updateMotorState();
void updateDisplayLEDState();
void updateTXState();
void updateRXState();

//BUTTONS 

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

// LEDs

CRGB displayLights[numDisplayLights];
boolean displayLightsOnOff[numDisplayLights] = {false, false, false, false, false, false, false, false, false, false, false, false, false};
CRGB gripLights[numGripLights];

uint8_t displayColorIndex = 0;
uint8_t gripColorIndex = 0; 

const TProgmemRGBPalette16* hunterColors = &ForestColors_p;
const TProgmemRGBPalette16* bountyColors = &HeatColors_p;
CRGBPalette16 currentPalette = *hunterColors;


void fillDisplayLights(int colorStartIndex);
void fillGripLights(int colorStartIndex);
void animateDisplayLights();
void animateGripLights();

void setGraphRight(int value);
void setGraphLeft(int value);
void setTransmitLight(boolean on);

//MOTOR

void setMotorOutput(int value);
int motorSpeed = 0;

//SERIAL

byte lastTxPacket = 0;
byte lastRxPacket = 0;

void monitorTX();
void monitorRX();

void writeTxTransaction(byte command);
void writeRxTransaction(byte command);

void initializePins() {
  
  //init display
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

  Serial1.begin(9600, SERIAL_8N1, TXr, TXt);

  Serial2.begin(9600, SERIAL_8N1, RXr, RXt);
  
  primary.attachClick(primaryButtonClick);
  primary.attachDoubleClick(primaryButtonDoubleClick);
  primary.attachLongPressStop(primaryButtonLongPress);
  secondary.attachClick(secondaryButtonClick);
  secondary.attachDoubleClick(secondaryButtonDoubleClick);
  secondary.attachLongPressStop(secondaryButtonLongPress);

  FastLED.addLeds<WS2812B, displayLightsPin, GRB>(displayLights, numDisplayLights);
  FastLED.addLeds<WS2812B, gripLightsPin, GRB>(gripLights, numGripLights);
  FastLED.setBrightness(65);
  
  display.begin();

  display.clearBuffer();
  display.setContrast(255);
  display.setFont(u8g2_font_pressstart2p_8f);
  drawDebugLabels();
  drawDebugState("OFF", "OFF", "N/A", "N/A", "00%", "OFF", "OFF");
  display.sendBuffer();
}

void loop(void) {
  primary.tick();
  secondary.tick();

  monitorRX();
  monitorTX();

  display.clearBuffer();
  drawDebugLabels();
  updatePrimaryButtonState();
  updateSecondaryButtonState();
  updateMotorState();
  updateDisplayLEDState();
  updateTXState();
  updateRXState();

  animateDisplayLights();

  setMotorOutput(motorSpeed);
  display.sendBuffer();
  FastLED.show();
}

//DISPLAY

void drawDebugLabels() {
  display.drawStr(32, 8, "PDN 3.0");
  display.drawStr(16, 16, "BTN 1:");
  display.drawStr(16, 24, "BTN 2:");
  display.drawStr(16, 32, "TX:");
  display.drawStr(16, 40, "RX:");
  display.drawStr(16, 48, "MOTOR:");
  display.drawStr(16, 56, "LED 1:");
  display.drawStr(16, 64, "LED 2:");
}

void drawDebugState(char* button1State, char* button2State, char* txData, char* rxData, char* motorSpeed, char* led1Pattern, char* led2Pattern) {
  display.drawStr(80, 16, button1State);
  display.drawStr(80, 24, button2State);
  display.drawStr(80, 32, txData);
  display.drawStr(80, 40, rxData);
  display.drawStr(80, 48, motorSpeed);
  display.drawStr(80, 56, led1Pattern);
  display.drawStr(80, 64, led2Pattern);
}

void updatePrimaryButtonState() {
  display.setCursor(80, 16);
  display.print(u8x8_u8toa(primaryPresses, 3));
}

void updateSecondaryButtonState() {
  display.setCursor(80, 24);
  display.print(u8x8_u8toa(secondaryPresses, 3));
}

void updateMotorState() {
  display.setCursor(80, 48);
  display.print(u8x8_u8toa(motorSpeed, 3));
}

void updateDisplayLEDState() {
  display.setCursor(80, 56);
  if(isHunter) {
    display.print("HNTR");
  } else {
    display.print("BNTY");
  }
}

void updateTXState() {
  display.setCursor(80, 32);
  display.print(u8x8_u8toa(lastTxPacket, 3));
}

void updateRXState() {
  display.setCursor(80, 40);
  display.print(u8x8_u8toa(lastRxPacket, 3));
}

//BUTTONS

void primaryButtonClick() {
  primaryPresses += 1;
  setGraphLeft(primaryPresses % 7);
}

void secondaryButtonClick() {
  secondaryPresses += 1;
  setGraphRight(secondaryPresses % 7);
}

void primaryButtonDoubleClick(){
  setTransmitLight(!displayLightsOnOff[12]);
}

void secondaryButtonDoubleClick(){
  if(motorSpeed == 0) {
    motorSpeed = 65;
  } else if(motorSpeed == 65) {
    motorSpeed = 155;
  } else if(motorSpeed == 155) {
    motorSpeed = 255;
  } else if(motorSpeed == 255) {
    motorSpeed = 0;
  }
}

void primaryButtonLongPress() {
  isHunter = !isHunter;

  if(isHunter) {
    currentPalette = *hunterColors;
  } else {
    currentPalette = *bountyColors;
  }
}

void secondaryButtonLongPress() {
  if(isHunter) {
    writeTxTransaction(1);
  } else {
    writeRxTransaction(1);
  }
}

//LEDS

void fillDisplayLights(int colorStartIndex){
  
  for(int i = 0; i < numDisplayLights; i++) {
    if(displayLightsOnOff[i]) {
      displayLights[i] = ColorFromPalette(currentPalette, colorStartIndex, 65, LINEARBLEND);
    } else {
      displayLights[i] = CRGB::Black;
    }
    colorStartIndex+=3;
  }
}

void fillGripLights(int colorStartIndex){

}

void animateDisplayLights(){
  displayColorIndex += 1;
  fillDisplayLights(displayColorIndex);
}

void animateGripLights(){

}

void setGraphRight(int value) {
  displayLightsOnOff[11] = value > 0;
  displayLightsOnOff[10] = value > 1;
  displayLightsOnOff[9] = value > 2;
  displayLightsOnOff[8] = value > 3;
  displayLightsOnOff[7] = value > 4;
  displayLightsOnOff[6] = value > 5;
}

void setGraphLeft(int value) {
  displayLightsOnOff[0] = value > 0;
  displayLightsOnOff[1] = value > 1;
  displayLightsOnOff[2] = value > 2;
  displayLightsOnOff[3] = value > 3;
  displayLightsOnOff[4] = value > 4;
  displayLightsOnOff[5] = value > 5;
}

void setTransmitLight(boolean on) {
  displayLightsOnOff[12] = on;
}

//MOTOR

void setMotorOutput(int value) {

  if(value > 255) {
    value = 255;
  } else if(value < 0) {
    value = 0;
  }

  analogWrite(motorPin, value);
}

//SERIAL

void monitorTX() {
  if(Serial1.available()) {
    lastTxPacket = Serial1.read();
  }
}

void monitorRX() {
  if(Serial2.available()) {
    lastRxPacket = Serial2.read();
  }
}

void writeTxTransaction(byte command) {
  lastRxPacket++;
  Serial1.write(lastRxPacket);
}

void writeRxTransaction(byte command) {
  lastTxPacket++;
  Serial2.write(lastTxPacket);
}