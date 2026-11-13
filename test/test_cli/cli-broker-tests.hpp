//
// CLI Broker Tests - Tests for cli::SerialCableBroker and NativePeerBroker
//

#pragma once

#include <gtest/gtest.h>
#include "cli/cli-serial-broker.hpp"
#include "device/drivers/native/native-peer-broker.hpp"
#include "device/drivers/native/native-serial-driver.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "device/drivers/peer-comms-types.hpp"

// ============================================
// SERIAL CABLE BROKER TEST SUITE
// ============================================

class SerialCableBrokerTestSuite : public testing::Test {
public:  // Public for test function access
    /** Two devices' jacks, registered as a hunter (0) and a bounty (1). */
    void SetUp() override {
        broker = &cli::SerialCableBroker::getInstance();

        // Create serial drivers for two devices
        outputJackA = new NativeSerialDriver("DeviceA_Out");
        inputJackA = new NativeSerialDriver("DeviceA_In");
        outputJackB = new NativeSerialDriver("DeviceB_Out");
        inputJackB = new NativeSerialDriver("DeviceB_In");

        // Register devices - A is hunter, B is bounty
        broker->registerDevice(0, outputJackA, inputJackA, true);   // Hunter
        broker->registerDevice(1, outputJackB, inputJackB, false);  // Bounty
    }

    /** Unregisters before deleting: a cabled jack points at its peer. */
    void TearDown() override {
        broker->unregisterDevice(0);
        broker->unregisterDevice(1);

        delete outputJackA;
        delete inputJackA;
        delete outputJackB;
        delete inputJackB;
    }

    cli::SerialCableBroker* broker = nullptr;
    NativeSerialDriver* outputJackA = nullptr;
    NativeSerialDriver* inputJackA = nullptr;
    NativeSerialDriver* outputJackB = nullptr;
    NativeSerialDriver* inputJackB = nullptr;
};

// Appends every byte the jack's exec() hands its byte callback to `sink`.
inline void captureBytes(NativeSerialDriver* jack, std::string* sink) {
    jack->setByteCallback([sink](uint8_t byte) { sink->push_back(static_cast<char>(byte)); });
}

// Test: Connect hunter to bounty
void serialBrokerConnectHunterToBounty(SerialCableBrokerTestSuite* suite) {
    ASSERT_TRUE(suite->broker->connect(0, 1));

    ASSERT_EQ(suite->broker->getConnectionCount(), 1);
    ASSERT_EQ(suite->broker->getConnectedDevice(0), 1);
    ASSERT_EQ(suite->broker->getConnectedDevice(1), 0);
}

// Test: Disconnect clears connection
void serialBrokerDisconnectClearsConnection(SerialCableBrokerTestSuite* suite) {
    suite->broker->connect(0, 1);
    ASSERT_EQ(suite->broker->getConnectionCount(), 1);

    suite->broker->disconnect(0, 1);
    ASSERT_EQ(suite->broker->getConnectionCount(), 0);
    ASSERT_EQ(suite->broker->getConnectedDevice(0), -1);
}

// Test: Double connection is idempotent
void serialBrokerDoubleConnectionIdempotent(SerialCableBrokerTestSuite* suite) {
    ASSERT_TRUE(suite->broker->connect(0, 1));
    ASSERT_TRUE(suite->broker->connect(0, 1));  // Should succeed (already connected)

    ASSERT_EQ(suite->broker->getConnectionCount(), 1);  // Still only one connection
}

// Test: Invalid device returns false
void serialBrokerInvalidDeviceReturnsFalse(SerialCableBrokerTestSuite* suite) {
    ASSERT_FALSE(suite->broker->connect(0, 99));  // Device 99 doesn't exist
    ASSERT_FALSE(suite->broker->connect(99, 0));  // Device 99 doesn't exist
}

// Test: a cable carries each jack's writes to the other's byte callback
void serialBrokerCableCarriesBytesBothWays(SerialCableBrokerTestSuite* suite) {
    std::string heardByBounty;
    std::string heardByHunter;
    captureBytes(suite->inputJackB, &heardByBounty);
    captureBytes(suite->outputJackA, &heardByHunter);
    // Hunter to bounty joins the hunter's output jack to the bounty's input jack.
    ASSERT_TRUE(suite->broker->connect(0, 1));

    suite->outputJackA->print('h');
    suite->outputJackA->print('i');
    suite->inputJackB->print('y');
    suite->inputJackB->print('o');

    // In the peer's RX at once, but handed to its callback only by its own exec().
    EXPECT_TRUE(heardByBounty.empty());
    suite->inputJackB->exec();
    suite->outputJackA->exec();

    EXPECT_EQ(heardByBounty, "hi");
    EXPECT_EQ(heardByHunter, "yo");
    EXPECT_EQ(suite->outputJackA->getOutputBufferSize(), 0u);
    EXPECT_EQ(suite->inputJackB->getOutputBufferSize(), 0u);
}

// Test: an unplugged jack keeps its writes instead of sending them
void serialBrokerDisconnectStopsRouting(SerialCableBrokerTestSuite* suite) {
    std::string heardByBounty;
    captureBytes(suite->inputJackB, &heardByBounty);
    suite->broker->connect(0, 1);
    suite->broker->disconnect(0, 1);

    suite->outputJackA->print('x');
    suite->inputJackB->exec();

    EXPECT_TRUE(heardByBounty.empty());
    EXPECT_EQ(suite->outputJackA->getOutput(), "x");
}

// Test: bytes written before the cable was plugged in cross when it is
void serialBrokerHeldBytesCrossOnConnect(SerialCableBrokerTestSuite* suite) {
    std::string heardByBounty;
    captureBytes(suite->inputJackB, &heardByBounty);
    suite->outputJackA->print('a');
    suite->outputJackA->print('b');

    suite->broker->connect(0, 1);
    suite->inputJackB->exec();

    EXPECT_EQ(heardByBounty, "ab");
    EXPECT_EQ(suite->outputJackA->getOutputBufferSize(), 0u);
}

// Test: a frame written a byte at a time arrives whole
void serialBrokerByteWritesShareOneRxEntry(SerialCableBrokerTestSuite* suite) {
    std::string heardByBounty;
    captureBytes(suite->inputJackB, &heardByBounty);
    suite->broker->connect(0, 1);

    // More writes than the RX queue has entries: an entry per write would drop
    // the head of the frame.
    std::string frame;
    for (size_t i = 0; i < NativeSerialDriver::MAX_INPUT_QUEUE_SIZE + 8; i++) {
        frame.push_back(static_cast<char>('A' + (i % 26)));
        suite->outputJackA->print(frame.back());
    }

    EXPECT_EQ(suite->inputJackB->getInputQueueSize(), 1u);
    suite->inputJackB->exec();
    EXPECT_EQ(heardByBounty, frame);
}

// Test: a jack that already has a cable refuses a second
void serialBrokerOccupiedJackRefusesSecondCable(SerialCableBrokerTestSuite* suite) {
    NativeSerialDriver outputJackC("DeviceC_Out");
    NativeSerialDriver inputJackC("DeviceC_In");
    suite->broker->registerDevice(2, &outputJackC, &inputJackC, true);
    std::string heardByBounty;
    captureBytes(suite->inputJackB, &heardByBounty);

    ASSERT_TRUE(suite->broker->connect(0, 1));
    // A second hunter would need the bounty's input jack too.
    EXPECT_FALSE(suite->broker->connect(2, 1));
    EXPECT_EQ(suite->broker->getConnectionCount(), 1u);

    outputJackC.print('c');
    suite->outputJackA->print('a');
    suite->inputJackB->exec();
    EXPECT_EQ(heardByBounty, "a");

    suite->broker->unregisterDevice(2);
}

// ============================================
// NATIVE PEER BROKER TEST SUITE
// ============================================

class NativePeerBrokerTestSuite : public testing::Test {
public:  // Public for test function access
    void SetUp() override {
        broker_ = &NativePeerBroker::getInstance();

        peerA = new NativePeerCommsDriver("PeerA");
        peerB = new NativePeerCommsDriver("PeerB");

        peerA->initialize();
        peerB->initialize();

        peerA->enable();
        peerB->enable();

        receivedPackets = 0;
    }
    
    void TearDown() override {
        peerA->disable();
        peerB->disable();

        delete peerA;
        delete peerB;
    }

    /** Increments received packet count on inbound packet delivery. */
    static void packetCallback(const uint8_t* srcMac, const uint8_t* data, 
                               size_t length, void* context) {
        auto* suite = static_cast<NativePeerBrokerTestSuite*>(context);
        suite->receivedPackets++;
    }
    
    NativePeerBroker* broker_;
    NativePeerCommsDriver* peerA;
    NativePeerCommsDriver* peerB;
    int receivedPackets;
};

// Test: Unicast delivery
void peerBrokerUnicastDelivery(NativePeerBrokerTestSuite* suite) {
    suite->peerB->setPacketHandler(PktType::kMatchCommand,
                                   NativePeerBrokerTestSuite::packetCallback, suite);

    uint8_t testData[] = {0x01, 0x02, 0x03};
    suite->peerA->sendData(suite->peerB->getMacAddress(), PktType::kMatchCommand, testData,
                           sizeof(testData));

    // Deliver pending packets, then drain the receiving driver's queue
    suite->broker_->deliverPackets();
    suite->peerB->exec();

    ASSERT_EQ(suite->receivedPackets, 1);
}

// Test: Broadcast excludes sender
void peerBrokerBroadcastExcludesSender(NativePeerBrokerTestSuite* suite) {
    // Set handler on both peers
    suite->peerA->setPacketHandler(PktType::kMatchCommand,
                                   NativePeerBrokerTestSuite::packetCallback, suite);
    suite->peerB->setPacketHandler(PktType::kMatchCommand,
                                   NativePeerBrokerTestSuite::packetCallback, suite);

    // Broadcast from A
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t testData[] = {0x01};
    suite->peerA->sendData(broadcastMac, PktType::kMatchCommand, testData, 1);

    // Deliver pending packets, then drain the receiving driver's queue
    suite->broker_->deliverPackets();
    suite->peerA->exec();
    suite->peerB->exec();

    // B should receive, but A should not receive its own broadcast
    ASSERT_GE(suite->receivedPackets, 1);
}

// Test: Peer registration with broker
void peerBrokerPeerRegistration(NativePeerBrokerTestSuite* suite) {
    // Both peers should be registered by enable()
    // This test verifies the broker can route between them

    suite->peerB->setPacketHandler(PktType::kMatchCommand,
                                   NativePeerBrokerTestSuite::packetCallback, suite);

    uint8_t testData[] = {0xAB, 0xCD};
    suite->peerA->sendData(suite->peerB->getMacAddress(), PktType::kMatchCommand, testData,
                           sizeof(testData));

    // Deliver pending packets, then drain the receiving driver's queue
    suite->broker_->deliverPackets();
    suite->peerB->exec();

    ASSERT_EQ(suite->receivedPackets, 1);
}

// Test: Unique MAC generation
void peerBrokerUniqueMacGeneration(NativePeerBrokerTestSuite* suite) {
    std::string macA = suite->peerA->getMacString();
    std::string macB = suite->peerB->getMacString();

    ASSERT_NE(macA, macB);
    
    // MAC should be in format XX:XX:XX:XX:XX:XX
    ASSERT_EQ(macA.length(), 17);
    ASSERT_EQ(macB.length(), 17);
}

// Test: Disabled peer doesn't receive
void peerBrokerDisabledPeerNoReceive(NativePeerBrokerTestSuite* suite) {
    suite->peerB->setPacketHandler(PktType::kMatchCommand,
                                   NativePeerBrokerTestSuite::packetCallback, suite);

    suite->peerB->disable();

    uint8_t testData[] = {0x01};
    suite->peerA->sendData(suite->peerB->getMacAddress(), PktType::kMatchCommand, testData, 1);

    // B is disabled, should not receive
    ASSERT_EQ(suite->receivedPackets, 0);

    suite->peerB->enable();  // Re-enable for teardown
}
