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

// Reads the string written by SerialManager::writeString from the FakeHWSerialWrapper msgQueue.
// writeString() calls print(STRING_START) then println(msg) which appends STRING_TERM.
// Format in queue: STRING_START + <content chars> + STRING_TERM
static std::string drainWrittenString(FakeHWSerialWrapper& serial) {
    auto& q = serial.msgQueue;
    if (q.empty()) return "";
    // Skip the STRING_START marker
    if (q.front() == STRING_START) q.pop_front();
    std::string result;
    while (!q.empty() && q.front() != STRING_TERM) {
        result += q.front();
        q.pop_front();
    }
    if (!q.empty()) q.pop_front(); // consume STRING_TERM
    return result;
}

class ChainDetectionTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));

        device.rdcOverride = &rdc;
        rdc.initialize(device.wirelessManager, device.serialManager, &device);

        state = new ChainDetectionState(&context, &rdc);
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
    ChainDetectionState* state;
};

inline void soloPlayerCompletesImmediately(ChainDetectionTests* t) {
    t->state->onStateMounted(&t->device);

    EXPECT_TRUE(t->context.detectionComplete);
    EXPECT_EQ(t->context.chainLength, 1);
    EXPECT_EQ(t->context.position, 0);
    EXPECT_EQ(t->context.role, ChainRole::CHAMPION);
}

inline void championSendsChainMessageOnMount(ChainDetectionTests* t) {
    struct ConnectedInputRDC : public RemoteDeviceCoordinator {
        RemoteDeviceCoordinator* real;
        PortStatus getPortStatus(SerialIdentifier port) override {
            if (port == SerialIdentifier::INPUT_JACK) return PortStatus::CONNECTED;
            return PortStatus::DISCONNECTED;
        }
        void registerSerialHandler(const std::string& prefix, SerialIdentifier jack,
                                   std::function<void(const std::string&)> handler) override {
            real->registerSerialHandler(prefix, jack, handler);
        }
        void unregisterSerialHandler(const std::string& prefix, SerialIdentifier jack) override {
            real->unregisterSerialHandler(prefix, jack);
        }
    };

    ConnectedInputRDC proxyRdc;
    proxyRdc.real = &t->rdc;

    ChainDetectionState champState(&t->context, &proxyRdc);
    champState.onStateMounted(&t->device);

    std::string sent = drainWrittenString(t->device.inputJackSerial);
    EXPECT_EQ(sent, "chain:0");
    EXPECT_FALSE(t->context.detectionComplete);
}

inline void championReceivesLenSetsChainLength(ChainDetectionTests* t) {
    struct ConnectedInputRDC : public RemoteDeviceCoordinator {
        RemoteDeviceCoordinator* real;
        PortStatus getPortStatus(SerialIdentifier port) override {
            if (port == SerialIdentifier::INPUT_JACK) return PortStatus::CONNECTED;
            return PortStatus::DISCONNECTED;
        }
        void registerSerialHandler(const std::string& prefix, SerialIdentifier jack,
                                   std::function<void(const std::string&)> handler) override {
            real->registerSerialHandler(prefix, jack, handler);
        }
        void unregisterSerialHandler(const std::string& prefix, SerialIdentifier jack) override {
            real->unregisterSerialHandler(prefix, jack);
        }
    };

    ConnectedInputRDC proxyRdc;
    proxyRdc.real = &t->rdc;

    ChainDetectionState champState(&t->context, &proxyRdc);
    champState.onStateMounted(&t->device);

    t->device.inputJackSerial.stringCallback("len:3");

    EXPECT_EQ(t->context.chainLength, 3);
    EXPECT_TRUE(t->context.detectionComplete);
    EXPECT_EQ(t->context.role, ChainRole::CHAMPION);
}

// 4. Tail supporter receives "chain:2" on output jack -> position=3, SUPPORTER, sends "len:3" out output jack
inline void tailSupporterReceivesChainSendsLen(ChainDetectionTests* t) {
    // Tail: INPUT_JACK disconnected, mounts as solo, then chain: on OUTPUT_JACK overrides.

    t->state->onStateMounted(&t->device);

    // Deliver "chain:2" on OUTPUT_JACK
    t->device.outputJackSerial.stringCallback("chain:2");

    EXPECT_EQ(t->context.position, 3);
    EXPECT_EQ(t->context.role, ChainRole::SUPPORTER);
    EXPECT_EQ(t->context.chainLength, 3);
    EXPECT_TRUE(t->context.detectionComplete);

    std::string sent = drainWrittenString(t->device.outputJackSerial);
    EXPECT_EQ(sent, "len:3");
}

// 5. Mid-chain supporter receives "chain:1" -> position=2, SUPPORTER, sends "chain:2" out input jack
inline void midChainSupporterForwardsChain(ChainDetectionTests* t) {
    struct ConnectedInputRDC : public RemoteDeviceCoordinator {
        RemoteDeviceCoordinator* real;
        PortStatus getPortStatus(SerialIdentifier port) override {
            if (port == SerialIdentifier::INPUT_JACK) return PortStatus::CONNECTED;
            return PortStatus::DISCONNECTED;
        }
        void registerSerialHandler(const std::string& prefix, SerialIdentifier jack,
                                   std::function<void(const std::string&)> handler) override {
            real->registerSerialHandler(prefix, jack, handler);
        }
        void unregisterSerialHandler(const std::string& prefix, SerialIdentifier jack) override {
            real->unregisterSerialHandler(prefix, jack);
        }
    };

    ConnectedInputRDC proxyRdc;
    proxyRdc.real = &t->rdc;

    ChainDetectionState midState(&t->context, &proxyRdc);
    midState.onStateMounted(&t->device);

    // Clear the "chain:0" that champion would have sent (not applicable here, but flush queue)
    t->device.inputJackSerial.msgQueue.clear();

    // Deliver "chain:1" on OUTPUT_JACK
    t->device.outputJackSerial.stringCallback("chain:1");

    EXPECT_EQ(t->context.position, 2);
    EXPECT_EQ(t->context.role, ChainRole::SUPPORTER);
    EXPECT_FALSE(t->context.detectionComplete);

    std::string sent = drainWrittenString(t->device.inputJackSerial);
    EXPECT_EQ(sent, "chain:2");
}

// 6. Mid-chain supporter receives "len:4" on input jack -> relays "len:4" out output jack, chainLength=4, detectionComplete
inline void midChainSupporterRelaysLen(ChainDetectionTests* t) {
    struct ConnectedInputRDC : public RemoteDeviceCoordinator {
        RemoteDeviceCoordinator* real;
        PortStatus getPortStatus(SerialIdentifier port) override {
            if (port == SerialIdentifier::INPUT_JACK) return PortStatus::CONNECTED;
            return PortStatus::DISCONNECTED;
        }
        void registerSerialHandler(const std::string& prefix, SerialIdentifier jack,
                                   std::function<void(const std::string&)> handler) override {
            real->registerSerialHandler(prefix, jack, handler);
        }
        void unregisterSerialHandler(const std::string& prefix, SerialIdentifier jack) override {
            real->unregisterSerialHandler(prefix, jack);
        }
    };

    ConnectedInputRDC proxyRdc;
    proxyRdc.real = &t->rdc;

    ChainDetectionState midState(&t->context, &proxyRdc);
    midState.onStateMounted(&t->device);
    // Flush queued "chain:0" if any
    t->device.inputJackSerial.msgQueue.clear();

    // First: receive chain:1 to establish as supporter
    t->device.outputJackSerial.stringCallback("chain:1");
    // Flush forwarded chain:2
    t->device.inputJackSerial.msgQueue.clear();

    // Now receive len:4 on INPUT_JACK
    t->device.inputJackSerial.stringCallback("len:4");

    EXPECT_EQ(t->context.chainLength, 4);
    EXPECT_TRUE(t->context.detectionComplete);
    EXPECT_EQ(t->context.role, ChainRole::SUPPORTER);

    std::string relayed = drainWrittenString(t->device.outputJackSerial);
    EXPECT_EQ(relayed, "len:4");
}
