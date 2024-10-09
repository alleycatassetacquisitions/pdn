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

    Serial1.begin(BAUDRATE, SERIAL_8E2, TXr, TXt, true);

    Serial2.begin(BAUDRATE, SERIAL_8E2, RXr, RXt, true);

    Serial1.setTxBufferSize(TRANSMIT_QUEUE_MAX_SIZE);
    Serial2.setTxBufferSize(TRANSMIT_QUEUE_MAX_SIZE);

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

void PDN::writeString(string *msg) {
    switch (currentCommsJack) {
        case OUTPUT_JACK: {
            outputJack().print(STRING_START);
            outputJack().println(*msg->c_str());
            break;
        }
        case INPUT_JACK: {
            inputJack().print(STRING_START);
            inputJack().println(*msg->c_str());
            break;
        }
    }
}

void PDN::writeString(const string *msg) {
    writeString(new string(*msg));
}

string *PDN::peekComms() {
    if (head == "") {
        head = readString();
    }
    return &head;
}

string PDN::readString() {
    string return_me = head;

    // need to invalidate head
    head = "";

    if (return_me == "") {
        switch (currentCommsJack) {
            case OUTPUT_JACK: {
                while (outputJack().available() && outputJack().peek() != STRING_START) {
                    outputJack().read();
                }
                if (outputJack().peek() == STRING_START) {
                    outputJack().read();
                    return_me = string(outputJack().readStringUntil(STRING_TERM).c_str());
                } else {
                    return_me = "No valid message during output jack read";
                }
                break;
            }
            case INPUT_JACK: {
                while (inputJack().available() && inputJack().peek() != STRING_START) {
                    inputJack().read();
                }
                if (inputJack().peek() == STRING_START) {
                    inputJack().read();
                    return_me = string(inputJack().readStringUntil(STRING_TERM).c_str());
                } else {
                    return_me = "No valid message during input jack read";
                }
                break;
            }
            default: return_me = "";
        }
    }
    return return_me;
}

bool PDN::commsAvailable() {
    switch (currentCommsJack) {
        case OUTPUT_JACK: {
            return outputJack().available() > 0;
        }
        case INPUT_JACK: {
            return inputJack().available() > 0;
        }
    }
}

int PDN::getSerialWriteQueueSize() {
    switch (currentCommsJack) {
        case OUTPUT_JACK:
            return TRANSMIT_QUEUE_MAX_SIZE - outputJack().availableForWrite();
        case INPUT_JACK:
            return TRANSMIT_QUEUE_MAX_SIZE - inputJack().availableForWrite();
    }
    return -1;
}

void PDN::setActiveComms(int whichJack) {
    currentCommsJack = whichJack;
}

HardwareSerial PDN::outputJack() {
    return Serial1;
}

HardwareSerial PDN::inputJack() {
    return Serial2;
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

