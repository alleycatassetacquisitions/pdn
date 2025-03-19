#include <gtest/gtest.h>

#include "device-mock.hpp"
#include "state.hpp"
//
// Created by Elli Furedy on 10/8/2024.
//
const string* TEST_OUTGOING_STRING_1 = new string("message");
const string* TEST_OUTGOING_STRING_2 = new string("another message");
const string* TEST_OUTGOING_STRING_3 = new string("this is a test");

const string* TEST_INCOMING_STRING = new string("reading");
const string* TEST_INCOMING_STRING_1 = new string("read more");
const string* GARBAGE_STRING = new string("ahefaosen..as");
const string* INVALID_INCOMING_STRING = new string("this isn't a registered message");

class TestReadingValidMessageState : State {
    public:
    TestReadingValidMessageState() : State(0) {
    }

    void onStateMounted(Device *PDN) override {
        std::vector<const string *> reading;

        reading.push_back(TEST_INCOMING_STRING);

        registerValidMessages(reading);
    }

    void onStateLoop(Device *PDN) override {
        string* validMessage = waitForValidMessage(PDN);
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