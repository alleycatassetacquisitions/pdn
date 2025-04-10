//
// Created by Elli Furedy on 1/24/2025.
//
#include "wireless/quickdraw-wireless-manager.hpp"

#include <Arduino.h>

#include "wireless/esp-now-comms.hpp"
#include "id-generator.hpp"
#include <WiFi.h>


struct QuickdrawPacket {
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    char hunterId[5];  // 4 chars + null terminator
    char bountyId[5];  // 4 chars + null terminator
    long hunterDrawTime;
    long bountyDrawTime;
    int command;
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

int QuickdrawWirelessManager::broadcastPacket(const string& macAddress, 
                                             int command, 
                                             Match match) {
    // Ensure WiFi is in STA mode before sending
    if (WiFi.getMode() != WIFI_STA) {
        ESP_LOGW("QWM", "WiFi not in STA mode, setting it now");
        WiFi.mode(WIFI_STA);
    }
    
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

    ESP_LOGI("QWM", "Broadcasting command %i", command);
    ESP_LOGI("QWM", "Match ID: %s", qdPacket.matchId);
    ESP_LOGI("QWM", "Hunter ID: %s", qdPacket.hunterId);
    ESP_LOGI("QWM", "Bounty ID: %s", qdPacket.bountyId);
    ESP_LOGI("QWM", "Hunter Draw Time: %ld", qdPacket.hunterDrawTime);
    ESP_LOGI("QWM", "Bounty Draw Time: %ld", qdPacket.bountyDrawTime);

    int ret = EspNowManager::GetInstance()->SendData(
        ESP_NOW_BROADCAST_ADDR,
        PktType::kQuickdrawCommand,
        (uint8_t*)&qdPacket,
        sizeof(qdPacket));

    return ret;
}


int QuickdrawWirelessManager::processQuickdrawCommand(const uint8_t *macAddress, const uint8_t *data,
    const size_t dataLen) {

    if(dataLen != sizeof(QuickdrawPacket)) {
        ESP_LOGE("RPM", "Unexpected packet len for PlayerInfoPkt. Got %lu but expected %lu\n",
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

