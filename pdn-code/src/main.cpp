#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <FastLED.h>
#include <HardwareSerial.h>
#include <OneButton.h>

#define primaryButtonPin 15
#define secondaryButtonPin 16
#define motor 17
#define TXt 41
#define TXr 40
#define RXt 39
#define RXr 38
#define displayLights 13
#define gripLights 21
#define displayCS 10
#define displayDC 9
#define displayRST 14


U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display(U8G2_R0, displayCS, displayDC, displayRST);
OneButton primary = OneButton(primaryButtonPin, true, true);
OneButton secondary = OneButton(secondaryButtonPin, true, true);

int primaryPresses = 0;
int secondaryPresses = 0;

void primaryButtonClick() {
  primaryPresses += 1;
}

void secondaryButtonClick() {
  secondaryPresses += 1;
}

void initializePins() {
  
  //init display
  pinMode(displayCS, OUTPUT);
  pinMode(displayDC, OUTPUT);
  digitalWrite(displayCS, 0);
  digitalWrite(displayDC, 0);	

  pinMode(motor, OUTPUT);

  pinMode(TXt, OUTPUT);
  pinMode(TXr, INPUT);

  pinMode(RXt, OUTPUT);
  pinMode(RXr, INPUT);

  pinMode(displayLights, OUTPUT);
  pinMode(gripLights, OUTPUT);
}

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

void setup(void) {
  initializePins();
  
  primary.attachClick(primaryButtonClick);
  secondary.attachClick(secondaryButtonClick);
  
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

  display.clearBuffer();
  drawDebugLabels();
  updatePrimaryButtonState();
  updateSecondaryButtonState();
    
  display.sendBuffer();
}