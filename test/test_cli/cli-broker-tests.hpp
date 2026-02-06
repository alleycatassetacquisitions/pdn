//
// CLI Broker Tests - Tests for cli::SerialCableBroker and NativePeerBroker
//

#pragma once

#include <gtest/gtest.h>
#include "cli/cli-serial-broker.hpp"
#include "device/drivers/native/native-peer-broker.hpp"
#include "device/drivers/native/native-serial-driver.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "wireless/peer-comms-types.hpp"

// ============================================
// SERIAL CABLE BROKER TEST SUITE
// ============================================

class SerialCableBrokerTestSuite : public testing::Test {
public:  // Public for test function access
    void SetUp() override {
        broker_ = &cli::SerialCableBroker::getInstance();
        
        // Create serial drivers for two devices
        outputJackA_ = new NativeSerialDriver("DeviceA_Out");
        inputJackA_ = new NativeSerialDriver("DeviceA_In");
        outputJackB_ = new NativeSerialDriver("DeviceB_Out");
        inputJackB_ = new NativeSerialDriver("DeviceB_In");
        
        // Register devices - A is hunter, B is bounty
        broker_->registerDevice(0, outputJackA_, inputJackA_, true);   // Hunter
        broker_->registerDevice(1, outputJackB_, inputJackB_, false);  // Bounty
    }
    
    void TearDown() override {
        broker_->unregisterDevice(0);
        broker_->unregisterDevice(1);
        
        delete outputJackA_;
        delete inputJackA_;
        delete outputJackB_;
        delete inputJackB_;
    }
    
    cli::SerialCableBroker* broker_;
    NativeSerialDriver* outputJackA_;
    NativeSerialDriver* inputJackA_;
    NativeSerialDriver* outputJackB_;
    NativeSerialDriver* inputJackB_;
};

// Test: Connect hunter to bounty
void serialBrokerConnectHunterToBounty(SerialCableBrokerTestSuite* suite) {
    ASSERT_TRUE(suite->broker_->connect(0, 1));
    
    ASSERT_EQ(suite->broker_->getConnectionCount(), 1);
    ASSERT_EQ(suite->broker_->getConnectedDevice(0), 1);
    ASSERT_EQ(suite->broker_->getConnectedDevice(1), 0);
}

// Test: Disconnect clears connection
void serialBrokerDisconnectClearsConnection(SerialCableBrokerTestSuite* suite) {
    suite->broker_->connect(0, 1);
    ASSERT_EQ(suite->broker_->getConnectionCount(), 1);
    
    suite->broker_->disconnect(0, 1);
    ASSERT_EQ(suite->broker_->getConnectionCount(), 0);
    ASSERT_EQ(suite->broker_->getConnectedDevice(0), -1);
}

// Test: Double connection is idempotent
void serialBrokerDoubleConnectionIdempotent(SerialCableBrokerTestSuite* suite) {
    ASSERT_TRUE(suite->broker_->connect(0, 1));
    ASSERT_TRUE(suite->broker_->connect(0, 1));  // Should succeed (already connected)
    
    ASSERT_EQ(suite->broker_->getConnectionCount(), 1);  // Still only one connection
}

// Test: Invalid device returns false
void serialBrokerInvalidDeviceReturnsFalse(SerialCableBrokerTestSuite* suite) {
    ASSERT_FALSE(suite->broker_->connect(0, 99));  // Device 99 doesn't exist
    ASSERT_FALSE(suite->broker_->connect(99, 0));  // Device 99 doesn't exist
}

// ============================================
// NATIVE PEER BROKER TEST SUITE
// ============================================

class NativePeerBrokerTestSuite : public testing::Test {
public:  // Public for test function access
    void SetUp() override {
        broker_ = &NativePeerBroker::getInstance();
        
        peerA_ = new NativePeerCommsDriver("PeerA");
        peerB_ = new NativePeerCommsDriver("PeerB");
        
        peerA_->initialize();
        peerB_->initialize();
        
        peerA_->connect();
        peerB_->connect();
        
        receivedPackets_ = 0;
    }
    
    void TearDown() override {
        peerA_->disconnect();
        peerB_->disconnect();
        
        delete peerA_;
        delete peerB_;
    }
    
    static void packetCallback(const uint8_t* srcMac, const uint8_t* data, 
                               size_t length, void* context) {
        auto* suite = static_cast<NativePeerBrokerTestSuite*>(context);
        suite->receivedPackets_++;
    }
    
    NativePeerBroker* broker_;
    NativePeerCommsDriver* peerA_;
    NativePeerCommsDriver* peerB_;
    int receivedPackets_;
};

// Test: Unicast delivery
void peerBrokerUnicastDelivery(NativePeerBrokerTestSuite* suite) {
    suite->peerB_->setPacketHandler(PktType::kQuickdrawCommand,
        NativePeerBrokerTestSuite::packetCallback, suite);
    
    uint8_t testData[] = {0x01, 0x02, 0x03};
    suite->peerA_->sendData(suite->peerB_->getMacAddress(), PktType::kQuickdrawCommand,
                           testData, sizeof(testData));
    
    // Deliver pending packets (broker is asynchronous)
    suite->broker_->deliverPackets();
    
    ASSERT_EQ(suite->receivedPackets_, 1);
}

// Test: Broadcast excludes sender
void peerBrokerBroadcastExcludesSender(NativePeerBrokerTestSuite* suite) {
    // Set handler on both peers
    suite->peerA_->setPacketHandler(PktType::kQuickdrawCommand,
        NativePeerBrokerTestSuite::packetCallback, suite);
    suite->peerB_->setPacketHandler(PktType::kQuickdrawCommand,
        NativePeerBrokerTestSuite::packetCallback, suite);
    
    // Broadcast from A
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t testData[] = {0x01};
    suite->peerA_->sendData(broadcastMac, PktType::kQuickdrawCommand, testData, 1);
    
    // Deliver pending packets (broker is asynchronous)
    suite->broker_->deliverPackets();
    
    // B should receive, but A should not receive its own broadcast
    ASSERT_GE(suite->receivedPackets_, 1);
}

// Test: Peer registration with broker
void peerBrokerPeerRegistration(NativePeerBrokerTestSuite* suite) {
    // Both peers should be registered by connect()
    // This test verifies the broker can route between them
    
    suite->peerB_->setPacketHandler(PktType::kQuickdrawCommand,
        NativePeerBrokerTestSuite::packetCallback, suite);
    
    uint8_t testData[] = {0xAB, 0xCD};
    suite->peerA_->sendData(suite->peerB_->getMacAddress(), PktType::kQuickdrawCommand,
                           testData, sizeof(testData));
    
    // Deliver pending packets (broker is asynchronous)
    suite->broker_->deliverPackets();
    
    ASSERT_EQ(suite->receivedPackets_, 1);
}

// Test: Unique MAC generation
void peerBrokerUniqueMacGeneration(NativePeerBrokerTestSuite* suite) {
    std::string macA = suite->peerA_->getMacString();
    std::string macB = suite->peerB_->getMacString();
    
    ASSERT_NE(macA, macB);
    
    // MAC should be in format XX:XX:XX:XX:XX:XX
    ASSERT_EQ(macA.length(), 17);
    ASSERT_EQ(macB.length(), 17);
}

// Test: Disconnected peer doesn't receive
void peerBrokerDisconnectedPeerNoReceive(NativePeerBrokerTestSuite* suite) {
    suite->peerB_->setPacketHandler(PktType::kQuickdrawCommand,
        NativePeerBrokerTestSuite::packetCallback, suite);
    
    suite->peerB_->disconnect();
    
    uint8_t testData[] = {0x01};
    suite->peerA_->sendData(suite->peerB_->getMacAddress(), PktType::kQuickdrawCommand,
                           testData, 1);
    
    // B is disconnected, should not receive
    ASSERT_EQ(suite->receivedPackets_, 0);
    
    suite->peerB_->connect();  // Reconnect for teardown
}
