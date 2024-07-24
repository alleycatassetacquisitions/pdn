#include "../include/device/device.hpp"

Device::Device() :
    display(U8G2_R0, displayCS, displayDC, displayRST), 
    vibrationMotor(motorPin),
    primary(primaryButtonPin, true, true),
    secondary(secondaryButtonPin, true, true) {                       
};

int Device::begin() {
    initializePins();
        
    Serial1.begin(BAUDRATE, SERIAL_8E2, TXr, TXt, true);

    Serial2.begin(BAUDRATE, SERIAL_8E2, RXr, RXt, true);

    display.begin();

    return 1;   
}

void Device::initializePins() {

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

void Device::attachPrimaryButtonClick(callbackFunction click) {
    primary.attachClick(click);
}

void Device::attachPrimaryButtonDoubleClick(callbackFunction doubleClick) {
    primary.attachDoubleClick(doubleClick);
}

void Device::attachPrimaryButtonLongPress(callbackFunction longPress) {
    primary.attachLongPressStop(longPress);
}

void Device::attachSecondaryButtonClick(callbackFunction click) {
    secondary.attachClick(click);
}

void Device::attachSecondaryButtonDoubleClick(callbackFunction doubleClick) {
    secondary.attachDoubleClick(doubleClick);
}

void Device::attachSecondaryButtonLongPress(callbackFunction longPress) {
    secondary.attachLongPressStop(longPress);
}

HardwareSerial Device::outputJack() {
    return Serial1;
}

HardwareSerial Device::inputJack() {
    return Serial2;
}