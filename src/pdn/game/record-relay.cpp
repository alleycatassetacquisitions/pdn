#include "game/record-relay.hpp"

#include "device/device-type.hpp"
#include "game/match-manager.hpp"
#include "game/shootout-manager.hpp"
#include "wireless/remote-player-manager.hpp"

RecordRelay::RecordRelay(WirelessManager* wirelessManager, MatchManager* matchManager,
                         ShootoutManager* shootoutManager)
    : wirelessManager(wirelessManager)
    , matchManager(matchManager)
    , shootoutManager(shootoutManager)
    , resender(wirelessManager)
    , crashLane(wirelessManager, &resender, nullptr)
    , matchLane(wirelessManager, &resender, matchManager)
    , ackChannel(wirelessManager, &resender, PktType::kRecordAck, nullptr) {
    ackChannel.onReceive([this](const uint8_t* fromMac, const RecordAckPacket& ack) {
        onAcknowledgement(fromMac, ack);
    });
    wirelessManager->setEspNowPacketHandler(
    PktType::kPlayerInfoBroadcast,
    [](const uint8_t* fromMac, const uint8_t* data, const size_t length, void* ctx) {
        static_cast<RecordRelay*>(ctx)->onPlayerInfoBroadcast(fromMac, data, length);
    },
    this);
}

RecordRelay::~RecordRelay() {
    wirelessManager->clearEspNowPacketHandler(PktType::kPlayerInfoBroadcast);
}

void RecordRelay::setCrashRecordSource(RecordSource<CrashRecordTraits>* crashRecordSource) {
    crashLane.setRecordSource(crashRecordSource);
}

bool RecordRelay::isSendingAllowed() {
    return wirelessManager->isEspNowReady() && !matchManager->getCurrentMatch().has_value() &&
           !shootoutManager->active();
}

void RecordRelay::onPlayerInfoBroadcast(const uint8_t* fromMac, const uint8_t* data,
                                        size_t length) {
    if (fromMac == nullptr || data == nullptr || length != sizeof(PlayerInfoPkt)) {
        LOG_W(RECORD_RELAY_TAG, "Dropping a player-info beacon of %u bytes",
              static_cast<unsigned>(length));
        return;
    }
    PlayerInfoPkt beacon{};
    memcpy(&beacon, data, sizeof(beacon));
    // Any device can send this beacon. Only one that says it is an FDN is a
    // relay target; nothing else in the frame is read.
    if (beacon.senderDeviceType != static_cast<uint8_t>(DeviceType::FDN) || !isSendingAllowed()) {
        return;
    }
    crashLane.sendLowestPending(fromMac);
    matchLane.sendLowestPending(fromMac);
}

void RecordRelay::onAcknowledgement(const uint8_t* fromMac, const RecordAckPacket& ack) {
    // Retiring is not gated: an ack that lands during a match still retires
    // its record. Only the next send waits.
    if (ack.kind == static_cast<uint8_t>(RecordKind::CRASH)) {
        if (crashLane.retire(fromMac, ack) && isSendingAllowed())
            crashLane.sendLowestPending(fromMac);
    } else if (ack.kind == static_cast<uint8_t>(RecordKind::MATCH)) {
        if (matchLane.retire(fromMac, ack) && isSendingAllowed())
            matchLane.sendLowestPending(fromMac);
    }
}
