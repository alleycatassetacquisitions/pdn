//
// Created by Elli Furedy on 10/8/2024.
//

#pragma once

#include "drivers/driver-interface.hpp"
#include <string>
#include <functional>
#include <cassert>
#include "device-constants.hpp"

enum class SerialByState {
    PRIMARY = 0,
    AUXILIARY = 1,
};

class SerialManager {
public:
    SerialManager(HWSerialWrapper* outputJack, HWSerialWrapper* inputJack)
        : outputJack_(outputJack), inputJack_(inputJack) {}

    ~SerialManager() = default;

    HWSerialWrapper* getOutputJack() { return outputJack_; }
    void setOutputJack(HWSerialWrapper* jack) { outputJack_ = jack; }

    HWSerialWrapper* getInputJack() { return inputJack_; }
    void setInputJack(HWSerialWrapper* jack) { inputJack_ = jack; }

    void writeString(const std::string& msg, SerialByState whichJack = SerialByState::PRIMARY) {
        if (whichJack == SerialByState::PRIMARY) {
            getPrimaryCommsJack()->print(STRING_START);
            getPrimaryCommsJack()->println(msg);
        } else {
            getAuxiliaryCommsJack()->print(STRING_START);
            getAuxiliaryCommsJack()->println(msg);
        }
    }

    std::string readString(SerialByState whichJack = SerialByState::PRIMARY){
        std::string return_me;

        if (whichJack == SerialByState::PRIMARY) {
            return_me = primaryHead;
            primaryHead = "";
        } else {
            return_me = auxiliaryHead;
            auxiliaryHead = "";
        }

        HWSerialWrapper* requestedJack = getJackByState(whichJack);

        if (return_me.empty()) {
            while (requestedJack->available() && requestedJack->peek() != STRING_START) {
                requestedJack->read();
            }
            if (requestedJack->peek() == STRING_START) {
                requestedJack->read();
                return_me = std::string(requestedJack->readStringUntil(STRING_TERM).c_str());
            } else {
                return_me = "null";
            }
        }
        return return_me;
    }

    void setActiveComms(SerialIdentifier whichJack) {
        currentCommsJack = whichJack;
    }

    std::string *peekComms(SerialByState whichJack = SerialByState::PRIMARY) {
        if (whichJack == SerialByState::PRIMARY && primaryHead == "") {
            primaryHead = readString(whichJack);
        } else if (whichJack == SerialByState::AUXILIARY && auxiliaryHead == "") {
            auxiliaryHead = readString(whichJack);
        }
        if (whichJack == SerialByState::PRIMARY) {
            return &primaryHead;
        } else {
            return &auxiliaryHead;
        }
    }

    bool commsAvailable(SerialByState whichJack = SerialByState::PRIMARY) {
        return getJackByState(whichJack)->available() > 0;
    }

    int getSerialWriteQueueSize(SerialByState whichJack = SerialByState::PRIMARY) {
        return TRANSMIT_QUEUE_MAX_SIZE - getJackByState(whichJack)->availableForWrite();
    }

    void setOnStringReceivedCallback(const std::function<void(std::string)>& callback, SerialByState whichJack = SerialByState::PRIMARY) {
        if (whichJack == SerialByState::PRIMARY) {
            onPrimaryJackStringReceivedCallback = callback;
            getPrimaryCommsJack()->setStringCallback(callback);
        } else {
            onAuxiliaryJackStringReceivedCallback = callback;
            getAuxiliaryCommsJack()->setStringCallback(callback);
        }
    }


    void clearCallbacks() {
        onPrimaryJackStringReceivedCallback = nullptr;
        onAuxiliaryJackStringReceivedCallback = nullptr;
        getPrimaryCommsJack()->setStringCallback(nullptr);
        getAuxiliaryCommsJack()->setStringCallback(nullptr);
    }

    void flushSerial() {
        outputJack_->flush();
        inputJack_->flush();
    }

    std::string getPrimaryHead() const { return primaryHead; }

private:
    HWSerialWrapper* getPrimaryCommsJack() {
        switch(currentCommsJack) {
            case SerialIdentifier::OUTPUT_JACK:
                return outputJack_;
            case SerialIdentifier::INPUT_JACK:
                return inputJack_;
            default:
                assert(false);
        }
    }

    HWSerialWrapper* getAuxiliaryCommsJack() {
        switch(currentCommsJack) {
            case SerialIdentifier::OUTPUT_JACK:
                return inputJack_;
            case SerialIdentifier::INPUT_JACK:
                return outputJack_;
            default:
                assert(false);
        }
    }

    HWSerialWrapper* getJackByState(SerialByState whichState) {
        if (whichState == SerialByState::PRIMARY) {
            return getPrimaryCommsJack();
        } else {
            return getAuxiliaryCommsJack();
        }
    }

    HWSerialWrapper* outputJack_;
    HWSerialWrapper* inputJack_;

    std::string primaryHead = "";
    std::string auxiliaryHead = "";

    SerialIdentifier currentCommsJack = SerialIdentifier::OUTPUT_JACK;
    std::function<void(std::string)> onPrimaryJackStringReceivedCallback;
    std::function<void(std::string)> onAuxiliaryJackStringReceivedCallback;
};
