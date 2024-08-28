#include "../include/device/device.hpp"

Device *Device::GetInstance()
{
    static Device instance;
    return &instance;
}


Device::Device() :
    display(displayCS, displayDC, displayRST), 
    vibrationMotor(motorPin),
    primary(primaryButtonPin, true, true),
    secondary(secondaryButtonPin, true, true),
    displayLights(numDisplayLights),
    gripLights(numGripLights) 
{


};

int Device::begin() 
{

    initializePins();
        
    Serial1.begin(BAUDRATE, SERIAL_8E2, TXr, TXt, true);

    Serial2.begin(BAUDRATE, SERIAL_8E2, RXr, RXt, true);

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

Haptics Device::getVibrator()
{
    return vibrationMotor;
}

Display Device::getDisplay()
{
    return display;
}

DisplayLights Device::getDisplayLights()
{
    return displayLights;
}

GripLights Device::getGripLights()
{
    return gripLights;
}

void Device::clearComms()
{
    outputJack().flush();
    inputJack().flush();
}

void Device::flushComms()
{

}

HardwareSerial Device::outputJack() {
    return Serial1;
}

HardwareSerial Device::inputJack() {
    return Serial2;
}

void Device::tick()
{
    primary.tick();
    secondary.tick();
}

void Device::setGlobablLightColor(CRGB color)
{
    FastLED.showColor(color, 255);
};

void Device::setGlobalBrightness(int brightness)
{
    FastLED.setBrightness(brightness);
};

