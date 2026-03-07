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

// Testable subclass that controls getPortStatus independently of handshake state machines.
class TestableRDC : public RemoteDeviceCoordinator {
public:
    PortStatus outputStatus = PortStatus::DISCONNECTED;
    PortStatus inputStatus = PortStatus::DISCONNECTED;

    PortStatus getPortStatus(SerialIdentifier port) override {
        return (port == SerialIdentifier::OUTPUT_JACK) ? outputStatus : inputStatus;
    }
};

class ChainBreakTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);
        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    TestableRDC rdc;
    FakePlatformClock* fakeClock;
};

// 1. OUTPUT_JACK transitions CONNECTED -> DISCONNECTED fires callback with OUTPUT_JACK
inline void rdcFiresDisconnectCallbackOnOutputPortTransition(ChainBreakTests* t) {
    bool called = false;
    SerialIdentifier disconnectedPort = SerialIdentifier::INPUT_JACK;

    t->rdc.setOnDisconnectCallback([&](SerialIdentifier port) {
        called = true;
        disconnectedPort = port;
    });

    t->rdc.outputStatus = PortStatus::CONNECTED;
    t->rdc.checkForDisconnects();
    EXPECT_FALSE(called);

    t->rdc.outputStatus = PortStatus::DISCONNECTED;
    t->rdc.checkForDisconnects();

    EXPECT_TRUE(called);
    EXPECT_EQ(disconnectedPort, SerialIdentifier::OUTPUT_JACK);
}

// 2. Port stays CONNECTED across two checks -> no callback
inline void rdcDoesNotFireCallbackWhenStillConnected(ChainBreakTests* t) {
    bool called = false;
    t->rdc.setOnDisconnectCallback([&](SerialIdentifier) { called = true; });

    t->rdc.outputStatus = PortStatus::CONNECTED;
    t->rdc.checkForDisconnects();
    t->rdc.checkForDisconnects();

    EXPECT_FALSE(called);
}

// 3. Port stays DISCONNECTED across two checks -> no callback
inline void rdcDoesNotFireCallbackWhenAlreadyDisconnected(ChainBreakTests* t) {
    bool called = false;
    t->rdc.setOnDisconnectCallback([&](SerialIdentifier) { called = true; });

    t->rdc.outputStatus = PortStatus::DISCONNECTED;
    t->rdc.checkForDisconnects();
    t->rdc.checkForDisconnects();

    EXPECT_FALSE(called);
}

// 4. clearOnDisconnectCallback prevents notification after a connect->disconnect transition
inline void rdcClearCallbackPreventsNotification(ChainBreakTests* t) {
    bool called = false;
    t->rdc.setOnDisconnectCallback([&](SerialIdentifier) { called = true; });
    t->rdc.clearOnDisconnectCallback();

    t->rdc.outputStatus = PortStatus::CONNECTED;
    t->rdc.checkForDisconnects();
    t->rdc.outputStatus = PortStatus::DISCONNECTED;
    t->rdc.checkForDisconnects();

    EXPECT_FALSE(called);
}

// 5. INPUT_JACK transitions CONNECTED -> DISCONNECTED fires callback with INPUT_JACK
inline void rdcFiresDisconnectForInputJack(ChainBreakTests* t) {
    bool called = false;
    SerialIdentifier disconnectedPort = SerialIdentifier::OUTPUT_JACK;

    t->rdc.setOnDisconnectCallback([&](SerialIdentifier port) {
        called = true;
        disconnectedPort = port;
    });

    t->rdc.inputStatus = PortStatus::CONNECTED;
    t->rdc.checkForDisconnects();
    EXPECT_FALSE(called);

    t->rdc.inputStatus = PortStatus::DISCONNECTED;
    t->rdc.checkForDisconnects();

    EXPECT_TRUE(called);
    EXPECT_EQ(disconnectedPort, SerialIdentifier::INPUT_JACK);
}

// ============================================
// DuelCountdown disconnect propagation tests
// ============================================

static std::string drainJackString(FakeHWSerialWrapper& serial) {
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

class ChainDuelStateBreakTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(*device.mockPrimaryButton, setButtonPress(_, _, _)).WillByDefault(Return());
        ON_CALL(*device.mockSecondaryButton, setButtonPress(_, _, _)).WillByDefault(Return());
        ON_CALL(*device.mockPrimaryButton, removeButtonCallbacks()).WillByDefault(Return());
        ON_CALL(*device.mockSecondaryButton, removeButtonCallbacks()).WillByDefault(Return());
        ON_CALL(*device.mockHaptics, setIntensity(_)).WillByDefault(Return());
        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, render()).WillByDefault(Return());

        device.rdcOverride = &rdc;
        rdc.initialize(device.wirelessManager, device.serialManager, &device);

        context.chainLength = 3;
        context.role = ChainRole::CHAMPION;
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    RemoteDeviceCoordinator rdc;
    FakePlatformClock* fakeClock;
    ChainContext context;
    Player player;
    MatchManager matchManager;
};

// 6. DuelCountdown in chain mode registers disconnect callback on mount;
//    OUTPUT_JACK disconnect sends "event:break" to INPUT_JACK
inline void duelCountdownSendsBreakOnOutputDisconnect(ChainDuelStateBreakTests* t) {
    DuelCountdown state(&t->player, &t->matchManager, &t->rdc, &t->context);
    state.onStateMounted(&t->device);

    // Simulate OUTPUT_JACK disconnect via the RDC callback mechanism
    // The state registers a callback; we trigger it directly by simulating what
    // checkForDisconnects() would do: call the registered onDisconnectCallback with OUTPUT_JACK.
    // Since the real RDC's callback is set via setOnDisconnectCallback, we verify it was set
    // and fire it manually.
    auto* rdcBase = static_cast<RemoteDeviceCoordinator*>(&t->rdc);
    // Fire disconnect for OUTPUT_JACK
    rdcBase->fireDisconnectCallbackForTest(SerialIdentifier::OUTPUT_JACK);

    std::string sent = drainJackString(t->device.inputJackSerial);
    EXPECT_EQ(sent, "event:break");

    state.onStateDismounted(&t->device);
}

// 7. DuelCountdown clears disconnect callback on dismount
inline void duelCountdownClearsDisconnectCallbackOnDismount(ChainDuelStateBreakTests* t) {
    DuelCountdown state(&t->player, &t->matchManager, &t->rdc, &t->context);
    state.onStateMounted(&t->device);
    state.onStateDismounted(&t->device);

    // After dismount, firing the disconnect should NOT send event:break
    t->rdc.fireDisconnectCallbackForTest(SerialIdentifier::OUTPUT_JACK);

    EXPECT_TRUE(t->device.inputJackSerial.msgQueue.empty());
}

// 8. Duel state in chain mode registers disconnect callback; OUTPUT_JACK disconnect sends event:break
inline void duelStateSendsBreakOnOutputDisconnect(ChainDuelStateBreakTests* t) {
    Duel state(&t->player, &t->matchManager, &t->rdc, &t->context);
    state.onStateMounted(&t->device);

    // Drain the "event:draw" sent on mount
    t->device.inputJackSerial.msgQueue.clear();

    t->rdc.fireDisconnectCallbackForTest(SerialIdentifier::OUTPUT_JACK);

    std::string sent = drainJackString(t->device.inputJackSerial);
    EXPECT_EQ(sent, "event:break");

    state.onStateDismounted(&t->device);
}

// 9. Duel state clears disconnect callback on dismount
inline void duelStateClearsDisconnectCallbackOnDismount(ChainDuelStateBreakTests* t) {
    Duel state(&t->player, &t->matchManager, &t->rdc, &t->context);
    state.onStateMounted(&t->device);
    state.onStateDismounted(&t->device);

    t->device.inputJackSerial.msgQueue.clear();
    t->rdc.fireDisconnectCallbackForTest(SerialIdentifier::OUTPUT_JACK);

    EXPECT_TRUE(t->device.inputJackSerial.msgQueue.empty());
}
