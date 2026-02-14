#include "network/discovery.hpp"
#include <cstring>
#include <algorithm>
#include "device/drivers/logger.hpp"

// Discovery system implementation

DeviceDiscovery::DeviceDiscovery() :
    myId_(""),
    myName_(""),
    myAllegiance_(0),
    myLevel_(1),
    myAvailable_(true),
    state_(PairingState::IDLE),
    initialized_(false),
    scanning_(false),
    scanStartMs_(0),
    scanDurationMs_(0),
    lastBroadcastMs_(0),
    hasPairedDevice_(false),
    pendingPairRequester_(""),
    onFound_(nullptr),
    onPairReq_(nullptr),
    onPairDone_(nullptr)
{
}

DeviceDiscovery::~DeviceDiscovery() {
    shutdown();
}

bool DeviceDiscovery::init(const std::string& myDeviceId, const std::string& myPlayerName) {
    if (initialized_) {
        return true;
    }

    myId_ = myDeviceId;
    myName_ = myPlayerName;
    myAllegiance_ = 0;  // Default allegiance
    myLevel_ = 1;       // Default level
    myAvailable_ = true;

    initialized_ = true;
    state_ = PairingState::IDLE;

    LOG_I("DISC", "Discovery initialized for device %s (%s)", myId_.c_str(), myName_.c_str());

    return true;
}

void DeviceDiscovery::shutdown() {
    if (!initialized_) {
        return;
    }

    stopScan();
    unpair();

    discoveredDevices_.clear();
    initialized_ = false;

    LOG_I("DISC", "Discovery shutdown");
}

void DeviceDiscovery::startScan(uint16_t durationMs) {
    if (!initialized_) {
        LOG_W("DISC", "Cannot start scan - not initialized");
        return;
    }

    if (scanning_) {
        LOG_W("DISC", "Already scanning");
        return;
    }

    scanning_ = true;
    scanStartMs_ = 0;  // Will be set on first update() call
    scanDurationMs_ = durationMs;
    lastBroadcastMs_ = 0;

    // Clear previous discoveries
    discoveredDevices_.clear();

    state_ = PairingState::SCANNING;

    LOG_I("DISC", "Started scanning for %d ms", durationMs);
}

void DeviceDiscovery::stopScan() {
    if (!scanning_) {
        return;
    }

    scanning_ = false;

    if (state_ == PairingState::SCANNING) {
        state_ = PairingState::IDLE;
    }

    LOG_I("DISC", "Stopped scanning - found %zu devices", discoveredDevices_.size());
}

bool DeviceDiscovery::isScanning() const {
    return scanning_;
}

std::vector<DeviceInfo> DeviceDiscovery::getDiscoveredDevices() const {
    std::vector<DeviceInfo> devices;
    for (const auto& pair : discoveredDevices_) {
        devices.push_back(pair.second);
    }
    return devices;
}

bool DeviceDiscovery::requestPair(const std::string& targetDeviceId) {
    if (!initialized_) {
        LOG_W("DISC", "Cannot request pair - not initialized");
        return false;
    }

    if (state_ == PairingState::PAIRED) {
        LOG_W("DISC", "Already paired - unpair first");
        return false;
    }

    if (state_ == PairingState::PAIR_REQUEST_SENT) {
        LOG_W("DISC", "Pair request already pending");
        return false;
    }

    // Find target device
    DeviceInfo* target = findDeviceById(targetDeviceId);
    if (!target) {
        LOG_W("DISC", "Target device not found: %s", targetDeviceId.c_str());
        return false;
    }

    if (!target->available) {
        LOG_W("DISC", "Target device not available: %s", targetDeviceId.c_str());
        return false;
    }

    // Send pair request
    sendDiscoveryPacket(target->macAddress, DiscoveryPacketType::PAIR_REQUEST);

    state_ = PairingState::PAIR_REQUEST_SENT;
    pendingPairRequester_ = targetDeviceId;

    LOG_I("DISC", "Sent pair request to %s", targetDeviceId.c_str());

    return true;
}

bool DeviceDiscovery::acceptPair(const std::string& requesterDeviceId) {
    if (!initialized_) {
        return false;
    }

    if (state_ != PairingState::PAIR_REQUEST_RECEIVED) {
        LOG_W("DISC", "No pending pair request");
        return false;
    }

    if (pendingPairRequester_ != requesterDeviceId) {
        LOG_W("DISC", "No pair request from %s", requesterDeviceId.c_str());
        return false;
    }

    // Find requester device
    DeviceInfo* requester = findDeviceById(requesterDeviceId);
    if (!requester) {
        LOG_W("DISC", "Requester device not found: %s", requesterDeviceId.c_str());
        return false;
    }

    // Send accept
    sendDiscoveryPacket(requester->macAddress, DiscoveryPacketType::PAIR_ACCEPT);

    // Complete pairing
    pairedDevice_ = *requester;
    hasPairedDevice_ = true;
    state_ = PairingState::PAIRED;
    pendingPairRequester_ = "";

    LOG_I("DISC", "Accepted pair from %s", requesterDeviceId.c_str());

    // Notify callback
    if (onPairDone_) {
        onPairDone_(true);
    }

    return true;
}

bool DeviceDiscovery::rejectPair(const std::string& requesterDeviceId) {
    if (!initialized_) {
        return false;
    }

    if (state_ != PairingState::PAIR_REQUEST_RECEIVED) {
        LOG_W("DISC", "No pending pair request");
        return false;
    }

    if (pendingPairRequester_ != requesterDeviceId) {
        LOG_W("DISC", "No pair request from %s", requesterDeviceId.c_str());
        return false;
    }

    // Find requester device
    DeviceInfo* requester = findDeviceById(requesterDeviceId);
    if (requester) {
        sendDiscoveryPacket(requester->macAddress, DiscoveryPacketType::PAIR_REJECT);
    }

    state_ = PairingState::IDLE;
    pendingPairRequester_ = "";

    LOG_I("DISC", "Rejected pair from %s", requesterDeviceId.c_str());

    return true;
}

void DeviceDiscovery::unpair() {
    if (!hasPairedDevice_) {
        return;
    }

    hasPairedDevice_ = false;
    state_ = PairingState::IDLE;

    LOG_I("DISC", "Unpaired from %s", pairedDevice_.deviceId.c_str());
}

PairingState DeviceDiscovery::getPairingState() const {
    return state_;
}

const DeviceInfo* DeviceDiscovery::getPairedDevice() const {
    if (!hasPairedDevice_) {
        return nullptr;
    }
    return &pairedDevice_;
}

void DeviceDiscovery::onDeviceFound(DeviceFoundCallback cb) {
    onFound_ = cb;
}

void DeviceDiscovery::onPairRequest(PairRequestCallback cb) {
    onPairReq_ = cb;
}

void DeviceDiscovery::onPairComplete(PairCompleteCallback cb) {
    onPairDone_ = cb;
}

void DeviceDiscovery::update(uint32_t currentTimeMs) {
    if (!initialized_) {
        return;
    }

    // Initialize scan start time on first update
    if (scanning_ && scanStartMs_ == 0) {
        scanStartMs_ = currentTimeMs;
    }

    // Check for scan timeout
    if (scanning_ && hasScanTimedOut(currentTimeMs)) {
        stopScan();
    }

    // Check for pair timeout
    if (state_ == PairingState::PAIR_REQUEST_SENT && hasPairTimedOut(currentTimeMs)) {
        LOG_W("DISC", "Pair request timed out");
        state_ = PairingState::IDLE;
        pendingPairRequester_ = "";

        if (onPairDone_) {
            onPairDone_(false);
        }
    }

    // Broadcast presence if scanning
    if (scanning_) {
        broadcastPresence(currentTimeMs);
    }

    // Prune stale devices
    pruneStaleDevices(currentTimeMs);
}

void DeviceDiscovery::handleDiscoveryPacket(const uint8_t* srcMac, const uint8_t* data, size_t len) {
    if (!initialized_) {
        return;
    }

    if (len < sizeof(DiscoveryPacket)) {
        LOG_W("DISC", "Received packet too small: %zu bytes", len);
        return;
    }

    const DiscoveryPacket* packet = reinterpret_cast<const DiscoveryPacket*>(data);

    // Check protocol version
    if (packet->version != 0x01) {
        LOG_W("DISC", "Unsupported protocol version: 0x%02X", packet->version);
        return;
    }

    // Dispatch by packet type
    switch (static_cast<DiscoveryPacketType>(packet->type)) {
        case DiscoveryPacketType::PRESENCE:
            handlePresence(srcMac, packet);
            break;

        case DiscoveryPacketType::PAIR_REQUEST:
            handlePairRequest(srcMac, packet);
            break;

        case DiscoveryPacketType::PAIR_ACCEPT:
            handlePairAccept(srcMac, packet);
            break;

        case DiscoveryPacketType::PAIR_REJECT:
            handlePairReject(srcMac, packet);
            break;

        default:
            LOG_W("DISC", "Unknown packet type: 0x%02X", packet->type);
            break;
    }
}

// === PRIVATE METHODS === //

void DeviceDiscovery::broadcastPresence(uint32_t currentTimeMs) {
    // Broadcast at regular intervals
    if (currentTimeMs - lastBroadcastMs_ < BROADCAST_INTERVAL_MS) {
        return;
    }

    lastBroadcastMs_ = currentTimeMs;

    // Send broadcast (MAC address FF:FF:FF:FF:FF:FF)
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    sendDiscoveryPacket(broadcastMac, DiscoveryPacketType::PRESENCE);
}

void DeviceDiscovery::handlePresence(const uint8_t* srcMac, const DiscoveryPacket* packet) {
    // Extract device info
    DeviceInfo info;

    // Copy device ID (ensure null termination)
    std::memcpy(&info.deviceId[0], packet->deviceId, sizeof(packet->deviceId));
    info.deviceId[sizeof(packet->deviceId)] = '\0';
    info.deviceId = std::string(packet->deviceId,
        std::find(packet->deviceId, packet->deviceId + sizeof(packet->deviceId), '\0'));

    // Copy player name (ensure null termination)
    info.playerName = std::string(packet->playerName,
        std::find(packet->playerName, packet->playerName + sizeof(packet->playerName), '\0'));

    info.allegiance = packet->allegiance;
    info.level = packet->level;
    info.available = (packet->available != 0);
    info.lastSeenMs = 0;  // Will be updated by caller
    info.signalStrength = -50;  // Mock RSSI for now

    std::memcpy(info.macAddress, srcMac, 6);
    info.ipAddress = 0;  // Unused in this implementation
    info.port = 0;       // Unused in this implementation

    // Ignore our own broadcasts
    if (info.deviceId == myId_) {
        return;
    }

    // Check if we already know this device
    bool isNew = (discoveredDevices_.find(info.deviceId) == discoveredDevices_.end());

    // Update or add device
    discoveredDevices_[info.deviceId] = info;

    // Notify callback if new device
    if (isNew && onFound_) {
        LOG_I("DISC", "Discovered device: %s (%s)",
              info.deviceId.c_str(), info.playerName.c_str());
        onFound_(info);
    }
}

void DeviceDiscovery::handlePairRequest(const uint8_t* srcMac, const DiscoveryPacket* packet) {
    // Extract device ID
    std::string requesterDeviceId(packet->deviceId,
        std::find(packet->deviceId, packet->deviceId + sizeof(packet->deviceId), '\0'));

    // Ignore if we're not idle or scanning
    if (state_ != PairingState::IDLE && state_ != PairingState::SCANNING) {
        LOG_W("DISC", "Ignoring pair request from %s - wrong state", requesterDeviceId.c_str());

        // Send reject
        uint8_t dstMac[6];
        std::memcpy(dstMac, srcMac, 6);
        sendDiscoveryPacket(dstMac, DiscoveryPacketType::PAIR_REJECT);
        return;
    }

    // Find requester in discovered devices
    DeviceInfo* requester = findDeviceByMac(srcMac);
    if (!requester) {
        LOG_W("DISC", "Pair request from unknown device");
        return;
    }

    // Update state
    state_ = PairingState::PAIR_REQUEST_RECEIVED;
    pendingPairRequester_ = requesterDeviceId;

    LOG_I("DISC", "Received pair request from %s", requesterDeviceId.c_str());

    // Notify callback
    if (onPairReq_) {
        onPairReq_(*requester);
    }
}

void DeviceDiscovery::handlePairAccept(const uint8_t* srcMac, const DiscoveryPacket* packet) {
    // Check if we're waiting for a response
    if (state_ != PairingState::PAIR_REQUEST_SENT) {
        LOG_W("DISC", "Unexpected pair accept - not waiting for response");
        return;
    }

    // Find device
    DeviceInfo* device = findDeviceByMac(srcMac);
    if (!device) {
        LOG_W("DISC", "Pair accept from unknown device");
        return;
    }

    // Complete pairing
    pairedDevice_ = *device;
    hasPairedDevice_ = true;
    state_ = PairingState::PAIRED;
    pendingPairRequester_ = "";

    LOG_I("DISC", "Pair accepted by %s", device->deviceId.c_str());

    // Notify callback
    if (onPairDone_) {
        onPairDone_(true);
    }
}

void DeviceDiscovery::handlePairReject(const uint8_t* srcMac, const DiscoveryPacket* packet) {
    // Check if we're waiting for a response
    if (state_ != PairingState::PAIR_REQUEST_SENT) {
        return;
    }

    // Find device
    DeviceInfo* device = findDeviceByMac(srcMac);
    if (!device) {
        return;
    }

    LOG_I("DISC", "Pair rejected by %s", device->deviceId.c_str());

    // Reset state
    state_ = PairingState::IDLE;
    pendingPairRequester_ = "";

    // Notify callback
    if (onPairDone_) {
        onPairDone_(false);
    }
}

void DeviceDiscovery::pruneStaleDevices(uint32_t currentTimeMs) {
    // Remove devices not seen in DEVICE_STALE_MS
    auto it = discoveredDevices_.begin();
    while (it != discoveredDevices_.end()) {
        if (currentTimeMs - it->second.lastSeenMs > DEVICE_STALE_MS) {
            LOG_D("DISC", "Pruning stale device: %s", it->second.deviceId.c_str());
            it = discoveredDevices_.erase(it);
        } else {
            ++it;
        }
    }
}

void DeviceDiscovery::sendDiscoveryPacket(const uint8_t* dstMac, DiscoveryPacketType type) {
    // Build packet
    DiscoveryPacket packet = buildPacket(type);

    // NOTE: Actual sending would need to integrate with the peer comms system
    // This is a placeholder - the real implementation would call:
    // peerComms->sendData(dstMac, PktType::kPlayerInfoBroadcast,
    //                     (uint8_t*)&packet, sizeof(packet));

    LOG_D("DISC", "Would send packet type 0x%02X to %s",
          static_cast<uint8_t>(type), macToString(dstMac).c_str());
}

DiscoveryPacket DeviceDiscovery::buildPacket(DiscoveryPacketType type) const {
    DiscoveryPacket packet;
    std::memset(&packet, 0, sizeof(packet));

    packet.type = static_cast<uint8_t>(type);
    packet.version = 0x01;

    // Copy device ID (null-padded)
    std::memset(packet.deviceId, 0, sizeof(packet.deviceId));
    std::strncpy(packet.deviceId, myId_.c_str(), sizeof(packet.deviceId) - 1);

    // Copy player name (null-padded)
    std::memset(packet.playerName, 0, sizeof(packet.playerName));
    std::strncpy(packet.playerName, myName_.c_str(), sizeof(packet.playerName) - 1);

    packet.allegiance = myAllegiance_;
    packet.level = myLevel_;
    packet.available = myAvailable_ ? 1 : 0;

    packet.reserved[0] = 0;
    packet.reserved[1] = 0;

    return packet;
}

DeviceInfo* DeviceDiscovery::findDeviceByMac(const uint8_t* mac) {
    for (auto& pair : discoveredDevices_) {
        if (macEquals(pair.second.macAddress, mac)) {
            return &pair.second;
        }
    }
    return nullptr;
}

DeviceInfo* DeviceDiscovery::findDeviceById(const std::string& deviceId) {
    auto it = discoveredDevices_.find(deviceId);
    if (it != discoveredDevices_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string DeviceDiscovery::macToString(const uint8_t* mac) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}

bool DeviceDiscovery::macEquals(const uint8_t* a, const uint8_t* b) {
    return std::memcmp(a, b, 6) == 0;
}

bool DeviceDiscovery::hasScanTimedOut(uint32_t currentTimeMs) const {
    if (!scanning_ || scanStartMs_ == 0) {
        return false;
    }
    return (currentTimeMs - scanStartMs_) >= scanDurationMs_;
}

bool DeviceDiscovery::hasPairTimedOut(uint32_t currentTimeMs) const {
    // For now, we don't track pair request start time
    // This would need to be added if strict timeout enforcement is needed
    return false;
}
