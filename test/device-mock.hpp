//
// Created by Elli Furedy on 10/6/2024.
//
#pragma once

#include <gmock/gmock.h>
#include "device.hpp"
#include <queue>
#include <vector>

using namespace std;

class FakeHWSerialWrapper : public HWSerialWrapper {
    public:
    FakeHWSerialWrapper() : HWSerialWrapper() {}

    void begin() override {

    }

    int availableForWrite() override {
        return 1024 - msgQueue.size();
    }

    int available() override {
        return msgQueue.size() > 0;
    }

    int peek() override {
        return msgQueue.front();
    }

    int read() override {
        char val = msgQueue.front();
        msgQueue.pop_front();
        return val;
    }

    string readStringUntil(char terminator) override {
        vector<char> buffer;
        while (msgQueue.front() != terminator) {
            buffer.push_back(msgQueue.front());
            msgQueue.pop_front();
        }
        msgQueue.pop_front();
        return string(&buffer.front(), buffer.size());
    }

    void print(char msg) override {
        msgQueue.emplace_back(msg);
    }

    void println(char* msg) override {
        while(msg[0] != '\0') {
            print(*msg);
        }
        print(STRING_TERM);
    }

    void println(string msg) override {
        const char* str = msg.c_str();
        for(int i = 0; i < msg.length(); i++) {
            print(str[i]);
        }
        print(STRING_TERM);
    }

    deque<char> msgQueue;

};

class FakeDevice : public Device {
protected:
    HWSerialWrapper* outputJackSerial;
    HWSerialWrapper* inputJackSerial;

    HWSerialWrapper* outputJack() override {
        return outputJackSerial;
    }

    HWSerialWrapper* inputJack() override {
        return inputJackSerial;
    }
};

class MockDevice : public Device {
    public:
    MOCK_METHOD(int, begin, (), (override));
    MOCK_METHOD(void, tick, (), (override));
    MOCK_METHOD(void, setDeviceId, (string), (override));
    MOCK_METHOD(string, getDeviceId, (), (override));
    MOCK_METHOD(void, setButtonClick, (int, parameterizedCallbackFunction, void*), (override));
    MOCK_METHOD(void, removeButtonCallbacks, (), (override));
    MOCK_METHOD(void, setGlobablLightColor, (PDNColor), (override));
    MOCK_METHOD(void, setGlobalBrightness, (int), (override));
    MOCK_METHOD(void, addToLight, (int, int, PDNColor), (override));
    MOCK_METHOD(void, fadeLightsBy, (int, int), (override));
    MOCK_METHOD(void, setVibration, (int), (override));
    MOCK_METHOD(int, getCurrentVibrationIntensity, (), (override));
    MOCK_METHOD(void, initializePins, (), (override));

    HWSerialWrapper* outputJack() override {
        return &outputJackSerial;
    }

    HWSerialWrapper* inputJack() override {
        return &inputJackSerial;
    }

    FakeHWSerialWrapper outputJackSerial;
    FakeHWSerialWrapper inputJackSerial;

};
