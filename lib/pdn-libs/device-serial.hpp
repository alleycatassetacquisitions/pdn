//
// Created by Elli Furedy on 10/8/2024.
//

#pragma once

#include "serial-wrapper.hpp"
#include <string>
#include <functional>
#include "device-constants.hpp"

enum class SerialIdentifier {
    OUTPUT_JACK = 0,
    INPUT_JACK = 1
};

class DeviceSerial {
    public:
    virtual ~DeviceSerial() {}

    void writeString(string msg) {
        getCurrentCommsJack()->print(STRING_START);
        getCurrentCommsJack()->println(msg);
    }

    string readString(){
        string return_me = head;

        // need to invalidate head
        head = "";

        if (return_me.empty()) {
            while (getCurrentCommsJack()->available() && getCurrentCommsJack()->peek() != STRING_START) {
                getCurrentCommsJack()->read();
            }
            if (getCurrentCommsJack()->peek() == STRING_START) {
                getCurrentCommsJack()->read();
                return_me = string(getCurrentCommsJack()->readStringUntil(STRING_TERM).c_str());
            } else {
                return_me = "null";
            }
        }
        return return_me;
    }

    void setActiveComms(SerialIdentifier whichJack) {
        currentCommsJack = whichJack;
    }

    string *peekComms() {
        if (head == "") {
            head = readString();
        }
        return &head;
    }

    bool commsAvailable() {
        return getCurrentCommsJack()->available() > 0;
    }

    int getSerialWriteQueueSize() {
        return TRANSMIT_QUEUE_MAX_SIZE - getCurrentCommsJack()->availableForWrite();
    }

    void setOnStringReceivedCallback(std::function<void(std::string)> callback) {
        onStringReceivedCallback = callback;
    }


    void clearCallbacks() {
        onStringReceivedCallback = nullptr;
    }

    void serialLoop() {
        if (commsAvailable()) {
            char incomingChar = getCurrentCommsJack()->read();
            if (incomingChar == STRING_START) {
                char receivedString[256] = {0}; // Buffer for received chars
                int idx = 0;
                while (getCurrentCommsJack()->available() && getCurrentCommsJack()->peek() != STRING_TERM && idx < 255) {
                    receivedString[idx++] = getCurrentCommsJack()->read();
                }
                receivedString[idx] = '\0'; // Null terminate
                if (onStringReceivedCallback) {
                    onStringReceivedCallback(string(receivedString));
                }
            }
        }
    }


protected:

    HWSerialWrapper* getCurrentCommsJack() {
        switch(currentCommsJack) {
            case SerialIdentifier::OUTPUT_JACK:
                return outputJack();
            case SerialIdentifier::INPUT_JACK:
                return inputJack();
            default:
                assert(false);
        }
    }

    virtual HWSerialWrapper* outputJack() = 0;

    virtual HWSerialWrapper* inputJack() = 0;

    string head = "";

private:

    SerialIdentifier currentCommsJack = SerialIdentifier::OUTPUT_JACK;
    std::function<void(std::string)> onStringReceivedCallback;
};
