//
// Created by Elli Furedy on 10/8/2024.
//

#pragma once
#include <string>

#include "device-constants.hpp"
#include "serial-wrapper.hpp"

using namespace std;

enum {
    OUTPUT_JACK = 0,
    INPUT_JACK = 1
};

class DeviceSerial {
    public:
    virtual ~DeviceSerial() {}

    void writeString(string *msg) {
        switch (currentCommsJack) {
            case OUTPUT_JACK: {
                outputJack()->print(STRING_START);
                outputJack()->println(*msg);
                break;
            }
            case INPUT_JACK: {
                inputJack()->print(STRING_START);
                inputJack()->println(*msg);
                break;
            }
        }
    }

    void writeString(const string* msg) {
        writeString(new string(*msg));
    }

    string readString(){
        string return_me = head;

        // need to invalidate head
        head = "";

        if (return_me.empty()) {
            switch (currentCommsJack) {
                case OUTPUT_JACK: {
                    while (outputJack()->available() && outputJack()->peek() != STRING_START) {
                        outputJack()->read();
                    }
                    if (outputJack()->peek() == STRING_START) {
                        outputJack()->read();
                        return_me = string(outputJack()->readStringUntil(STRING_TERM).c_str());
                    } else {
                        return_me = "No valid message during output jack read";
                    }
                    break;
                }
                case INPUT_JACK: {
                    while (inputJack()->available() && inputJack()->peek() != STRING_START) {
                        inputJack()->read();
                    }
                    if (inputJack()->peek() == STRING_START) {
                        inputJack()->read();
                        return_me = string(inputJack()->readStringUntil(STRING_TERM).c_str());
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

    void setActiveComms(int whichJack) {
        currentCommsJack = whichJack;
    }

    string *peekComms() {
        if (head == "") {
            head = readString();
        }
        return &head;
    }

    bool commsAvailable() {
        switch (currentCommsJack) {
            case OUTPUT_JACK: {
                return outputJack()->available() > 0;
            }
            case INPUT_JACK: {
                return inputJack()->available() > 0;
            }
            default:
                return false;
        }
    }

    int getSerialWriteQueueSize() {
        switch (currentCommsJack) {
            case OUTPUT_JACK:
                return TRANSMIT_QUEUE_MAX_SIZE - outputJack()->availableForWrite();
            case INPUT_JACK:
                return TRANSMIT_QUEUE_MAX_SIZE - inputJack()->availableForWrite();
        }
        return -1;
    }

protected:

    virtual HWSerialWrapper* outputJack() = 0;

    virtual HWSerialWrapper* inputJack() = 0;

private:
    string head = "";

    int currentCommsJack = 1;
};
