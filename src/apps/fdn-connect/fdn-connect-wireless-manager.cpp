#include "apps/fdn-connect/fdn-connect-wireless-manager.hpp"
#include "device/drivers/logger.hpp"
#include <cstring>

FDNConnectWirelessManager::FDNConnectWirelessManager() {}

FDNConnectWirelessManager::~FDNConnectWirelessManager() {}

void FDNConnectWirelessManager::initialize(WirelessManager* wm) {
    this->wirelessManager = wm;
}

int FDNConnectWirelessManager::sendPdnConnect(const uint8_t macAddress[6], const char playerId[PLAYER_ID_BUFFER_SIZE]) {
    FdnConnectPacket pkt = {};
    pkt.command = PDN_CONNECT;
    strncpy(pkt.playerId, playerId, PLAYER_ID_BUFFER_SIZE - 1);
    pkt.playerId[PLAYER_ID_BUFFER_SIZE - 1] = '\0';
    return wirelessManager->sendEspNowData(macAddress, PktType::kFDNConnectCommand, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

int FDNConnectWirelessManager::sendButtonPress(const uint8_t macAddress[6], ButtonIdentifier button) {
    FdnConnectPacket pkt = {};
    pkt.command     = BUTTON_PRESS;
    pkt.buttonValue = static_cast<uint8_t>(button);
    return wirelessManager->sendEspNowData(macAddress, PktType::kFDNConnectCommand, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

void FDNConnectWirelessManager::setHackSequenceCallback(const std::function<void(const uint8_t sequence[FDN_HACK_SEQUENCE_LENGTH])>& callback) {
    hackSequenceCallback = callback;
}

void FDNConnectWirelessManager::clearCallbacks() {
    hackSequenceCallback = nullptr;
}

int FDNConnectWirelessManager::processFDNConnectCommand(const uint8_t* src, const uint8_t* data, size_t len) {
    if (len != sizeof(FdnConnectPacket)) {
        LOG_E("FDNConnectWirelessManager", "Unexpected packet length. Got %lu, expected %lu", len, sizeof(FdnConnectPacket));
        return -1;
    }

    const FdnConnectPacket* pkt = reinterpret_cast<const FdnConnectPacket*>(data);

    switch (static_cast<FdnConnectCmd>(pkt->command)) {
        case SEND_HACK_SEQUENCE:
            if (hackSequenceCallback) {
                hackSequenceCallback(pkt->sequence);
            }
            break;
        default:
            LOG_W("FDNConnectWirelessManager", "Unhandled FDN command: %d", pkt->command);
            break;
    }

    return 0;
}
