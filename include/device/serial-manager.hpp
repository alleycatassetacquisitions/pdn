//
// Created by Elli Furedy on 10/8/2024.
//

#pragma once

#include "drivers/driver-interface.hpp"
#include <string>
#include <functional>
#include "device-constants.hpp"

class SerialManager {
public:
    SerialManager(HWSerialWrapper* inputJack, HWSerialWrapper* inputSecondaryJack)
        : inputJack(inputJack), inputSecondaryJack(inputSecondaryJack) {}

    ~SerialManager() = default;

    HWSerialWrapper* getInputJack() { return inputJack; }
    void setInputJack(HWSerialWrapper* jack) { inputJack = jack; }

    HWSerialWrapper* getInputSecondaryJack() { return inputSecondaryJack; }
    void setInputSecondaryJack(HWSerialWrapper* jack) { inputSecondaryJack = jack; }

    void writeString(const std::string& msg, SerialIdentifier jack = SerialIdentifier::INPUT_JACK) {
        HWSerialWrapper* hw = getJack(jack);
        hw->print(STRING_START);
        hw->println(msg);
    }

    std::string readString(SerialIdentifier jack = SerialIdentifier::INPUT_JACK) {
        std::string& head = getHead(jack);
        std::string return_me = head;
        head = "";

        HWSerialWrapper* hw = getJack(jack);

        if (return_me.empty()) {
            while (hw->available() && hw->peek() != STRING_START) {
                hw->read();
            }
            if (hw->peek() == STRING_START) {
                hw->read();
                return_me = std::string(hw->readStringUntil(STRING_TERM).c_str());
            } else {
                return_me = "null";
            }
        }
        return return_me;
    }

    std::string* peekComms(SerialIdentifier jack = SerialIdentifier::INPUT_JACK) {
        std::string& head = getHead(jack);
        if (head.empty()) {
            head = readString(jack);
        }
        return &head;
    }

    bool commsAvailable(SerialIdentifier jack = SerialIdentifier::INPUT_JACK) {
        return getJack(jack)->available() > 0;
    }

    int getSerialWriteQueueSize(SerialIdentifier jack = SerialIdentifier::INPUT_JACK) {
        return TRANSMIT_QUEUE_MAX_SIZE - getJack(jack)->availableForWrite();
    }

    void setOnStringReceivedCallback(const std::function<void(std::string)>& callback, SerialIdentifier jack = SerialIdentifier::INPUT_JACK) {
        getJack(jack)->setStringCallback(callback);
    }

    void clearCallback(SerialIdentifier jack = SerialIdentifier::INPUT_JACK) {
        getJack(jack)->setStringCallback(nullptr);
    }

    void clearCallbacks() {
        inputJack->setStringCallback(nullptr);
        inputSecondaryJack->setStringCallback(nullptr);
    }

    void flushSerial() {
        inputJack->flush();
        inputSecondaryJack->flush();
    }

    std::string getInputHead() const { return inputHead; }

private:
    HWSerialWrapper* getJack(SerialIdentifier jack) {
        if (jack == SerialIdentifier::INPUT_JACK) return inputJack;
        if (jack == SerialIdentifier::INPUT_JACK_SECONDARY) return inputSecondaryJack;
        return nullptr;
    }

    std::string& getHead(SerialIdentifier jack) {
        if (jack == SerialIdentifier::INPUT_JACK_SECONDARY) return inputSecondaryHead;
        return inputHead;
    }

    HWSerialWrapper* inputJack;
    HWSerialWrapper* inputSecondaryJack;

    std::string inputHead = "";
    std::string inputSecondaryHead = "";
};
