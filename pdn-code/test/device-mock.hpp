//
// Created by Elli Furedy on 10/6/2024.
//

#include <gmock/gmock.h>

#include "device.hpp"

class MockDevice : public Device {
    public:
    MOCK_METHOD(int, begin, (), (override));
    MOCK_METHOD(void, tick, (), (override));
    MOCK_METHOD(void, setDeviceId, (string), (override));
    MOCK_METHOD(string, getDeviceId, (), (override));
    MOCK_METHOD(void, setButtonClick, (int, parameterizedCallbackFunction, void*), (override));
    MOCK_METHOD(void, removeButtonCallbacks, (), (override));
    MOCK_METHOD(void, writeString, (string*), (override));
    MOCK_METHOD(void, writeString, (const string*), (override));
    MOCK_METHOD(string, readString, (), (override));
    MOCK_METHOD(void, setActiveComms, (int), (override));
    MOCK_METHOD(string*, peekComms, (), (override));
    MOCK_METHOD(bool, commsAvailable, (), (override));
    MOCK_METHOD(int, getSerialWriteQueueSize, (), (override));
    MOCK_METHOD(void, setGlobablLightColor, (PDNColor), (override));
    MOCK_METHOD(void, setGlobalBrightness, (int), (override));
    MOCK_METHOD(void, addToLight, (int, int, PDNColor), (override));
    MOCK_METHOD(void, fadeLightsBy, (int, int), (override));
    MOCK_METHOD(void, setVibration, (int), (override));
    MOCK_METHOD(int, getCurrentVibrationIntensity, (), (override));
    MOCK_METHOD(void, initializePins, (), (override));
};
