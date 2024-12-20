#include "device/pdn.hpp"

PDN *PDN::GetInstance() {
    static PDN instance;
    return &instance;
}

PDN::~PDN() {
}


PDN::PDN() : display(displayCS, displayDC, displayRST),
             haptics(motorPin),
             primary(primaryButtonPin),
             secondary(secondaryButtonPin),
             displayLights(numDisplayLights),
             gripLights(numGripLights) {
    this->PDN::initializePins();
};

int PDN::begin() {

    serialOut.begin();
    serialIn.begin();

    FastLED.clear();
    FastLED.show();

    //Initialize PSRAM which can be used as
    //extra heap space by allocating using
    //ps_malloc instead of malloc
    if(psramInit())
    {
        Serial.println("PSRAM initialized");
    }

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

void PDN::loop() {
    primary.loop();
    secondary.loop();

    FastLED.show();
}

void PDN::onStateChange() {
    FastLED.clear();
}


void PDN::setGlobablLightColor(LEDColor color) {
    FastLED.showColor(CRGB(color.red, color.green, color.blue), 255);
};

void PDN::setGlobalBrightness(int brightness) {
    FastLED.setBrightness(brightness);
};

void PDN::fadeLightsBy(LightIdentifier whichLights, int value) {
    switch(whichLights) {
        case LightIdentifier::GLOBAL: {
            displayLights.fade(value);
            gripLights.fade(value);
            break;
        }
        case LightIdentifier::DISPLAY_LIGHTS: {
            displayLights.fade(value);
            break;
        }
        case LightIdentifier::GRIP_LIGHTS: {
            gripLights.fade(value);
            break;
        }
        case LightIdentifier::TRANSMIT_LIGHT: {
            break;
        }
    }
}

void PDN::addToLight(LightIdentifier whichLights, int ledNum, LEDColor color) {
    switch(whichLights) {
        case LightIdentifier::GLOBAL:
            break;
        case LightIdentifier::DISPLAY_LIGHTS: {
            displayLights.addToLight(ledNum, CRGB(color.red, color.green, color.blue));
            break;
        }
        case LightIdentifier::GRIP_LIGHTS: {
            gripLights.addToLight(ledNum, CRGB(color.red, color.green, color.blue));
            break;
        }
        case LightIdentifier::TRANSMIT_LIGHT: {
            displayLights.setTransmitLight(CRGB(color.red, color.green, color.blue));
            break;
        }
    }
}

void PDN::setLight(LightIdentifier whichLights, int ledNum, LEDColor color) {
    switch(whichLights) {
        case LightIdentifier::GLOBAL:
            break;
        case LightIdentifier::DISPLAY_LIGHTS: {
            displayLights.setLight(ledNum, CRGB(color.red, color.green, color.blue));
            break;
        }
        case LightIdentifier::GRIP_LIGHTS: {
            gripLights.setLight(ledNum, CRGB(color.red, color.green, color.blue));
            break;
        }
        case LightIdentifier::TRANSMIT_LIGHT: {
            displayLights.setTransmitLight(CRGB(color.red, color.green, color.blue));
            break;
        }
    }
}



//Button Functions

void PDN::setButtonClick(ButtonInteraction interactionType, ButtonIdentifier whichButton, callbackFunction newFunction) {
    switch(interactionType) {
        case ButtonInteraction::CLICK: {
            attachSingleClick(whichButton, newFunction);
            break;
        }
        case ButtonInteraction::RELEASE: {
            attachLongPressRelease(whichButton, newFunction);
            break;
        }
        case ButtonInteraction::PRESS: {
            attachPress(whichButton, newFunction);
            break;
        }
        case ButtonInteraction::DOUBLE_CLICK: {
            attachDoubleClick(whichButton, newFunction);
            break;
        }
        case ButtonInteraction::MULTI_CLICK: {
            attachMultiClick(whichButton, newFunction);
            break;
        }
        case ButtonInteraction::LONG_PRESS: {
            attachLongPress(whichButton, newFunction);
            break;
        }
        case ButtonInteraction::DURING_LONG_PRESS: {
            attachDuringLongPress(whichButton, newFunction);
            break;
        }
    }
}



void PDN::setButtonClick(ButtonInteraction interactionType, ButtonIdentifier whichButton,
    parameterizedCallbackFunction newFunction, void *parameter) {
    switch(interactionType) {
        case ButtonInteraction::CLICK: {
            attachSingleClick(whichButton, newFunction, parameter);
            break;
        }
        case ButtonInteraction::RELEASE: {
            attachLongPressRelease(whichButton, newFunction, parameter);
            break;
        }
        case ButtonInteraction::PRESS: {
            attachPress(whichButton, newFunction, parameter);
            break;
        }
        case ButtonInteraction::DOUBLE_CLICK: {
            attachDoubleClick(whichButton, newFunction, parameter);
            break;
        }
        case ButtonInteraction::MULTI_CLICK: {
            attachMultiClick(whichButton, newFunction, parameter);
            break;
        }
        case ButtonInteraction::LONG_PRESS: {
            attachLongPress(whichButton, newFunction, parameter);
            break;
        }
        case ButtonInteraction::DURING_LONG_PRESS: {
            attachDuringLongPress(whichButton, newFunction, parameter);
            break;
        }
    }
}

void PDN::removeButtonCallbacks(ButtonIdentifier whichButton) {
    getButton(whichButton)->removeButtonCallbacks();
}

bool PDN::isLongPressed(ButtonIdentifier whichButton) {
    return getButton(whichButton)->isLongPressed();
}

unsigned long PDN::longPressedMillis(ButtonIdentifier whichButton) {
    return getButton(whichButton)->longPressedMillis();
}

Display * PDN::invalidateScreen() {
    return display.invalidateScreen();
}

void PDN::render() {
    display.render();
}

Display* PDN::drawImage(Image image, int xStart, int yStart) {
    return display.drawImage(image, xStart, yStart);
}

void PDN::setVibration(int value) {
    haptics.setIntensity(value);
}

int PDN::getCurrentVibrationIntensity() {
    return haptics.getIntensity();
}

void PDN::setDeviceId(string deviceId) {
    this->deviceId = deviceId;
}

Display* PDN::drawImage(Image image) {
    return display.drawImage(image);
}

Display* PDN::drawText(const char *text, int xStart, int yStart) {
    return display.drawText(text, xStart, yStart);
}

Display* PDN::drawText(const char *text) {
    return display.drawText(text);
}

PDNButton * PDN::getButton(ButtonIdentifier whichButton) {
    switch(whichButton) {
        case ButtonIdentifier::PRIMARY_BUTTON:
            return &primary;
        case ButtonIdentifier::SECONDARY_BUTTON:
            return &secondary;
    }
}

void PDN::attachSingleClick(ButtonIdentifier whichButton, callbackFunction newFunction) {
    getButton(whichButton)->setButtonSingleClick(newFunction);
}

void PDN::attachSingleClick(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void *parameter) {
    getButton(whichButton)->setButtonSingleClick(newFunction, parameter);
}

void PDN::attachDoubleClick(ButtonIdentifier whichButton, callbackFunction newFunction) {
    getButton(whichButton)->setButtonDoubleClick(newFunction);
}

void PDN::attachDoubleClick(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void *parameter) {
    getButton(whichButton)->setButtonDoubleClick(newFunction, parameter);
}

void PDN::attachMultiClick(ButtonIdentifier whichButton, callbackFunction newFunction) {
    getButton(whichButton)->setButtonMultiClick(newFunction);
}

void PDN::attachMultiClick(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void *parameter) {
    getButton(whichButton)->setButtonMultiClick(newFunction, parameter);
}

void PDN::attachPress(ButtonIdentifier whichButton, callbackFunction newFunction) {
    getButton(whichButton)->setButtonPress(newFunction);
}

void PDN::attachPress(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void *parameter) {
    getButton(whichButton)->setButtonPress(newFunction, parameter);
}

void PDN::attachLongPress(ButtonIdentifier whichButton, callbackFunction newFunction) {
    getButton(whichButton)->setButtonLongPress(newFunction);
}

void PDN::attachLongPress(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void *parameter) {
    getButton(whichButton)->setButtonLongPress(newFunction, parameter);
}

void PDN::attachDuringLongPress(ButtonIdentifier whichButton, callbackFunction newFunction) {
    getButton(whichButton)->setButtonDuringLongPress(newFunction);
}

void PDN::attachDuringLongPress(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction,
    void *parameter) {
    getButton(whichButton)->setButtonDuringLongPress(newFunction, parameter);
}

void PDN::attachLongPressRelease(ButtonIdentifier whichButton, callbackFunction newFunction) {
    getButton(whichButton)->setButtonLongPressRelease(newFunction);
}

void PDN::attachLongPressRelease(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void *parameter) {
    getButton(whichButton)->setButtonLongPressRelease(newFunction, parameter);
}
