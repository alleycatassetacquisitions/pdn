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
    SerialManager(HWSerialWrapper* outputJack, HWSerialWrapper* inputJack)
        : outputJack(outputJack), inputJack(inputJack) {}

    ~SerialManager() = default;

    HWSerialWrapper* getOutputJack() { return outputJack; }
    void setOutputJack(HWSerialWrapper* jack) { outputJack = jack; }

    HWSerialWrapper* getInputJack() { return inputJack; }
    void setInputJack(HWSerialWrapper* jack) { inputJack = jack; }

    void writeString(const std::string& msg, SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        HWSerialWrapper* hw = getJack(jack);
        hw->print(STRING_START);
        hw->println(msg);
    }

    std::string readString(SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
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

    std::string* peekComms(SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        std::string& head = getHead(jack);
        if (head.empty()) {
            head = readString(jack);
        }
        return &head;
    }

    bool commsAvailable(SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        return getJack(jack)->available() > 0;
    }

    int getSerialWriteQueueSize(SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        return TRANSMIT_QUEUE_MAX_SIZE - getJack(jack)->availableForWrite();
    }

    void setOnStringReceivedCallback(const std::function<void(std::string)>& callback, SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        getJack(jack)->setStringCallback(callback);
    }

    void clearCallback(SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        getJack(jack)->setStringCallback(nullptr);
    }

    void clearCallbacks() {
        outputJack->setStringCallback(nullptr);
        inputJack->setStringCallback(nullptr);
    }

    void flushSerial() {
        outputJack->flush();
        inputJack->flush();
    }

    std::string getOutputHead() const { return outputHead; }

private:
    HWSerialWrapper* getJack(SerialIdentifier jack) {
        return jack == SerialIdentifier::OUTPUT_JACK ? outputJack : inputJack;
    }

    std::string& getHead(SerialIdentifier jack) {
        return jack == SerialIdentifier::OUTPUT_JACK ? outputHead : inputHead;
    }

    HWSerialWrapper* outputJack;
    HWSerialWrapper* inputJack;

    std::string outputHead = "";
    std::string inputHead = "";
};
