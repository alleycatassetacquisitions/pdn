#include "network/discovery.hpp"
#include <iostream>
#include <thread>
#include <chrono>

/**
 * Simple demo/test of the network discovery system.
 *
 * This demonstrates how the discovery API would be used in the CLI simulator.
 * Integration with the actual CLI commands would happen in cli-commands.hpp.
 */

void printDevices(const std::vector<DeviceInfo>& devices) {
    std::cout << "\nDiscovered devices:\n";
    if (devices.empty()) {
        std::cout << "  (none)\n";
        return;
    }

    for (size_t i = 0; i < devices.size(); i++) {
        const auto& dev = devices[i];
        std::cout << "  [" << i << "] " << dev.deviceId << " - " << dev.playerName;
        std::cout << " (Allegiance:" << static_cast<int>(dev.allegiance);
        std::cout << " Level:" << static_cast<int>(dev.level);
        std::cout << " Available:" << (dev.available ? "yes" : "no");
        std::cout << " Signal:" << static_cast<int>(dev.signalStrength) << "dBm)\n";
    }
}

void printPairingState(PairingState state) {
    switch (state) {
        case PairingState::IDLE:
            std::cout << "IDLE";
            break;
        case PairingState::SCANNING:
            std::cout << "SCANNING";
            break;
        case PairingState::PAIR_REQUEST_SENT:
            std::cout << "PAIR_REQUEST_SENT";
            break;
        case PairingState::PAIR_REQUEST_RECEIVED:
            std::cout << "PAIR_REQUEST_RECEIVED";
            break;
        case PairingState::PAIRED:
            std::cout << "PAIRED";
            break;
        case PairingState::DISCONNECTING:
            std::cout << "DISCONNECTING";
            break;
    }
}

#ifndef NATIVE_BUILD
int main() {
    std::cout << "=== Network Discovery System Demo ===\n\n";

    // Create two discovery instances (simulating two devices)
    DeviceDiscovery discovery1;
    DeviceDiscovery discovery2;

    // Initialize devices
    std::cout << "Initializing devices...\n";
    discovery1.init("HUNTER-001", "Alice");
    discovery2.init("BOUNTY-001", "Bob");

    // Set up callbacks for device 1
    discovery1.onDeviceFound([](const DeviceInfo& info) {
        std::cout << "  [HUNTER-001] Found device: " << info.deviceId
                  << " (" << info.playerName << ")\n";
    });

    discovery1.onPairRequest([](const DeviceInfo& info) {
        std::cout << "  [HUNTER-001] Pair request from: " << info.deviceId
                  << " (" << info.playerName << ")\n";
    });

    discovery1.onPairComplete([](bool success) {
        std::cout << "  [HUNTER-001] Pairing " << (success ? "succeeded" : "failed") << "\n";
    });

    // Set up callbacks for device 2
    discovery2.onDeviceFound([](const DeviceInfo& info) {
        std::cout << "  [BOUNTY-001] Found device: " << info.deviceId
                  << " (" << info.playerName << ")\n";
    });

    discovery2.onPairRequest([](const DeviceInfo& info) {
        std::cout << "  [BOUNTY-001] Pair request from: " << info.deviceId
                  << " (" << info.playerName << ")\n";
    });

    discovery2.onPairComplete([](bool success) {
        std::cout << "  [BOUNTY-001] Pairing " << (success ? "succeeded" : "failed") << "\n";
    });

    // Demonstrate scanning
    std::cout << "\n--- Test 1: Scanning ---\n";
    std::cout << "Device 1 starts scanning...\n";
    discovery1.startScan(3000);  // 3 second scan

    // Simulate update loop
    for (int i = 0; i < 35; i++) {
        uint32_t currentTimeMs = i * 100;  // 100ms per iteration
        discovery1.update(currentTimeMs);
        discovery2.update(currentTimeMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Scan complete. State: ";
    printPairingState(discovery1.getPairingState());
    std::cout << "\n";

    printDevices(discovery1.getDiscoveredDevices());

    // Demonstrate pairing (would require actual peer communication to work)
    std::cout << "\n--- Test 2: Pairing (conceptual) ---\n";
    std::cout << "Note: Actual pairing requires peer communication integration\n";
    std::cout << "      Discovery protocol defined in discovery.hpp\n";
    std::cout << "      Packet format: 39 bytes with device info\n";

    // Show pairing API
    std::cout << "\nPairing API usage:\n";
    std::cout << "  1. discovery.requestPair(\"BOUNTY-001\")  // Send pair request\n";
    std::cout << "  2. discovery.acceptPair(\"HUNTER-001\")   // Accept pair request\n";
    std::cout << "  3. discovery.unpair()                    // Disconnect\n";

    // Cleanup
    std::cout << "\nShutting down...\n";
    discovery1.shutdown();
    discovery2.shutdown();

    std::cout << "\n=== Demo Complete ===\n";
    std::cout << "\nIntegration points:\n";
    std::cout << "  - Add 'scan' command to cli-commands.hpp\n";
    std::cout << "  - Add 'pair' command to cli-commands.hpp\n";
    std::cout << "  - Register discovery packet handler with PeerCommsInterface\n";
    std::cout << "  - Use PktType::kPlayerInfoBroadcast for discovery packets\n";

    return 0;
}
#endif // NATIVE_BUILD
