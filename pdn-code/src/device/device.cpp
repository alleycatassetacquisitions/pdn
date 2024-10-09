#include "../include/device/device.hpp"

#include "comms_constants.hpp"

Device *Device::GetInstance() {
    static Device instance;
    return &instance;
}


Device::Device() : display(displayCS, displayDC, displayRST),
                   vibrationMotor(motorPin),
                   primary(primaryButtonPin, true, true),
                   secondary(secondaryButtonPin, true, true),
                   displayLights(numDisplayLights),
                   gripLights(numGripLights),
                   head("") {
};

int Device::begin() {
    initializePins();

    Serial1.begin(BAUDRATE, SERIAL_8E2, TXr, TXt, true);

    Serial2.begin(BAUDRATE, SERIAL_8E2, RXr, RXt, true);

    Serial1.setTxBufferSize(TRANSMIT_QUEUE_MAX_SIZE);
    Serial2.setTxBufferSize(TRANSMIT_QUEUE_MAX_SIZE);

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

String Device::getDeviceId() {
    return deviceID;
}

OneButton Device::getPrimaryButton() {
    return primary;
}

OneButton Device::getSecondaryButton() {
    return secondary;
}

Haptics Device::getVibrator() {
    return vibrationMotor;
}

Display Device::getDisplay() {
    return display;
}

DisplayLights Device::getDisplayLights() {
    return displayLights;
}

GripLights Device::getGripLights() {
    return gripLights;
}

void Device::writeString(String *msg) {
    switch (currentCommsJack) {
        case OUTPUT_JACK: {
            outputJack().print(STRING_START);
            outputJack().println(*msg);
            break;
        }
        case INPUT_JACK: {
            inputJack().print(STRING_START);
            inputJack().println(*msg);
            break;
        }
    }
}

void Device::writeString(const String *msg) {
    writeString(new String(*msg));
}

String *Device::peekComms() {
    if (head == "") {
        head = readString();
    }
    return &head;
}

String Device::readString() {
    String return_me = head;

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
                    return_me = outputJack().readStringUntil(STRING_TERM);
                } else {
                    return_me = "No valid message during output jack read";
                }
            }
            case INPUT_JACK: {
                while (inputJack().available() && inputJack().peek() != STRING_START) {
                    inputJack().read();
                }
                if (inputJack().peek() == STRING_START) {
                    inputJack().read();
                    return_me = inputJack().readStringUntil(STRING_TERM);
                } else {
                    return_me = "No valid message during input jack read";
                }
            }
            default: return_me = "";
        }
    }
    return return_me;
}

bool Device::commsAvailable() {
    switch (currentCommsJack) {
        case OUTPUT_JACK: {
            return outputJack().available() > 0;
        }
        case INPUT_JACK: {
            return inputJack().available() > 0;
        }
    }
}

int Device::getSerialWriteQueueSize() {
    switch (currentCommsJack) {
        case OUTPUT_JACK:
            return TRANSMIT_QUEUE_MAX_SIZE - outputJack().availableForWrite();
        case INPUT_JACK:
            return TRANSMIT_QUEUE_MAX_SIZE - inputJack().availableForWrite();
    }
}

void Device::setActiveComms(int whichJack) {
    currentCommsJack = whichJack;
}

HardwareSerial Device::outputJack() {
    return Serial1;
}

HardwareSerial Device::inputJack() {
    return Serial2;
}

void Device::tick() {
    primary.tick();
    secondary.tick();
}

void Device::setGlobablLightColor(CRGB color) {
    FastLED.showColor(color, 255);
};

void Device::setGlobalBrightness(int brightness) {
    FastLED.setBrightness(brightness);
};
