//
// Created by Elli Furedy on 10/4/2024.
//
#pragma once
#include <HardwareSerial.h>

#include "device-interface/device-serial.hpp"


enum {
    OUTPUT_JACK = 1,
    INPUT_JACK = 2
};

class PDNSerial : public IDeviceSerial {
public:
    PDNSerial();
    void writeString(String *msg) override;
    void writeString(const String *msg) override;
    String readString() override;
    String *peekComms() override;
    bool commsAvailable() override;
    int getTrxBufferedMessagesSize() override;

    void setActiveComms(int whichJack);

protected:

    int currentCommsJack = 1;

    HardwareSerial inputJack();
    HardwareSerial outputJack();
    HardwareSerial getActiveCommsJack();
};
