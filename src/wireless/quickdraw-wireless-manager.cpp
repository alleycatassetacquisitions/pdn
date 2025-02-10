//
// Created by Elli Furedy on 1/24/2025.
//
#include "wireless/quickdraw-wireless-manager.hpp"

#include <Arduino.h>

#include "wireless/esp-now-comms.hpp"
#define UUID_LEN_TERMINATED 33
#define UUID_LEN 32


struct QuickdrawPacket {
    char id[UUID_LEN_TERMINATED];
    char matchId[UUID_LEN_TERMINATED];
    char opponentId[UUID_LEN_TERMINATED];
    int command;
    long resultTime;
    int ackCount;


} __attribute__((packed));

QuickdrawWirelessManager *QuickdrawWirelessManager::GetInstance() {
    static QuickdrawWirelessManager instance;
    return &instance;
}

QuickdrawWirelessManager::QuickdrawWirelessManager() : broadcastTimer() {}

void QuickdrawWirelessManager::initialize(Player *player, long broadcastDelay) {
    this->player = player;
    this->broadcastDelay = broadcastDelay;
}

void QuickdrawWirelessManager::clearCallbacks() {
    packetReceivedCallback = nullptr;
}

void QuickdrawWirelessManager::setPacketReceivedCallback(std::function<void (QuickdrawCommand)> callback) {
    packetReceivedCallback = callback;
}

int QuickdrawWirelessManager::broadcastPacket(string macAddress, int command, int drawTimeMs, int ackCount, string matchId, string opponentId) {
    // TODO swap this to a direct send to the mac address instead of broadcasting
    if(!broadcastTimer.isRunning() || broadcastTimer.expired()) {
        QuickdrawPacket qdPacket;
        strncpy(qdPacket.id, player->getUserID().c_str(), UUID_LEN);
        qdPacket.id[UUID_LEN] = '\0';
        strncpy(qdPacket.matchId, matchId.c_str(), UUID_LEN);
        qdPacket.matchId[UUID_LEN] = '\0';
        strncpy(qdPacket.opponentId, opponentId.c_str(), UUID_LEN);
        qdPacket.opponentId[UUID_LEN] = '\0';
        qdPacket.command = command;
        qdPacket.resultTime = drawTimeMs;
        qdPacket.ackCount = ackCount;


        ESP_LOGI("QWM", "Broadcasting command %i", command);

        int ret = EspNowManager::GetInstance()->SendData(
            ESP_NOW_BROADCAST_ADDR,
            PktType::kQuickdrawCommand,
            (uint8_t*)&qdPacket,
            sizeof(qdPacket));

        broadcastTimer.setTimer(broadcastDelay);

        return ret;
    }
    return -1;

}


int QuickdrawWirelessManager::processQuickdrawCommand(const uint8_t *macAddress, const uint8_t *data,
    const size_t dataLen) {

    if(dataLen != sizeof(QuickdrawPacket)) {
        ESP_LOGE("RPM", "Unexpected packet len for PlayerInfoPkt. Got %lu but expected %lu\n",
                      dataLen, sizeof(QuickdrawPacket));

        return -1;
    }

    QuickdrawPacket *packet = (QuickdrawPacket *)data;

    QuickdrawCommand command = QuickdrawCommand(
            MacToString(macAddress),
            packet->command,
            packet->resultTime,
            packet->ackCount,
            packet->matchId,
            packet->opponentId);


    logPacket(command);
    packetReceivedCallback(command);

    return 1;
}

void QuickdrawWirelessManager::clearPackets() {
    commandTracker.clear();
}

void QuickdrawWirelessManager::clearPacket(int command) {
    commandTracker.erase(command);
}


int QuickdrawWirelessManager::getPacketAckCount(int command) {
    if(commandTracker.find(command) == commandTracker.end()) {
        //No command found
        return 0;
    }

    return commandTracker[command].ackCount;
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

