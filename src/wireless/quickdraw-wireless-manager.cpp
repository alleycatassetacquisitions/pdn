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

void QuickdrawWirelessManager::setPacketReceivedCallback(const std::function<void (QuickdrawCommand)>& callback) {
    packetReceivedCallback = callback;
}

int QuickdrawWirelessManager::broadcastPacket(const std::string& macAddress, 
                                             int command, 
                                             Match match) {
    
    QuickdrawPacket qdPacket;
    
    // Set command
    qdPacket.command = command;
    
    // Copy the match ID (UUID)
    strncpy(qdPacket.matchId, match.getMatchId().c_str(), IdGenerator::UUID_STRING_LENGTH);
    qdPacket.matchId[IdGenerator::UUID_STRING_LENGTH] = '\0';  // Ensure null-termination
    
    // Copy the hunter ID (4 chars)
    strncpy(qdPacket.hunterId, match.getHunterId().c_str(), 4);
    qdPacket.hunterId[4] = '\0';  // Ensure null-termination
    
    // Copy the bounty ID (4 chars)
    strncpy(qdPacket.bountyId, match.getBountyId().c_str(), 4);
    qdPacket.bountyId[4] = '\0';  // Ensure null-termination
    
    // Set draw times
    qdPacket.hunterDrawTime = match.getHunterDrawTime();
    qdPacket.bountyDrawTime = match.getBountyDrawTime();

    LOG_I("QWM", "Sending command %i to %s", command, macAddress.c_str());
    LOG_I("QWM", "Match ID: %s", qdPacket.matchId);
    LOG_I("QWM", "Hunter ID: %s", qdPacket.hunterId);
    LOG_I("QWM", "Bounty ID: %s", qdPacket.bountyId);
    LOG_I("QWM", "Hunter Draw Time: %ld", qdPacket.hunterDrawTime);
    LOG_I("QWM", "Bounty Draw Time: %ld", qdPacket.bountyDrawTime);

    uint8_t dstMac[6];
    const uint8_t* targetMac;

    int ret;

    if (!macAddress.empty() && StringToMac(macAddress.c_str(), dstMac)) {
        targetMac = dstMac;

        ret = wirelessManager->sendEspNowData(
            targetMac,
            PktType::kQuickdrawCommand,
            (uint8_t*)&qdPacket,
            sizeof(qdPacket));
    } else {
        ret = -1;
    }

    return ret;
}


int QuickdrawWirelessManager::processQuickdrawCommand(const uint8_t *macAddress, const uint8_t *data,
    const size_t dataLen) {

    if(dataLen != sizeof(QuickdrawPacket)) {
        LOG_E("RPM", "Unexpected packet len for PlayerInfoPkt. Got %lu but expected %lu\n",
                      dataLen, sizeof(QuickdrawPacket));

        return -1;
    }

    QuickdrawPacket *packet = (QuickdrawPacket *)data;

    Match match = Match(packet->matchId, packet->hunterId, packet->bountyId);
    match.setHunterDrawTime(packet->hunterDrawTime);
    match.setBountyDrawTime(packet->bountyDrawTime);

    QuickdrawCommand command = QuickdrawCommand(
            MacToString(macAddress),
            packet->command,
            match);


    logPacket(command);
    if(packetReceivedCallback) {
        packetReceivedCallback(command);
    }

    return 1;
}

void QuickdrawWirelessManager::clearPackets() {
    commandTracker.clear();
}

void QuickdrawWirelessManager::clearPacket(int command) {
    commandTracker.erase(command);
}


int QuickdrawWirelessManager::getPacketAckCount(int command) {
    return 0;
}


void QuickdrawWirelessManager::logPacket(QuickdrawCommand packet) {
    // if(commandTracker.find(packet.command) == commandTracker.end()) {
        //No command found, place it in the tracker.
        commandTracker[packet.command] = packet;
    // } else {
    //     if(std::memcmp(commandTracker[packet.command].wifiMacAddr, packet.wifiMacAddr, 6) == 0) {
    //         //command came from the same user as the last one
    //         commandTracker[packet.command]
    //     }
    // }
}

