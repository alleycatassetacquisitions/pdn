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

// 1. Solo player: input jack DISCONNECTED -> detection completes immediately, position=0, length=1, CHAMPION
inline void soloPlayerCompletesImmediately(ChainDetectionTests* t) {
    // Both jacks disconnected by default in real RDC (no handshake done)
    t->state->onStateMounted(&t->device);

    EXPECT_TRUE(t->context.detectionComplete);
    EXPECT_EQ(t->context.chainLength, 1);
    EXPECT_EQ(t->context.position, 0);
    EXPECT_EQ(t->context.role, ChainRole::CHAMPION);
}

// 2. Champion sends "chain:0" out input jack on mount when input CONNECTED
inline void championSendsChainMessageOnMount(ChainDetectionTests* t) {
    // Simulate input jack connected by completing a handshake on input port.
    // We drive it via the serial callback that the RDC uses internally for MAC exchange.
    // Easier: use FakeRemoteDeviceCoordinator to set port status, but we have the real RDC.
    // Instead, manipulate the input port status by running the handshake protocol.
    // The simplest approach: subclass and override getPortStatus in a local mock.
    // Since we need the real serial routing, we use a custom approach:
    // Use a MockRDC that reports INPUT_JACK CONNECTED but delegates registerSerialHandler to real rdc.

    // Actually the cleanest approach for these tests: use the FakeRemoteDeviceCoordinator
    // which lets us set port status, but we need real serial routing too.
    // Solution: create a hybrid - set rdcOverride to a custom mock that:
    //   - returns CONNECTED for INPUT_JACK
    //   - delegates registerSerialHandler/unregisterSerialHandler to the real rdc

    // For simplicity, create a wrapper RDC that sets INPUT_JACK status while routing via real rdc
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

    // Should have sent "chain:0" out INPUT_JACK
    std::string sent = drainWrittenString(t->device.inputJackSerial);
    EXPECT_EQ(sent, "chain:0");
    EXPECT_FALSE(t->context.detectionComplete);
}

// 3. Champion receives "len:3" -> chainLength=3, detectionComplete=true
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

    // Champion is waiting for "len:" on INPUT_JACK
    t->device.inputJackSerial.stringCallback("len:3");

    EXPECT_EQ(t->context.chainLength, 3);
    EXPECT_TRUE(t->context.detectionComplete);
    EXPECT_EQ(t->context.role, ChainRole::CHAMPION);
}

// 4. Tail supporter receives "chain:2" on output jack -> position=3, SUPPORTER, sends "len:3" out output jack
inline void tailSupporterReceivesChainSendsLen(ChainDetectionTests* t) {
    // Input DISCONNECTED (tail), so it just receives chain: on output and sends len: back
    t->state->onStateMounted(&t->device);
    // After mount with disconnected input, detection completes as solo.
    // But for tail supporter test, we need output jack connected AND input disconnected.
    // The state is mounted before chain: arrives. Let's reset and remount with a clean context.

    // Reset context
    t->context = ChainContext{};

    // The state registers "chain:" handler on OUTPUT_JACK regardless of port status on mount.
    // Actually per spec: if input DISCONNECTED, solo player path.
    // For the tail supporter: input is disconnected, but the state still needs to register
    // "chain:" on OUTPUT_JACK to detect if it receives a chain message.
    // Re-reading the spec: if input DISCONNECTED on mount -> solo detection completes immediately.
    // But a tail supporter DOES have input disconnected. The detection for a tail happens
    // when it RECEIVES "chain:N" on OUTPUT_JACK.
    // This means: on mount, the state registers "chain:" on OUTPUT_JACK too (even for solo case?).
    // Actually looking at spec again:
    //   - if input DISCONNECTED: solo. set chainLength=1, detectionComplete=true, return.
    //   - if input CONNECTED: register handlers
    // So a solo player never registers anything. The tail supporter must have
    // its state mounted BEFORE the chain message arrives - meaning its input IS connected
    // or it registers handlers regardless.
    //
    // Re-reading: "Also register handler for 'chain:' on OUTPUT_JACK (in case this device
    // receives a chain message and is actually a supporter)."
    // This suggests BOTH handlers are registered when input is CONNECTED.
    // For a tail supporter: input is DISCONNECTED, so it would complete as solo immediately.
    //
    // But that can't be right for a real chain: the tail supporter has:
    //   - OUTPUT_JACK: connected to next device upstream
    //   - INPUT_JACK: disconnected (nothing further downstream)
    //
    // Wait - I need to re-read the jack naming. Looking at the serial-router-tests:
    //   outputJackSerial.stringCallback("chain:0") - simulates receiving on output jack
    //   inputJackSerial.stringCallback("chain:1")  - simulates receiving on input jack
    //
    // And in rdc-tests: outputJackSerial receives MAC addresses first.
    // The "output jack" appears to be the one that goes toward the previous device (champion side).
    // The "input jack" goes toward the next device in chain.
    //
    // For the CHAMPION: input jack is connected to the next supporter.
    //   Champion sends "chain:0" OUT the INPUT_JACK (toward next supporter).
    // For a MIDDLE supporter:
    //   OUTPUT_JACK receives "chain:N" (from champion side)
    //   sends "chain:N+1" out INPUT_JACK (toward tail)
    //   receives "len:M" on INPUT_JACK
    //   relays "len:M" out OUTPUT_JACK
    // For TAIL supporter:
    //   OUTPUT_JACK receives "chain:N"
    //   INPUT_JACK is disconnected
    //   sends "len:N+1" out OUTPUT_JACK
    //
    // So for tail: input IS disconnected but it still handles chain: on output.
    // The spec says: if input DISCONNECTED -> solo, return immediately.
    // But this conflicts with the tail supporter behavior.
    //
    // Resolution: the mount logic checks if input is disconnected for "solo detection",
    // but a tail supporter would have input disconnected AND receive chain: on output.
    // The "chain:" on OUTPUT_JACK handler must be registered REGARDLESS of input status,
    // so even the solo path still needs that handler registered.
    //
    // OR: the solo detection happens BEFORE any chain message arrives. A true solo
    // device will complete immediately. A tail supporter: even though input is disconnected,
    // it registered the "chain:" output handler and then gets overridden when chain: arrives.
    //
    // Given the spec says "if input DISCONNECTED: solo... return", and also says to register
    // "chain:" on OUTPUT_JACK in the connected branch, I think the tail supporter path is:
    // it mounts with input disconnected -> solo detection -> complete immediately.
    // That's wrong for tail supporters.
    //
    // Most likely interpretation: the spec's "if input jack DISCONNECTED: solo" means
    // specifically that NO other device is connected to this device at all. The OUTPUT_JACK
    // connectivity is what matters for the champion detection - the champion always has
    // output connected (to the controller/hub), while input may or may not be connected.
    //
    // Actually I think I've been confused about which jack is which. Let me re-read:
    // "If input jack DISCONNECTED (via RDC->getPortStatus): solo player."
    // "If input jack CONNECTED: this device might be champion. Register handler for 'len:' on INPUT_JACK."
    // "Send 'chain:0' out INPUT_JACK"
    // "Handler for 'chain:N' on OUTPUT_JACK"
    // "If input jack DISCONNECTED (tail): send 'len:{N+1}' out OUTPUT_JACK"
    //
    // So: Champion has INPUT_JACK connected (to next device).
    // Tail has INPUT_JACK disconnected within the "chain:N" handler.
    // The "chain:" handler on OUTPUT_JACK is ALWAYS registered on mount (not conditional).
    // The "solo" check: if input disconnected AND no chain: received on output.
    //
    // Actually re-reading one more time:
    // "If input jack CONNECTED: this device might be champion. Register handler for 'len:' on INPUT_JACK.
    //  Send 'chain:0' out INPUT_JACK via serialManager->writeString().
    //  Also register handler for 'chain:' on OUTPUT_JACK"
    //
    // The "also register handler for 'chain:' on OUTPUT_JACK" is in the INPUT_JACK CONNECTED branch.
    // And the OUTPUT_JACK chain: handler's tail sub-case checks if INPUT_JACK is disconnected.
    //
    // So: A tail supporter has INPUT_JACK CONNECTED (to the device before it) on OUTPUT_JACK side,
    // but wait no - the OUTPUT_JACK is connected to the upstream device.
    //
    // I think the jack naming is: output_jack points "out" toward the controller/network,
    // input_jack points "in" toward the end of the chain.
    //
    // Champion is at position 0. It has:
    //   - No OUTPUT_JACK neighbor (it IS the start)
    //   - INPUT_JACK connected to next supporter (position 1)
    //
    // Supporter at position 1 has:
    //   - OUTPUT_JACK connected to champion (position 0)
    //   - INPUT_JACK connected to next supporter (position 2)
    //
    // Tail supporter has:
    //   - OUTPUT_JACK connected to previous device
    //   - INPUT_JACK disconnected
    //
    // Solo player has:
    //   - OUTPUT_JACK disconnected
    //   - INPUT_JACK disconnected
    //
    // The spec says "If input jack DISCONNECTED -> solo". This makes sense for champion:
    // if the champion has no one downstream (INPUT_JACK disconnected), it's solo.
    //
    // For a tail supporter: INPUT_JACK IS disconnected, but it's not solo - it receives
    // chain: on OUTPUT_JACK. The chain: handler fires and within that handler, it checks
    // if INPUT_JACK is disconnected to determine it's the tail.
    //
    // So the full mount logic is:
    // 1. ALWAYS register "chain:" handler on OUTPUT_JACK
    // 2. If INPUT_JACK DISCONNECTED: solo path (chainLength=1, complete, return)
    //    BUT wait - if we return early, we never process chain: messages.
    //    The solo path must complete ONLY if no chain: arrives... but that's async.
    //
    // I think the design is:
    // - Champion detection: if INPUT_JACK connected -> might be champion -> send chain:0 out INPUT_JACK
    //   and register len: handler on INPUT_JACK. Also register chain: on OUTPUT_JACK in case
    //   this device is actually a supporter (receives chain: from upstream).
    // - If INPUT_JACK disconnected AND no chain: received -> solo (set complete immediately)
    //
    // For the tail: OUTPUT_JACK is connected, INPUT_JACK is disconnected.
    // On mount: register "chain:" on OUTPUT_JACK. Check INPUT_JACK -> disconnected -> solo? NO.
    // Wait, "solo" should only trigger if BOTH jacks are disconnected.
    //
    // But the spec literally says "If input jack DISCONNECTED -> solo player".
    // This would break the tail supporter case.
    //
    // UNLESS: "input jack" here refers to something different. Perhaps "input jack DISCONNECTED"
    // means "this device has no upstream connection" - i.e., OUTPUT_JACK is disconnected.
    // In that case, the champion checks OUTPUT_JACK (no upstream connection), and if
    // OUTPUT_JACK is connected, it knows it might be a supporter.
    //
    // Let me re-read the spec one more time very carefully:
    // "If input jack DISCONNECTED: solo player. Set chainLength=1, detectionComplete=true, return."
    // "If input jack CONNECTED: this device might be champion."
    //
    // Given the handler for "chain:N" is on OUTPUT_JACK, and within that handler:
    // "If input jack DISCONNECTED (tail): send 'len:{N+1}' out OUTPUT_JACK"
    //
    // The "input jack" in the handler refers to INPUT_JACK - used to detect tail (no downstream).
    // The "input jack DISCONNECTED" on mount must refer to a DIFFERENT condition.
    //
    // I believe "input jack" on mount means: the jack that receives FROM upstream (OUTPUT_JACK).
    // And "input jack" in the handler means: the jack that sends TO downstream (INPUT_JACK).
    // The naming is confusing because "output" and "input" might be from the PDN's perspective.
    //
    // Given the code: "registerSerialHandler for 'len:' on INPUT_JACK" and
    // "send 'chain:0' out INPUT_JACK" - the champion sends downstream via INPUT_JACK.
    // The "len:" reply comes BACK via INPUT_JACK.
    // This confirms: INPUT_JACK = downstream direction.
    //
    // So "If input jack DISCONNECTED -> solo" = no downstream device. Champion with no supporters.
    // For tail: INPUT_JACK disconnected, but OUTPUT_JACK connected (upstream exists).
    // The tail supporter is detected via the "chain:N" handler, which checks INPUT_JACK
    // to see if it should propagate further or be the tail.
    //
    // This means the mount logic MUST register "chain:" on OUTPUT_JACK BEFORE checking
    // INPUT_JACK for solo detection. OR the "solo" path must not return immediately but
    // instead just mark that it's potentially solo, and detection completes when no chain: arrives.
    //
    // OR: the real design is:
    // On mount, ALWAYS register "chain:" on OUTPUT_JACK.
    // Then check INPUT_JACK: if connected -> send "chain:0" and register "len:" on INPUT_JACK.
    //                        if disconnected -> set chainLength=1, detectionComplete=true.
    //                          (solo, but the chain: handler is still registered in case
    //                           this device is actually a tail supporter)
    //
    // The solo case SETS complete, but if a "chain:" arrives on OUTPUT_JACK, the handler
    // overrides the solo detection (resets detectionComplete, sets position, role=SUPPORTER, etc).
    //
    // This makes the most sense. The spec's "return" after solo detection is simplistic -
    // in reality you still need the OUTPUT_JACK handler registered for tail detection.
    //
    // For the tests, I'll implement what makes logical sense and matches the test cases specified.

    // For test 4: tail supporter receives "chain:2" -> position=3, SUPPORTER, sends "len:3"
    // Setup: device has INPUT_JACK disconnected (real RDC default), OUTPUT_JACK also disconnected.
    // State mounts -> solo path fires (length=1, complete=true).
    // Then chain: arrives on OUTPUT_JACK -> handler fires, overrides: position=3, SUPPORTER,
    // since INPUT_JACK disconnected -> sends "len:3" out OUTPUT_JACK, chainLength=3, complete=true.

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
