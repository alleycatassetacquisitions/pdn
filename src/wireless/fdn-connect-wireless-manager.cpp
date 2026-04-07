#include "wireless/fdn-connect-wireless-manager.hpp"
#include "device/drivers/logger.hpp"

#define TAG "FDN_CWM"

struct FdnConnectPacket {
    int command;
    char playerId[PLAYER_ID_BUFFER_SIZE];
    uint8_t sequence[FDN_HACK_SEQUENCE_LENGTH];
    int buttonValue;
} __attribute__((packed));

FDNConnectWirelessManager::FDNConnectWirelessManager() {}

FDNConnectWirelessManager::~FDNConnectWirelessManager() {
    wirelessManager = nullptr;
}

void FDNConnectWirelessManager::initialize(WirelessManager* wm) {
    wirelessManager = wm;
}

void FDNConnectWirelessManager::setPeer(const uint8_t* macAddr) {
    memcpy(peerMac.data(), macAddr, 6);
    hasPeer = true;
}

void FDNConnectWirelessManager::clearPeer() {
    peerMac.fill(0);
    hasPeer = false;
}

int FDNConnectWirelessManager::sendHackSequence(const ButtonIdentifier* sequence) {
    if (!hasPeer) {
        LOG_E(TAG, "Cannot send hack sequence — no peer set");
        return -1;
    }

    FdnConnectPacket pkt{};
    pkt.command = SEND_HACK_SEQUENCE;
    for (int i = 0; i < FDN_HACK_SEQUENCE_LENGTH; i++) {
        pkt.sequence[i] = static_cast<uint8_t>(sequence[i]);
    }

    LOG_I(TAG, "Sending hack sequence to PDN");
    return wirelessManager->sendEspNowData(
        peerMac.data(),
        PktType::kFdnConnect,
        reinterpret_cast<const uint8_t*>(&pkt),
        sizeof(pkt));
}

void FDNConnectWirelessManager::setConnectCallback(
    std::function<void(const std::string& playerId, const uint8_t* senderMac)> callback) {
    connectCallback = std::move(callback);
}

void FDNConnectWirelessManager::setButtonPressCallback(
    std::function<void(ButtonIdentifier btn)> callback) {
    buttonPressCallback = std::move(callback);
}

void FDNConnectWirelessManager::clearCallbacks() {
    connectCallback = nullptr;
    buttonPressCallback = nullptr;
}

int FDNConnectWirelessManager::processPacket(const uint8_t* senderMac, const uint8_t* data, size_t dataLen) {
    if (dataLen != sizeof(FdnConnectPacket)) {
        LOG_E(TAG, "Unexpected packet length. Got %lu, expected %lu", dataLen, sizeof(FdnConnectPacket));
        return -1;
    }

    const FdnConnectPacket* pkt = reinterpret_cast<const FdnConnectPacket*>(data);

    if (pkt->command < 0 || pkt->command >= FDN_CONNECT_CMD_COUNT) {
        LOG_E(TAG, "Invalid command %d, dropping", pkt->command);
        return -1;
    }

    switch (pkt->command) {
        case PDN_CONNECT:
            LOG_I(TAG, "PDN connected, player ID: %s", pkt->playerId);
            if (connectCallback) {
                connectCallback(std::string(pkt->playerId), senderMac);
            }
            break;

        case BUTTON_PRESS: {
            ButtonIdentifier btn = static_cast<ButtonIdentifier>(pkt->buttonValue);
            LOG_I(TAG, "Button press received: %d", pkt->buttonValue);
            if (buttonPressCallback) {
                buttonPressCallback(btn);
            }
            break;
        }

        default:
            LOG_W(TAG, "Unhandled command %d", pkt->command);
            break;
    }

    return 1;
}
