#include <gtest/gtest.h>

#include "device-mock.hpp"
#include "state/state.hpp"
//
// Created by Elli Furedy on 10/8/2024.
//
const std::string* TEST_OUTGOING_STRING_1 = new std::string("message");
const std::string* TEST_OUTGOING_STRING_2 = new std::string("another message");
const std::string* TEST_OUTGOING_STRING_3 = new std::string("this is a test");

const std::string* TEST_INCOMING_STRING = new std::string("reading");
const std::string* TEST_INCOMING_STRING_1 = new std::string("read more");
const std::string* GARBAGE_STRING = new std::string("ahefaosen..as");
const std::string* INVALID_INCOMING_STRING = new std::string("this isn't a registered message");

class TestReadingValidMessageState : State {
    public:
    TestReadingValidMessageState() : State(0) {
    }

    void onStateMounted(Device *PDN) override {
        std::vector<const std::string *> reading;

        reading.push_back(TEST_INCOMING_STRING);

        registerValidMessages(reading);
    }

    void onStateLoop(Device *PDN) override {
        std::string* validMessage = waitForValidMessage(PDN);
        if(validMessage != nullptr) {
            didReadValidMessage = true;
        }
    }

    void onStateDismounted(Device *PDN) override {

    }

    bool didReadValidMessage = false;

};

class StateTestSuite : public testing::Test {

protected:
    StateTestSuite() {}

    void SetUp() override {
        readValidMessageState = TestReadingValidMessageState();
        stateTestDevice.setActiveComms(SerialIdentifier::INPUT_JACK);
    }

    void prepareValidMessageTest() {
        readValidMessageState.onStateMounted(&stateTestDevice);

        stateTestDevice.writeString(*TEST_INCOMING_STRING);
    }

    void prepareInvalidMessageTest() {
        readValidMessageState.onStateMounted(&stateTestDevice);

        stateTestDevice.writeString(*INVALID_INCOMING_STRING);
    }

    void prepareGarbageFirstTest() {
        readValidMessageState.onStateMounted(&stateTestDevice);

        stateTestDevice.writeString(*INVALID_INCOMING_STRING);
        stateTestDevice.writeString(*TEST_INCOMING_STRING);
    }

    MockDevice stateTestDevice;
    TestReadingValidMessageState readValidMessageState;

};