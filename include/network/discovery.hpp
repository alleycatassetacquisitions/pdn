#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include "wireless/peer-comms-types.hpp"

/**
 * Device information for discovery.
 */
struct DeviceInfo {
    std::string deviceId;       // unique device identifier
    std::string playerName;     // player's display name
    uint8_t allegiance;         // faction: 0=NONE, 1=ENDLINE, 2=PRESTIGE, 3=RESISTANCE
    uint8_t level;              // player level/rank
    bool available;             // is the device available for pairing?
    uint32_t lastSeenMs;        // when we last heard from this device
    int8_t signalStrength;      // RSSI or equivalent (-100 to 0 dBm)

    // Network addressing (platform-specific)
    uint8_t macAddress[6];      // ESP-NOW MAC address or unique ID
    uint32_t ipAddress;         // IP for CLI mode (unused on ESP32)
    uint16_t port;              // Port for CLI mode (unused on ESP32)
};

/**
 * Pairing state machine.
 */
enum class PairingState : uint8_t {
    IDLE,                       // Not scanning, not paired
    SCANNING,                   // Actively scanning for devices
    PAIR_REQUEST_SENT,          // Sent pair request, waiting for response
    PAIR_REQUEST_RECEIVED,      // Received pair request from another device
    PAIRED,                     // Successfully paired with another device
    DISCONNECTING               // Tearing down pairing
};

/**
 * Discovery packet types (sent as payload within PktType::kPlayerInfoBroadcast).
 */
enum class DiscoveryPacketType : uint8_t {
    PRESENCE = 0x01,            // Broadcast presence
    PAIR_REQUEST = 0x02,        // Request pairing
    PAIR_ACCEPT = 0x03,         // Accept pairing
    PAIR_REJECT = 0x04          // Reject pairing
};

/**
 * Discovery packet format (39 bytes).
 *
 * Sent as the payload of PktType::kPlayerInfoBroadcast packets.
 * This allows discovery to work alongside existing game packets.
 */
struct DiscoveryPacket {
    uint8_t type;               // DiscoveryPacketType
    uint8_t version;            // Protocol version (0x01)
    char deviceId[16];          // Device ID (null-padded)
    char playerName[16];        // Player name (null-padded)
    uint8_t allegiance;         // Allegiance value
    uint8_t level;              // Player level
    uint8_t available;          // Available flag (0/1)
    uint8_t reserved[2];        // Reserved for future use
} __attribute__((packed));

static_assert(sizeof(DiscoveryPacket) == 39, "DiscoveryPacket must be 39 bytes");

/**
 * Device discovery and pairing manager.
 *
 * Provides device discovery and pairing services on top of the existing
 * peer communication layer. Works with both ESP-NOW (hardware) and
 * NativePeerBroker (CLI simulator).
 */
class DeviceDiscovery {
public:
    // Callbacks
    using DeviceFoundCallback = std::function<void(const DeviceInfo&)>;
    using PairRequestCallback = std::function<void(const DeviceInfo&)>;
    using PairCompleteCallback = std::function<void(bool success)>;

    DeviceDiscovery();
    ~DeviceDiscovery();

    // === INITIALIZATION === //

    /**
     * Initialize discovery system.
     * @param myDeviceId This device's unique identifier
     * @param myPlayerName This device's player name
     * @return true on success
     */
    bool init(const std::string& myDeviceId, const std::string& myPlayerName);

    /**
     * Shutdown discovery system.
     */
    void shutdown();

    // === SCANNING === //

    /**
     * Start scanning for nearby devices.
     * @param durationMs How long to scan (default 5000ms)
     */
    void startScan(uint16_t durationMs = 5000);

    /**
     * Stop scanning early.
     */
    void stopScan();

    /**
     * Check if currently scanning.
     */
    bool isScanning() const;

    /**
     * Get list of discovered devices.
     */
    std::vector<DeviceInfo> getDiscoveredDevices() const;

    // === PAIRING === //

    /**
     * Request pairing with a target device.
     * @param targetDeviceId Device ID to pair with
     * @return true if request was sent
     */
    bool requestPair(const std::string& targetDeviceId);

    /**
     * Accept a pairing request.
     * @param requesterDeviceId Device ID that requested pairing
     * @return true if accepted
     */
    bool acceptPair(const std::string& requesterDeviceId);

    /**
     * Reject a pairing request.
     * @param requesterDeviceId Device ID that requested pairing
     * @return true if rejected
     */
    bool rejectPair(const std::string& requesterDeviceId);

    /**
     * Unpair from current device.
     */
    void unpair();

    /**
     * Get current pairing state.
     */
    PairingState getPairingState() const;

    /**
     * Get currently paired device (or nullptr if not paired).
     */
    const DeviceInfo* getPairedDevice() const;

    // === CALLBACKS === //

    /**
     * Register callback for when a device is discovered.
     */
    void onDeviceFound(DeviceFoundCallback cb);

    /**
     * Register callback for when a pair request is received.
     */
    void onPairRequest(PairRequestCallback cb);

    /**
     * Register callback for when pairing completes (success or failure).
     */
    void onPairComplete(PairCompleteCallback cb);

    // === UPDATE === //

    /**
     * Update discovery system - call every tick.
     * @param currentTimeMs Current time in milliseconds
     */
    void update(uint32_t currentTimeMs);

    // === PACKET HANDLING === //

    /**
     * Handle incoming discovery packet.
     * This should be called by the packet handler registered with the peer comms system.
     */
    void handleDiscoveryPacket(const uint8_t* srcMac, const uint8_t* data, size_t len);

private:
    // My device info
    std::string myId_;
    std::string myName_;
    uint8_t myAllegiance_;
    uint8_t myLevel_;
    bool myAvailable_;

    // State
    PairingState state_;
    bool initialized_;

    // Scanning
    bool scanning_;
    uint32_t scanStartMs_;
    uint32_t scanDurationMs_;
    uint32_t lastBroadcastMs_;

    // Discovered devices (keyed by device ID)
    std::map<std::string, DeviceInfo> discoveredDevices_;

    // Paired device
    DeviceInfo pairedDevice_;
    bool hasPairedDevice_;

    // Pending pair request
    std::string pendingPairRequester_;

    // Callbacks
    DeviceFoundCallback onFound_;
    PairRequestCallback onPairReq_;
    PairCompleteCallback onPairDone_;

    // Timeouts
    static constexpr uint32_t BROADCAST_INTERVAL_MS = 2000;     // Broadcast every 2 seconds
    static constexpr uint32_t DEVICE_STALE_MS = 10000;          // Remove devices not seen in 10s
    static constexpr uint32_t PAIR_TIMEOUT_MS = 15000;          // Pair request timeout

    // Helper methods

    /**
     * Broadcast our presence.
     */
    void broadcastPresence(uint32_t currentTimeMs);

    /**
     * Process incoming presence announcement.
     */
    void handlePresence(const uint8_t* srcMac, const DiscoveryPacket* packet);

    /**
     * Process incoming pair request.
     */
    void handlePairRequest(const uint8_t* srcMac, const DiscoveryPacket* packet);

    /**
     * Process incoming pair accept.
     */
    void handlePairAccept(const uint8_t* srcMac, const DiscoveryPacket* packet);

    /**
     * Process incoming pair reject.
     */
    void handlePairReject(const uint8_t* srcMac, const DiscoveryPacket* packet);

    /**
     * Remove stale devices (not seen in DEVICE_STALE_MS).
     */
    void pruneStaleDevices(uint32_t currentTimeMs);

    /**
     * Send a discovery packet.
     */
    void sendDiscoveryPacket(const uint8_t* dstMac, DiscoveryPacketType type);

    /**
     * Build a discovery packet with our current info.
     */
    DiscoveryPacket buildPacket(DiscoveryPacketType type) const;

    /**
     * Find device info by MAC address.
     */
    DeviceInfo* findDeviceByMac(const uint8_t* mac);

    /**
     * Find device info by device ID.
     */
    DeviceInfo* findDeviceById(const std::string& deviceId);

    /**
     * Convert MAC address to string for logging.
     */
    static std::string macToString(const uint8_t* mac);

    /**
     * Compare two MAC addresses.
     */
    static bool macEquals(const uint8_t* a, const uint8_t* b);

    /**
     * Check if scan has timed out.
     */
    bool hasScanTimedOut(uint32_t currentTimeMs) const;

    /**
     * Check if pair request has timed out.
     */
    bool hasPairTimedOut(uint32_t currentTimeMs) const;
};
