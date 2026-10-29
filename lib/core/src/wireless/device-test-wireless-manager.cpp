#include "wireless/device-test-wireless-manager.hpp"

#include <cstring>

#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"

#define TAG "DEVICE_TEST_WM"

DeviceTestWirelessManager::DeviceTestWirelessManager() {}

DeviceTestWirelessManager::~DeviceTestWirelessManager() {
    wirelessManager = nullptr;
}

void DeviceTestWirelessManager::initialize(WirelessManager* wirelessManagerIn) {
    wirelessManager = wirelessManagerIn;
}

void DeviceTestWirelessManager::registerEspNowHandler() {
    if (wirelessManager == nullptr || espNowHandlerRegistered) {
        return;
    }
    wirelessManager->setEspNowPacketHandler(
        PktType::kDeviceTestCommand,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<DeviceTestWirelessManager*>(ctx)->processDeviceTestPacket(macAddress, data, dataLen);
        },
        this);
    espNowHandlerRegistered = true;
}

void DeviceTestWirelessManager::unregisterEspNowHandler() {
    if (wirelessManager == nullptr || !espNowHandlerRegistered) {
        return;
    }
    wirelessManager->clearEspNowPacketHandler(PktType::kDeviceTestCommand);
    espNowHandlerRegistered = false;
}

void DeviceTestWirelessManager::setPacketReceivedCallback(DeviceTestPacketCallback callback) {
    packetReceivedCallback = callback;
}

void DeviceTestWirelessManager::setPeerFilter(DeviceTestPeerFilter filter) {
    peerFilter = filter;
}

void DeviceTestWirelessManager::setSessionBeginCallback(std::function<void(const uint8_t* fromMac)> callback) {
    sessionBeginCallback = callback;
}

void DeviceTestWirelessManager::clearCallbacks() {
    packetReceivedCallback = nullptr;
    peerFilter = nullptr;
    sessionBeginCallback = nullptr;
}

int DeviceTestWirelessManager::sendCommand(const uint8_t* dstMac,
                                           DeviceTestCommand command,
                                           const uint8_t* payload,
                                           size_t payloadLen) {
    if (wirelessManager == nullptr || dstMac == nullptr) {
        return -1;
    }
    if (payloadLen > sizeof(DeviceTestPacket::payload)) {
        LOG_E(TAG, "Payload too large: %u", (unsigned)payloadLen);
        return -1;
    }

    DeviceTestPacket packet = {};
    packet.command = command;
    packet.seqId = nextSeqId++;
    if (nextSeqId == 0) {
        nextSeqId = 1;
    }
    if (payload != nullptr && payloadLen > 0) {
        memcpy(packet.payload, payload, payloadLen);
    }

    return wirelessManager->sendEspNowData(dstMac,
                                         PktType::kDeviceTestCommand,
                                         reinterpret_cast<const uint8_t*>(&packet),
                                         sizeof(packet));
}

int DeviceTestWirelessManager::sendBeginSession(const uint8_t* dstMac) {
    return sendCommand(dstMac, DeviceTestCommand::BEGIN_SESSION, nullptr, 0);
}

int DeviceTestWirelessManager::sendEndSession(const uint8_t* dstMac) {
    return sendCommand(dstMac, DeviceTestCommand::END_SESSION, nullptr, 0);
}

int DeviceTestWirelessManager::sendShowScreenPattern(const uint8_t* dstMac) {
    return sendCommand(dstMac, DeviceTestCommand::SHOW_SCREEN_PATTERN, nullptr, 0);
}

int DeviceTestWirelessManager::sendShowAllLeds(const uint8_t* dstMac) {
    return sendCommand(dstMac, DeviceTestCommand::SHOW_ALL_LEDS, nullptr, 0);
}

int DeviceTestWirelessManager::sendClearLeds(const uint8_t* dstMac) {
    return sendCommand(dstMac, DeviceTestCommand::CLEAR_LEDS, nullptr, 0);
}

int DeviceTestWirelessManager::sendTriggerHaptics(const uint8_t* dstMac) {
    return sendCommand(dstMac, DeviceTestCommand::TRIGGER_HAPTICS, nullptr, 0);
}

int DeviceTestWirelessManager::sendSessionInfo(const uint8_t* dstMac, const char* commitHash8) {
    if (commitHash8 == nullptr) {
        return -1;
    }
    uint8_t payload[16] = {};
    strncpy(reinterpret_cast<char*>(payload), commitHash8, 8);
    return sendCommand(dstMac, DeviceTestCommand::SESSION_INFO, payload, 8);
}

int DeviceTestWirelessManager::processDeviceTestPacket(const uint8_t* macAddress,
                                                     const uint8_t* data,
                                                     size_t dataLen) {
    if (dataLen != sizeof(DeviceTestPacket)) {
        LOG_E(TAG, "Unexpected packet len: got %u expected %u",
              (unsigned)dataLen,
              (unsigned)sizeof(DeviceTestPacket));
        return -1;
    }

    if (peerFilter && !peerFilter(macAddress)) {
        return -1;
    }

    const DeviceTestPacket* packet = reinterpret_cast<const DeviceTestPacket*>(data);
    const DeviceTestCommand command = packet->command;

    if (command == DeviceTestCommand::BEGIN_SESSION && sessionBeginCallback) {
        sessionBeginCallback(macAddress);
    }

    if (packetReceivedCallback) {
        packetReceivedCallback(macAddress, command, packet->payload, sizeof(packet->payload));
    }

    return 1;
}
