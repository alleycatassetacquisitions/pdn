#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/chain-context.hpp"
#include "game/quickdraw-states.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::NiceMock;

static std::string drainOutputJackString(FakeHWSerialWrapper& serial) {
    auto& q = serial.msgQueue;
    if (q.empty()) return "";
    if (q.front() == STRING_START) q.pop_front();
    std::string result;
    while (!q.empty() && q.front() != STRING_TERM) {
        result += q.front();
        q.pop_front();
    }
    if (!q.empty()) q.pop_front();
    return result;
}

class SupporterReadyTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));

        device.rdcOverride = &rdc;
        rdc.initialize(device.wirelessManager, device.serialManager, &device);

        state = new SupporterReadyState(&context, &rdc);
    }

    void TearDown() override {
        delete state;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    RemoteDeviceCoordinator rdc;
    FakePlatformClock* fakeClock;
    ChainContext context;
    SupporterReadyState* state;
};

// 1. Supporter presses button -> sends "confirm:1" out OUTPUT_JACK
inline void supporterPressButtonSendsConfirm1(SupporterReadyTests* t) {
    t->state->onStateMounted(&t->device);

    // Simulate primary button press
    t->device.mockPrimaryButton->setButtonPress(
        [](void* ctx) {
            // trigger the stored callback by calling it directly
        }, nullptr, ButtonInteraction::CLICK
    );

    // The state registered a parameterized callback on the primary button.
    // We need to invoke it. In the mock, setButtonPress stores the callback.
    // We simulate the press by invoking the callback that the state registered.
    // The state calls setButtonPress(callback, this, CLICK) which triggers MockButton::setButtonPress.
    // We need to capture the callback. Instead, use ON_CALL to intercept it.
    // Better: remount after setting up capture.
    delete t->state;
    t->state = new SupporterReadyState(&t->context, &t->rdc);

    parameterizedCallbackFunction capturedPrimary;
    void* capturedPrimaryCtx = nullptr;
    parameterizedCallbackFunction capturedSecondary;
    void* capturedSecondaryCtx = nullptr;

    ON_CALL(*t->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillByDefault([&](parameterizedCallbackFunction fn, void* ctx, ButtonInteraction) {
            capturedPrimary = fn;
            capturedPrimaryCtx = ctx;
        });
    ON_CALL(*t->device.mockSecondaryButton, setButtonPress(_, _, _))
        .WillByDefault([&](parameterizedCallbackFunction fn, void* ctx, ButtonInteraction) {
            capturedSecondary = fn;
            capturedSecondaryCtx = ctx;
        });

    t->state->onStateMounted(&t->device);

    ASSERT_NE(capturedPrimary, nullptr);

    // Simulate button press
    capturedPrimary(capturedPrimaryCtx);

    // Should have sent "confirm:1" out OUTPUT_JACK
    std::string sent = drainOutputJackString(t->device.outputJackSerial);
    EXPECT_EQ(sent, "confirm:1");
}

// 2. Supporter receives "confirm:2" from INPUT_JACK, has pressed -> sends "confirm:3" out OUTPUT_JACK
inline void supporterReceivesConfirmHasPressed(SupporterReadyTests* t) {
    parameterizedCallbackFunction capturedPrimary;
    void* capturedPrimaryCtx = nullptr;

    ON_CALL(*t->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillByDefault([&](parameterizedCallbackFunction fn, void* ctx, ButtonInteraction) {
            capturedPrimary = fn;
            capturedPrimaryCtx = ctx;
        });
    ON_CALL(*t->device.mockSecondaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});

    t->state->onStateMounted(&t->device);

    ASSERT_NE(capturedPrimary, nullptr);

    // Press button first (sets hasPressed_ = true, sends confirm:1 which we'll discard)
    capturedPrimary(capturedPrimaryCtx);
    t->device.outputJackSerial.msgQueue.clear();

    // Now receive "confirm:2" from downstream INPUT_JACK
    // hasPressed_ = true so count should be 2+1 = 3
    t->device.inputJackSerial.stringCallback("confirm:2");

    std::string sent = drainOutputJackString(t->device.outputJackSerial);
    EXPECT_EQ(sent, "confirm:3");
}

// 3. Supporter receives "confirm:2" from INPUT_JACK, hasn't pressed -> sends "confirm:2" out OUTPUT_JACK
inline void supporterReceivesConfirmHasNotPressed(SupporterReadyTests* t) {
    ON_CALL(*t->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});
    ON_CALL(*t->device.mockSecondaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});

    t->state->onStateMounted(&t->device);

    // No button press - receive confirm:2 from downstream
    t->device.inputJackSerial.stringCallback("confirm:2");

    std::string sent = drainOutputJackString(t->device.outputJackSerial);
    EXPECT_EQ(sent, "confirm:2");
}

// 4. Supporter receives "event:countdown" -> sets receivedCountdown, relays to INPUT_JACK
inline void supporterReceivesEventCountdown(SupporterReadyTests* t) {
    ON_CALL(*t->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});
    ON_CALL(*t->device.mockSecondaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});

    t->state->onStateMounted(&t->device);

    t->device.outputJackSerial.stringCallback("event:countdown");

    EXPECT_TRUE(t->state->receivedCountdown());

    // Should relay to INPUT_JACK
    std::string relayed = drainOutputJackString(t->device.inputJackSerial);
    EXPECT_EQ(relayed, "event:countdown");
}

// 5. Supporter receives "event:draw" -> relays to INPUT_JACK
inline void supporterReceivesEventDraw(SupporterReadyTests* t) {
    ON_CALL(*t->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});
    ON_CALL(*t->device.mockSecondaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});

    t->state->onStateMounted(&t->device);

    t->device.outputJackSerial.stringCallback("event:draw");

    std::string relayed = drainOutputJackString(t->device.inputJackSerial);
    EXPECT_EQ(relayed, "event:draw");
}

// 6. Supporter receives "event:win" -> sets transitionToWin
inline void supporterReceivesEventWinSetsFlag(SupporterReadyTests* t) {
    ON_CALL(*t->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});
    ON_CALL(*t->device.mockSecondaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});

    t->state->onStateMounted(&t->device);

    t->device.outputJackSerial.stringCallback("event:win");

    EXPECT_TRUE(t->state->transitionToWin());
}

// 7. Supporter receives "event:loss" -> sets transitionToLose
inline void supporterReceivesEventLossSetsFlag(SupporterReadyTests* t) {
    ON_CALL(*t->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});
    ON_CALL(*t->device.mockSecondaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});

    t->state->onStateMounted(&t->device);

    t->device.outputJackSerial.stringCallback("event:loss");

    EXPECT_TRUE(t->state->transitionToLose());
}

// 8. Supporter receives "event:break" -> relays to INPUT_JACK, sets transitionToIdle
inline void supporterReceivesEventBreak(SupporterReadyTests* t) {
    ON_CALL(*t->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});
    ON_CALL(*t->device.mockSecondaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});

    t->state->onStateMounted(&t->device);

    t->device.outputJackSerial.stringCallback("event:break");

    EXPECT_TRUE(t->state->transitionToIdle());

    std::string relayed = drainOutputJackString(t->device.inputJackSerial);
    EXPECT_EQ(relayed, "event:break");
}

// 9. Double press doesn't double count — second press ignored
inline void supporterDoublePressIgnored(SupporterReadyTests* t) {
    parameterizedCallbackFunction capturedPrimary;
    void* capturedPrimaryCtx = nullptr;

    ON_CALL(*t->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillByDefault([&](parameterizedCallbackFunction fn, void* ctx, ButtonInteraction) {
            capturedPrimary = fn;
            capturedPrimaryCtx = ctx;
        });
    ON_CALL(*t->device.mockSecondaryButton, setButtonPress(_, _, _))
        .WillByDefault([](parameterizedCallbackFunction, void*, ButtonInteraction) {});

    t->state->onStateMounted(&t->device);
    ASSERT_NE(capturedPrimary, nullptr);

    // First press -> sends confirm:1
    capturedPrimary(capturedPrimaryCtx);
    std::string first = drainOutputJackString(t->device.outputJackSerial);
    EXPECT_EQ(first, "confirm:1");

    // Second press -> should NOT send anything (already pressed)
    capturedPrimary(capturedPrimaryCtx);
    // Queue should be empty
    EXPECT_TRUE(t->device.outputJackSerial.msgQueue.empty());
}
