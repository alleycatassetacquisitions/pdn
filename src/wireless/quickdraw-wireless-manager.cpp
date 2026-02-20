//
// Created by Elli Furedy on 1/24/2025.
//
#include "wireless/quickdraw-wireless-manager.hpp"
#include "device/drivers/peer-comms-interface.hpp"

struct QuickdrawPacket {
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    char hunterId[5];  // 4 chars + null terminator
    char bountyId[5];  // 4 chars + null terminator
    long hunterDrawTime;
    long bountyDrawTime;
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

int QuickdrawWirelessManager::broadcastPacket(const uint8_t* macAddress,
                                             int command,
                                             const Match& match) {
    if (!macAddress) {
        return -1;
    }

    QuickdrawPacket qdPacket;
    qdPacket.command = command;

    memcpy(qdPacket.matchId, match.getMatchId(), IdGenerator::UUID_STRING_LENGTH);
    qdPacket.matchId[IdGenerator::UUID_STRING_LENGTH] = '\0';

    memcpy(qdPacket.hunterId, match.getHunterId(), 4);
    qdPacket.hunterId[4] = '\0';

    memcpy(qdPacket.bountyId, match.getBountyId(), 4);
    qdPacket.bountyId[4] = '\0';

    qdPacket.hunterDrawTime = match.getHunterDrawTime();
    qdPacket.bountyDrawTime = match.getBountyDrawTime();

    LOG_I("QWM", "Sending command %i to %s", command, MacToString(macAddress));
    LOG_I("QWM", "Match ID: %s", qdPacket.matchId);
    LOG_I("QWM", "Hunter Draw Time: %ld, Bounty Draw Time: %ld",
          qdPacket.hunterDrawTime, qdPacket.bountyDrawTime);

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

    QuickdrawCommand command(macAddress, packet->command,
                             packet->matchId, packet->hunterId, packet->bountyId,
                             packet->hunterDrawTime, packet->bountyDrawTime);

    if(packetReceivedCallback) {
        packetReceivedCallback(command);
    }

    return 1;
}