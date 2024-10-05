#include "device/pdn-serial.hpp"

#include "comms_constants.hpp"
#include "device/pdn.hpp"
//
// Created by Elli Furedy on 10/5/2024.
//
PDN::PDN() = default;

void PDNSerial::writeString(String *msg) override {
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

void PDNSerial::writeString(const String *msg) override {
    writeString(new String(*msg));
}

String *PDNSerial::peekComms() override {
    if (head == "") {
        head = readString();
    }
    return &head;
}

String PDNSerial::readString() override {
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
                break;
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
                break;
            }
            default: return_me = "";
        }
    }
    return return_me;
}

bool PDNSerial::commsAvailable() override {
    switch (currentCommsJack) {
        case OUTPUT_JACK: {
            return outputJack().available() > 0;
        }
        case INPUT_JACK: {
            return inputJack().available() > 0;
        }
    }
}

int PDNSerial::getTrxBufferedMessagesSize() override {
    switch (currentCommsJack) {
        case OUTPUT_JACK:
            return TRANSMIT_QUEUE_MAX_SIZE - outputJack().availableForWrite();
        case INPUT_JACK:
            return TRANSMIT_QUEUE_MAX_SIZE - inputJack().availableForWrite();
    }
}

void PDNSerial::setActiveComms(int whichJack) {
    currentCommsJack = whichJack;
}

HardwareSerial PDNSerial::outputJack() {
    return Serial1;
}

HardwareSerial PDNSerial::inputJack() {
    return Serial2;
}