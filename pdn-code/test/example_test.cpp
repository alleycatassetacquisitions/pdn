//
// Created by Elli Furedy on 8/28/2024.
//
//
// Created by Elli Furedy on 8/27/2024.
//
#include <gtest/gtest.h>
#include <gmock/gmock.h>\


#include "device-mock.hpp"
#include "test-objects.hpp"
// TEST(...)
// TEST_F(...)

#if defined(ARDUINO)
#include <Arduino.h>

void setup()
{
    // should be the same value as for the `test_speed` option in "platformio.ini"
    // default value is test_speed=115200
    Serial.begin(115200);

    ::testing::InitGoogleTest();
    // if you plan to use GMock, replace the line above with
    // ::testing::InitGoogleMock();
}

void loop()
{
    // Run tests
    if (RUN_ALL_TESTS())
        ;

    // sleep for 1 sec
    delay(1000);
}

#else

TEST(StateMachineTestSuite, testStateMachine) {
    MockDevice* device;
    TestStateMachine* stateMachine = new TestStateMachine(device);

    stateMachine->initialize();

    State* currentState = stateMachine->getCurrentState();

    ASSERT_TRUE(static_cast<InitialTestState*>(currentState)->stateMountedInvoked);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // if you plan to use GMock, replace the line above with
    // ::testing::InitGoogleMock(&argc, argv);

    if (RUN_ALL_TESTS())
        ;

    // Always return zero-code and allow PlatformIO to parse results
    return 0;
}
#endif