//
// Created by Elli Furedy on 1/24/2025.
//
#include "wireless/quickdraw-wireless-manager.hpp"
#include "device/drivers/peer-comms-interface.hpp"


QuickdrawWirelessManager::QuickdrawWirelessManager() : broadcastTimer() {}

QuickdrawWirelessManager::~QuickdrawWirelessManager() {
    player = nullptr;
    wirelessManager = nullptr;
    delete resender_;
}

void QuickdrawWirelessManager::initialize(Player *player, WirelessManager* wirelessManager, long broadcastDelay) {
    this->player = player;
    this->broadcastDelay = broadcastDelay;
    this->wirelessManager = wirelessManager;
    resender_ = new Resender(wirelessManager);
    resender_->setAbandonCallback(
        [this](PktType /*type*/, uint8_t subType, uint8_t seqId, const uint8_t* target) {
            LOG_W("QWM",
                "kQuickdrawCommand cmd=%u abandoned: target=%02X:%02X:%02X:%02X:%02X:%02X seqId=%u",
                (unsigned)subType,
                target[0], target[1], target[2], target[3], target[4], target[5],
                (unsigned)seqId);
            if (abandonCallback_) abandonCallback_(subType, target);
        });
}

void QuickdrawWirelessManager::clearCallbacks() {
    packetReceivedCallback = nullptr;
    abandonCallback_ = nullptr;
}

void QuickdrawWirelessManager::setPacketReceivedCallback(const std::function<void(const QuickdrawCommand&)>& callback) {
    packetReceivedCallback = callback;
}

void QuickdrawWirelessManager::setAbandonCallback(AbandonCallback cb) {
    abandonCallback_ = std::move(cb);
}

void QuickdrawWirelessManager::sync() {
    if (resender_) resender_->sync();
}

int QuickdrawWirelessManager::broadcastPacket(const uint8_t macAddress[6],
                                             QuickdrawCommand& command) {
    QuickdrawPacket qdPacket = QuickdrawPacket();

    qdPacket.command = command.command;
    qdPacket.playerDrawTime = command.playerDrawTime;
    qdPacket.isHunter = command.isHunter;
    qdPacket.seqId = command.seqId;

    memcpy(qdPacket.matchId, command.matchId, IdGenerator::UUID_BUFFER_SIZE);
    memcpy(qdPacket.playerId, command.playerId, 5);

    LOG_I("QWM", "Sending command %i to %s", command.command, MacToString(macAddress));
    LOG_I("QWM", "Match ID: %s", qdPacket.matchId);
    LOG_I("QWM", "Player Draw Time: %ld", qdPacket.playerDrawTime);

    return wirelessManager->sendEspNowData(
        macAddress,
        PktType::kQuickdrawCommand,
        reinterpret_cast<const uint8_t*>(&qdPacket),
        sizeof(qdPacket));
}

int QuickdrawWirelessManager::broadcastReliable(const uint8_t macAddress[6],
                                                QuickdrawCommand& command) {
    if (resender_ == nullptr) return -1;

    uint8_t seqId = nextSeqId_++;
    if (nextSeqId_ == 0) nextSeqId_ = 1;
    command.seqId = seqId;

    QuickdrawPacket qdPacket = QuickdrawPacket();
    qdPacket.command = command.command;
    qdPacket.playerDrawTime = command.playerDrawTime;
    qdPacket.isHunter = command.isHunter;
    qdPacket.seqId = seqId;
    memcpy(qdPacket.matchId, command.matchId, IdGenerator::UUID_BUFFER_SIZE);
    memcpy(qdPacket.playerId, command.playerId, 5);

    LOG_I("QWM", "Sending reliable command %i to %s seqId=%u",
          command.command, MacToString(macAddress), (unsigned)seqId);

    // subType keyed on QDCommand so different in-flight commands to the same
    // peer don't dedupe each other.
    resender_->send(macAddress, PktType::kQuickdrawCommand,
                    static_cast<uint8_t>(command.command),
                    seqId,
                    reinterpret_cast<const uint8_t*>(&qdPacket),
                    sizeof(qdPacket));
    return 0;
}

bool QuickdrawWirelessManager::isDuplicate(const uint8_t* mac, uint8_t command, uint8_t seqId) {
    for (auto& e : observed_) {
        if (e.command == command && memcmp(e.mac.data(), mac, 6) == 0) {
            if (e.lastSeqId == seqId) return true;
            e.lastSeqId = seqId;
            return false;
        }
    }
    ObservedKey k;
    memcpy(k.mac.data(), mac, 6);
    k.command = command;
    k.lastSeqId = seqId;
    observed_.push_back(k);
    return false;
}

void QuickdrawWirelessManager::sendAck(const uint8_t* toMac, uint8_t command, uint8_t seqId) {
    Resender::sendAck(wirelessManager, toMac, PktType::kQuickdrawCommand, command, seqId);
}

int QuickdrawWirelessManager::processQuickdrawCommand(const uint8_t *macAddress, const uint8_t *data,
    const size_t dataLen) {

    if(dataLen != sizeof(QuickdrawPacket)) {
        LOG_E("QWM", "QuickdrawPacket size mismatch: got %lu, expected %lu (possible firmware mismatch)",
                      dataLen, sizeof(QuickdrawPacket));
        return -1;
    }

    const QuickdrawPacket* packet = reinterpret_cast<const QuickdrawPacket*>(data);

    // Ack first so the sender can stop retrying even when we've already
    // applied this packet (it's a retransmit).
    if (packet->seqId != 0) {
        sendAck(macAddress, static_cast<uint8_t>(packet->command), packet->seqId);
        if (isDuplicate(macAddress, static_cast<uint8_t>(packet->command), packet->seqId)) {
            return 0;
        }
    }

    QuickdrawCommand command(reinterpret_cast<const uint8_t*>(macAddress), packet->command,
                             packet->matchId, packet->playerId, packet->playerDrawTime, packet->isHunter,
                             packet->seqId);

    if(packetReceivedCallback) {
        packetReceivedCallback(command);
    }

    return 1;
}

