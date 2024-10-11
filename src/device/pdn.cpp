#include "../../include/device/pdn.hpp"

PDN *PDN::GetInstance() {
    static PDN instance;
    return &instance;
}

PDN::~PDN() {
}


PDN::PDN() : display(displayCS, displayDC, displayRST),
             vibrationMotor(motorPin),
             primary(primaryButtonPin, true, true),
             secondary(secondaryButtonPin, true, true),
             displayLights(numDisplayLights),
             gripLights(numGripLights) {
};

int PDN::begin() {
    initializePins();

    serialOut.begin();
    serialIn.begin();

    return 1;
}

void PDN::initializePins() {
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

string PDN::getDeviceId() {
    return deviceId;
}

HWSerialWrapper* PDN::outputJack() {
    return &serialOut;
}

HWSerialWrapper* PDN::inputJack() {
    return &serialIn;
}

void PDN::tick() {
    primary.tick();
    secondary.tick();
}

void PDN::setGlobablLightColor(PDNColor color) {
    FastLED.showColor(CRGB(color.red, color.green, color.blue), 255);
};

void PDN::setGlobalBrightness(int brightness) {
    FastLED.setBrightness(brightness);
};

void PDN::fadeLightsBy(int whichLights, int value) {
    switch(whichLights) {
        case GLOBAL: {
            displayLights.fade(value);
            gripLights.fade(value);
            break;
        }
        case DISPLAY_LIGHTS: {
            displayLights.fade(value);
            break;
        }
        case GRIP_LIGHTS: {
            gripLights.fade(value);
            break;
        }
    }
}

void PDN::addToLight(int whichLights, int ledNum, PDNColor color) {
    switch(whichLights) {
        case GLOBAL:
            break;
        case DISPLAY_LIGHTS: {
            displayLights.addToLight(ledNum, CRGB(color.red, color.green, color.blue));
            break;
        }
        case GRIP_LIGHTS: {
            gripLights.addToLight(ledNum, CRGB(color.red, color.green, color.blue));
            break;
        }
    }


}

void PDN::setButtonClick(int whichButton, parameterizedCallbackFunction newFunction, void *parameter) {
    switch(whichButton) {
        case PRIMARY_BUTTON: {
            primary.attachClick(newFunction, parameter);
            break;
        }
        case SECONDARY_BUTTON: {
            secondary.attachClick(newFunction, parameter);
            break;
        }
    }
}

void PDN::removeButtonCallbacks() {
    primary.reset();
    secondary.reset();
}

void PDN::setVibration(int value) {
    vibrationMotor.setIntensity(value);
}

int PDN::getCurrentVibrationIntensity() {
    return vibrationMotor.getIntensity();
}

void PDN::setDeviceId(string deviceId) {
    this->deviceId = deviceId;
}

void PDN::drawText(char *text, int xStart, int yStart) {
    display.drawText(text, xStart, yStart);
}

void PDN::drawImage(Image image, int xStart, int yStart) {
    display.drawImage(image, xStart, yStart);
}

