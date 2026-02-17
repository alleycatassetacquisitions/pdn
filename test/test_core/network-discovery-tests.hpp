#pragma once

#include <gtest/gtest.h>
#include "network/discovery.hpp"
#include <cstring>

class NetworkDiscoveryTestSuite : public testing::Test {
protected:
    void SetUp() override {
        discovery1 = new DeviceDiscovery();
        discovery2 = new DeviceDiscovery();
        currentTime = 0;
    }

    void TearDown() override {
        delete discovery1;
        delete discovery2;
    }

    DeviceDiscovery* discovery1;
    DeviceDiscovery* discovery2;
    uint32_t currentTime;
};

// ============================================
// INITIALIZATION TESTS
// ============================================

inline void discoveryInitializesWithDeviceInfo(DeviceDiscovery* discovery) {
    bool result = discovery->init("TEST-001", "Alice");

    EXPECT_TRUE(result);
    EXPECT_EQ(discovery->getPairingState(), PairingState::IDLE);
    EXPECT_FALSE(discovery->isScanning());
}

inline void discoveryCannotReinitialize(DeviceDiscovery* discovery) {
    discovery->init("TEST-001", "Alice");

    // Second init should succeed but be a no-op
    bool result = discovery->init("TEST-002", "Bob");
    EXPECT_TRUE(result);
}

inline void discoveryShutdownClearsState(DeviceDiscovery* discovery) {
    discovery->init("TEST-001", "Alice");
    discovery->startScan(5000);

    discovery->shutdown();

    EXPECT_EQ(discovery->getPairingState(), PairingState::IDLE);
    EXPECT_FALSE(discovery->isScanning());
}

// ============================================
// SCANNING TESTS
// ============================================

inline void scanningChangesStateToScanning(DeviceDiscovery* discovery, uint32_t* currentTime) {
    discovery->init("TEST-001", "Alice");

    discovery->startScan(3000);

    EXPECT_TRUE(discovery->isScanning());
    EXPECT_EQ(discovery->getPairingState(), PairingState::SCANNING);
}

inline void scanningTimesOutAfterDuration(DeviceDiscovery* discovery, uint32_t* currentTime) {
    discovery->init("TEST-001", "Alice");

    discovery->startScan(3000);

    // Simulate time passing - start at 100 to avoid edge case with 0
    *currentTime = 100;
    discovery->update(*currentTime);
    EXPECT_TRUE(discovery->isScanning());

    *currentTime = 1000;
    discovery->update(*currentTime);
    EXPECT_TRUE(discovery->isScanning());

    // At 3200ms (3100ms elapsed since start), should timeout
    *currentTime = 3200;
    discovery->update(*currentTime);
    EXPECT_FALSE(discovery->isScanning());
    EXPECT_EQ(discovery->getPairingState(), PairingState::IDLE);
}

inline void scanningCanBeStoppedEarly(DeviceDiscovery* discovery, uint32_t* currentTime) {
    discovery->init("TEST-001", "Alice");

    discovery->startScan(5000);
    EXPECT_TRUE(discovery->isScanning());

    discovery->stopScan();
    EXPECT_FALSE(discovery->isScanning());
    EXPECT_EQ(discovery->getPairingState(), PairingState::IDLE);
}

inline void scanningClearsDiscoveredDevices(DeviceDiscovery* discovery, uint32_t* currentTime) {
    discovery->init("TEST-001", "Alice");

    // Start scan, get some devices (simulated via packet handling)
    discovery->startScan(3000);

    // Stop and start a new scan
    discovery->stopScan();
    discovery->startScan(3000);

    // Discovered devices should be cleared
    std::vector<DeviceInfo> devices = discovery->getDiscoveredDevices();
    EXPECT_EQ(devices.size(), 0);
}

// ============================================
// PRESENCE PACKET TESTS
// ============================================

inline void presencePacketDiscoveredDeviceAdded(DeviceDiscovery* discovery1) {
    discovery1->init("HUNTER-001", "Alice");

    discovery1->startScan(5000);

    // Build a presence packet from device 2
    uint8_t srcMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    DiscoveryPacket packet;
    std::memset(&packet, 0, sizeof(packet));
    packet.type = static_cast<uint8_t>(DiscoveryPacketType::PRESENCE);
    packet.version = 0x01;
    std::strncpy(packet.deviceId, "BOUNTY-001", sizeof(packet.deviceId) - 1);
    std::strncpy(packet.playerName, "Bob", sizeof(packet.playerName) - 1);
    packet.allegiance = 1;
    packet.level = 5;
    packet.available = 1;

    // Device 1 receives the packet
    discovery1->handleDiscoveryPacket(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    // Verify device was discovered
    std::vector<DeviceInfo> devices = discovery1->getDiscoveredDevices();
    EXPECT_EQ(devices.size(), 1);
    EXPECT_EQ(devices[0].deviceId, "BOUNTY-001");
    EXPECT_EQ(devices[0].playerName, "Bob");
    EXPECT_EQ(devices[0].allegiance, 1);
    EXPECT_EQ(devices[0].level, 5);
    EXPECT_TRUE(devices[0].available);
}

inline void presencePacketFromSelfIsIgnored(DeviceDiscovery* discovery) {
    discovery->init("TEST-001", "Alice");

    discovery->startScan(5000);

    // Build a presence packet from ourselves
    uint8_t srcMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    DiscoveryPacket packet;
    std::memset(&packet, 0, sizeof(packet));
    packet.type = static_cast<uint8_t>(DiscoveryPacketType::PRESENCE);
    packet.version = 0x01;
    std::strncpy(packet.deviceId, "TEST-001", sizeof(packet.deviceId) - 1);
    std::strncpy(packet.playerName, "Alice", sizeof(packet.playerName) - 1);

    discovery->handleDiscoveryPacket(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    // Should not discover ourselves
    std::vector<DeviceInfo> devices = discovery->getDiscoveredDevices();
    EXPECT_EQ(devices.size(), 0);
}

// ============================================
// PAIRING TESTS
// ============================================

inline void pairRequestChangesState(DeviceDiscovery* discovery1) {
    discovery1->init("HUNTER-001", "Alice");

    discovery1->startScan(5000);

    // Simulate device 2 being discovered
    uint8_t srcMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    DiscoveryPacket packet;
    std::memset(&packet, 0, sizeof(packet));
    packet.type = static_cast<uint8_t>(DiscoveryPacketType::PRESENCE);
    packet.version = 0x01;
    std::strncpy(packet.deviceId, "BOUNTY-001", sizeof(packet.deviceId) - 1);
    std::strncpy(packet.playerName, "Bob", sizeof(packet.playerName) - 1);
    packet.available = 1;

    discovery1->handleDiscoveryPacket(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    // Request pairing
    bool result = discovery1->requestPair("BOUNTY-001");

    EXPECT_TRUE(result);
    EXPECT_EQ(discovery1->getPairingState(), PairingState::PAIR_REQUEST_SENT);
}

inline void pairRequestToUnknownDeviceFails(DeviceDiscovery* discovery) {
    discovery->init("HUNTER-001", "Alice");

    bool result = discovery->requestPair("UNKNOWN-DEVICE");

    EXPECT_FALSE(result);
    EXPECT_EQ(discovery->getPairingState(), PairingState::IDLE);
}

inline void pairRequestWhenAlreadyPairedFails(DeviceDiscovery* discovery1) {
    discovery1->init("HUNTER-001", "Alice");

    // Simulate being already paired
    uint8_t srcMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    DiscoveryPacket presencePacket;
    std::memset(&presencePacket, 0, sizeof(presencePacket));
    presencePacket.type = static_cast<uint8_t>(DiscoveryPacketType::PRESENCE);
    presencePacket.version = 0x01;
    std::strncpy(presencePacket.deviceId, "BOUNTY-001", sizeof(presencePacket.deviceId) - 1);
    presencePacket.available = 1;

    discovery1->handleDiscoveryPacket(srcMac, reinterpret_cast<uint8_t*>(&presencePacket), sizeof(presencePacket));
    discovery1->requestPair("BOUNTY-001");

    // Simulate accept response
    DiscoveryPacket acceptPacket;
    std::memset(&acceptPacket, 0, sizeof(acceptPacket));
    acceptPacket.type = static_cast<uint8_t>(DiscoveryPacketType::PAIR_ACCEPT);
    acceptPacket.version = 0x01;
    std::strncpy(acceptPacket.deviceId, "BOUNTY-001", sizeof(acceptPacket.deviceId) - 1);

    discovery1->handleDiscoveryPacket(srcMac, reinterpret_cast<uint8_t*>(&acceptPacket), sizeof(acceptPacket));

    EXPECT_EQ(discovery1->getPairingState(), PairingState::PAIRED);

    // Try to pair with another device
    bool result = discovery1->requestPair("ANOTHER-DEVICE");
    EXPECT_FALSE(result);
}

// ============================================
// CALLBACK TESTS
// ============================================

inline void deviceFoundCallbackInvoked(DeviceDiscovery* discovery) {
    discovery->init("TEST-001", "Alice");

    bool callbackInvoked = false;
    std::string foundDeviceId;

    discovery->onDeviceFound([&callbackInvoked, &foundDeviceId](const DeviceInfo& info) {
        callbackInvoked = true;
        foundDeviceId = info.deviceId;
    });

    discovery->startScan(5000);

    // Simulate discovering a device
    uint8_t srcMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    DiscoveryPacket packet;
    std::memset(&packet, 0, sizeof(packet));
    packet.type = static_cast<uint8_t>(DiscoveryPacketType::PRESENCE);
    packet.version = 0x01;
    std::strncpy(packet.deviceId, "FOUND-001", sizeof(packet.deviceId) - 1);

    discovery->handleDiscoveryPacket(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    EXPECT_TRUE(callbackInvoked);
    EXPECT_EQ(foundDeviceId, "FOUND-001");
}

inline void pairCompleteCallbackInvokedOnSuccess(DeviceDiscovery* discovery) {
    discovery->init("TEST-001", "Alice");

    bool callbackInvoked = false;
    bool pairSuccess = false;

    discovery->onPairComplete([&callbackInvoked, &pairSuccess](bool success) {
        callbackInvoked = true;
        pairSuccess = success;
    });

    // Simulate discovering a device
    uint8_t srcMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    DiscoveryPacket presencePacket;
    std::memset(&presencePacket, 0, sizeof(presencePacket));
    presencePacket.type = static_cast<uint8_t>(DiscoveryPacketType::PRESENCE);
    presencePacket.version = 0x01;
    std::strncpy(presencePacket.deviceId, "TARGET-001", sizeof(presencePacket.deviceId) - 1);
    presencePacket.available = 1;

    discovery->handleDiscoveryPacket(srcMac, reinterpret_cast<uint8_t*>(&presencePacket), sizeof(presencePacket));
    discovery->requestPair("TARGET-001");

    // Simulate accept response
    DiscoveryPacket acceptPacket;
    std::memset(&acceptPacket, 0, sizeof(acceptPacket));
    acceptPacket.type = static_cast<uint8_t>(DiscoveryPacketType::PAIR_ACCEPT);
    acceptPacket.version = 0x01;
    std::strncpy(acceptPacket.deviceId, "TARGET-001", sizeof(acceptPacket.deviceId) - 1);

    discovery->handleDiscoveryPacket(srcMac, reinterpret_cast<uint8_t*>(&acceptPacket), sizeof(acceptPacket));

    EXPECT_TRUE(callbackInvoked);
    EXPECT_TRUE(pairSuccess);
}
