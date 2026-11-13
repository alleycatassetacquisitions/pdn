#include "game/record-relay.hpp"

#include "device/device-type.hpp"
#include "game/match-manager.hpp"
#include "game/shootout-manager.hpp"
#include "wireless/remote-player-manager.hpp"

RecordRelay::RecordRelay(WirelessManager* wirelessManager,
                         RemoteDeviceCoordinator* remoteDeviceCoordinator,
                         MatchManager* matchManager, ShootoutManager* shootoutManager)
    : wirelessManager(wirelessManager)
    , remoteDeviceCoordinator(remoteDeviceCoordinator)
    , matchManager(matchManager)
    , shootoutManager(shootoutManager)
    , crashLane(wirelessManager, [this]() { return isPlayActive(); })
    , matchLane(wirelessManager, [this]() { return isPlayActive(); }) {
    wirelessManager->setEspNowPacketHandler(
    PktType::kPlayerInfoBroadcast,
    [](const uint8_t* fromMac, const uint8_t* data, const size_t length, void* ctx) {
        static_cast<RecordRelay*>(ctx)->onPlayerInfoBroadcast(fromMac, data, length);
    },
    this);
    remoteDeviceCoordinator->setOnJackChange(
    [this](SerialIdentifier jack, bool connected) { onJackChange(jack, connected); }, this);
    // Cancelled as play starts rather than on the next tick: the transport's
    // retry pass runs before the tick, and a match can start after it.
    matchManager->setOnMatchStarted([this]() { cancelSendsInFlight(); }, this);
    shootoutManager->setOnPhaseEntered(
    [this](ShootoutManager::Phase phase) {
        if (phase != ShootoutManager::Phase::IDLE) {
            cancelSendsInFlight();
        }
    },
    this);
}

RecordRelay::~RecordRelay() {
    shootoutManager->setOnPhaseEntered(nullptr, this);
    matchManager->setOnMatchStarted(nullptr, this);
    remoteDeviceCoordinator->setOnJackChange(nullptr, this);
    wirelessManager->clearEspNowPacketHandler(PktType::kPlayerInfoBroadcast);
}

void RecordRelay::setCrashRecordSource(RecordSource<CrashRecordTraits>* crashRecordSource) {
    crashLane.setRecordSource(crashRecordSource);
}

void RecordRelay::setMatchRecordSource(RecordSource<MatchRecordTraits>* matchRecordSource) {
    matchLane.setRecordSource(matchRecordSource);
}

bool RecordRelay::isPlayActive() const {
    return matchManager->getCurrentMatch().has_value() || shootoutManager->active();
}

void RecordRelay::cancelSendsInFlight() {
    crashLane.cancelInFlight();
    matchLane.cancelInFlight();
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
    if (beacon.senderDeviceType != static_cast<uint8_t>(DeviceType::FDN)) {
        return;
    }
    sendPendingRecords(fromMac);
}

void RecordRelay::onJackChange(SerialIdentifier jack, bool connected) {
    if (!connected || remoteDeviceCoordinator->getPeerDeviceType(jack) != DeviceType::FDN) {
        return;
    }
    const uint8_t* fdnMac = remoteDeviceCoordinator->getPeerMac(jack);
    if (fdnMac == nullptr) {
        LOG_E(RECORD_RELAY_TAG, "An FDN became Connected with no MAC address to send to");
        return;
    }
    sendPendingRecords(fdnMac);
}

void RecordRelay::sendPendingRecords(const uint8_t* fdnMac) {
    crashLane.sendLowestPending(fdnMac);
    matchLane.sendLowestPending(fdnMac);
}
