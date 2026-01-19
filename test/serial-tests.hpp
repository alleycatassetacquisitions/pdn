//
// Created by Elli Furedy on 10/11/2024.
//

#include <gtest/gtest.h>

#include "device-mock.hpp"

#include <string>

using namespace std;

std::string TEST_WRITE_STRING = "this is a test write string.";
std::string TEST_READ_STRING = "this is a test read string.";

class SerialTestSuite : public testing::Test {

protected:
    SerialTestSuite() {

    }

    void SetUp() override {

    }

    void serialWriteAppendsStringStart() {
        stateTestDevice.setActiveComms(SerialIdentifier::OUTPUT_JACK);

        stateTestDevice.writeString(TEST_WRITE_STRING.c_str());

        ASSERT_EQ(stateTestDevice.outputJackSerial.peek(), STRING_START);
    }

    void headIsSetWhenPeekIsExecutedAndStringIsRemovedFromQueue() {
        stateTestDevice.setActiveComms(SerialIdentifier::OUTPUT_JACK);

        stateTestDevice.writeString(TEST_WRITE_STRING.c_str());

        ASSERT_EQ(stateTestDevice.getHead(), "");

        stateTestDevice.peekComms();

        ASSERT_EQ(stateTestDevice.outputJackSerial.available(), 0);

        ASSERT_EQ(stateTestDevice.getHead(), TEST_WRITE_STRING);
    }

    void whenHeadIsEmptyReadStringStillReturnsNextString() {
        stateTestDevice.setActiveComms(SerialIdentifier::OUTPUT_JACK);
        stateTestDevice.writeString(TEST_WRITE_STRING.c_str());

        ASSERT_EQ(stateTestDevice.getHead(), "");

        std::string readResult = stateTestDevice.readString();

        ASSERT_EQ(readResult, TEST_WRITE_STRING);
    }

    MockDevice stateTestDevice;

};
