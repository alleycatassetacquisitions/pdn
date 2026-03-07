//
// Created by Elli Furedy on 1/24/2025.
//
#include "wireless/quickdraw-wireless-manager.hpp"
#include "device/drivers/peer-comms-interface.hpp"

struct QuickdrawPacket {
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    char playerId[5];  // 4 chars + null terminator
    bool isHunter;
    long playerDrawTime;
    int command;
} __attribute__((packed));

QuickdrawWirelessManager::QuickdrawWirelessManager() : broadcastTimer() {}

QuickdrawWirelessManager::~QuickdrawWirelessManager() {
    player = nullptr;
    wirelessManager = nullptr;
}

void QuickdrawWirelessManager::initialize(Player *player, WirelessManager* wirelessManager, long broadcastDelay) {
    this->player = player;
    this->broadcastDelay = broadcastDelay;
    this->wirelessManager = wirelessManager;
}

void QuickdrawWirelessManager::clearCallbacks() {
    packetReceivedCallback = nullptr;
}

void QuickdrawWirelessManager::setPacketReceivedCallback(const std::function<void(const QuickdrawCommand&)>& callback) {
    packetReceivedCallback = callback;
}

int QuickdrawWirelessManager::broadcastPacket(const uint8_t macAddress[6],
                                             QuickdrawCommand& command) {
    QuickdrawPacket qdPacket = QuickdrawPacket();

    qdPacket.command = command.command;
    qdPacket.playerDrawTime = command.playerDrawTime;

    memcpy(qdPacket.matchId, command.matchId, IdGenerator::UUID_BUFFER_SIZE);

    LOG_I("QWM", "Sending command %i to %s", command, MacToString(macAddress));
    LOG_I("QWM", "Match ID: %s", qdPacket.matchId);
    LOG_I("QWM", "Player Draw Time: %ld", qdPacket.playerDrawTime);

    return wirelessManager->sendEspNowData(
        macAddress,
        PktType::kQuickdrawCommand,
        reinterpret_cast<const uint8_t*>(&qdPacket),
        sizeof(qdPacket));
}


int QuickdrawWirelessManager::processQuickdrawCommand(const uint8_t *macAddress, const uint8_t *data,
    const size_t dataLen) {

    if(dataLen != sizeof(QuickdrawPacket)) {
        LOG_E("RPM", "Unexpected packet len for PlayerInfoPkt. Got %lu but expected %lu\n",
                      dataLen, sizeof(QuickdrawPacket));

        return -1;
    }

    const QuickdrawPacket* packet = reinterpret_cast<const QuickdrawPacket*>(data);

    QuickdrawCommand command(reinterpret_cast<const uint8_t*>(macAddress), packet->command,
                             packet->matchId, packet->playerId, packet->playerDrawTime, packet->isHunter);

    if(packetReceivedCallback) {
        packetReceivedCallback(command);
    }

    return 1;
}