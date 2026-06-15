//
// Created by Elli Furedy on 10/8/2024.
//

#pragma once

#include "drivers/driver-interface.hpp"
#include <string>
#include <functional>
#include "protocol-constants.hpp"

class SerialManager {
public:
    SerialManager(HWSerialWrapper* outputJack, HWSerialWrapper* inputJack)
        : outputJack(outputJack), inputJack(inputJack), inputJackSecondary(nullptr) {}

    ~SerialManager() = default;

    HWSerialWrapper* getOutputJack() { return outputJack; }
    void setOutputJack(HWSerialWrapper* jack) { outputJack = jack; }

    HWSerialWrapper* getInputJack() { return inputJack; }
    void setInputJack(HWSerialWrapper* jack) { inputJack = jack; }

    HWSerialWrapper* getSecondaryInputJack() { return inputJackSecondary; }
    void setSecondaryInputJack(HWSerialWrapper* jack) { inputJackSecondary = jack; }

    void writeString(const std::string& msg, SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        HWSerialWrapper* hw = getJack(jack);
        if (!hw) return;
        hw->print(STRING_START);
        hw->println(msg);
    }

    std::string readString(SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        std::string& head = getHead(jack);
        std::string return_me = head;
        head = "";

        HWSerialWrapper* hw = getJack(jack);
        if (!hw) return "null";

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

    std::string* peekComms(SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        std::string& head = getHead(jack);
        if (head.empty()) {
            head = readString(jack);
        }
        return &head;
    }

    bool commsAvailable(SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        HWSerialWrapper* hw = getJack(jack);
        return hw ? hw->available() > 0 : false;
    }

    int getSerialWriteQueueSize(SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        HWSerialWrapper* hw = getJack(jack);
        return hw ? TRANSMIT_QUEUE_MAX_SIZE - hw->availableForWrite() : 0;
    }

    void setOnStringReceivedCallback(const std::function<void(std::string)>& callback, SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        HWSerialWrapper* hw = getJack(jack);
        if (hw) hw->setStringCallback(callback);
    }

    void clearCallback(SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        HWSerialWrapper* hw = getJack(jack);
        if (hw) hw->setStringCallback(nullptr);
    }

    void clearCallbacks() {
        if (outputJack) outputJack->setStringCallback(nullptr);
        if (inputJack) inputJack->setStringCallback(nullptr);
        if (inputJackSecondary) inputJackSecondary->setStringCallback(nullptr);
    }

    void flushSerial() {
        if (outputJack) outputJack->flush();
        if (inputJack) inputJack->flush();
        if (inputJackSecondary) inputJackSecondary->flush();
    }

    std::string getOutputHead() const { return outputHead; }

private:
    HWSerialWrapper* getJack(SerialIdentifier jack) {
        switch (jack) {
            case SerialIdentifier::OUTPUT_JACK:          return outputJack;
            case SerialIdentifier::INPUT_JACK:           return inputJack;
            case SerialIdentifier::INPUT_JACK_SECONDARY: return inputJackSecondary;
            default:                                     return nullptr;
        }
    }

    std::string& getHead(SerialIdentifier jack) {
        switch (jack) {
            case SerialIdentifier::OUTPUT_JACK:          return outputHead;
            case SerialIdentifier::INPUT_JACK_SECONDARY: return secondaryInputHead;
            default:                                     return inputHead;
        }
    }

    HWSerialWrapper* outputJack;
    HWSerialWrapper* inputJack;
    HWSerialWrapper* inputJackSecondary;

    std::string outputHead = "";
    std::string inputHead = "";
    std::string secondaryInputHead = "";
};
