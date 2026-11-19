#include "apps/player-fetch/band-registration.hpp"

#include "alleycat-server/player-provision-codec.hpp"
#include "device/drivers/logger.hpp"

#include <cstring>

static const char TAG[] = "BandRegistration";

BandRegistration::BandRegistration(WirelessManager* wirelessManager,
                                   RemoteDeviceCoordinator* remoteDeviceCoordinator,
                                   PlayerProvider* playerProvider)
    : remoteDeviceCoordinator(remoteDeviceCoordinator)
    , playerProvider(playerProvider)
    , provisionChannel(wirelessManager, nullptr, PktType::kPlayerProvision, nullptr) {
    remoteDeviceCoordinator->setOnApplicationFrame(
    ApplicationFrameKind::RECORD_KEY,
    [this](SerialIdentifier jack, const uint8_t* payload, size_t length) {
        onRecordKey(jack, payload, length);
    });
    // Refused ahead of duplicate detection, so a frame that came too early does
    // not use up the seqId its retransmit carries.
    provisionChannel.setAdmitFilter([this](const uint8_t* fromMac, const PlayerProvisionPacket&) {
        return phase == Phase::OPEN && fdnJackOf(fromMac).has_value() ? FrameAdmission::DEDUPLICATE
                                                                      : FrameAdmission::REFUSE;
    });
    provisionChannel.onReceive([this](const uint8_t* fromMac, const PlayerProvisionPacket& packet) {
        onProvision(fromMac, packet);
    });
}

BandRegistration::~BandRegistration() {
    remoteDeviceCoordinator->setOnApplicationFrame(ApplicationFrameKind::RECORD_KEY, nullptr,
                                                   nullptr);
    remoteDeviceCoordinator = nullptr;
    playerProvider = nullptr;
}

void BandRegistration::open() {
    dropPieces();
    phase = Phase::OPEN;
}

void BandRegistration::close(bool commitKey) {
    if (commitKey && pendingKey.isHeld())
        playerProvider->getRecordKey()->set(pendingKey.getBytes());
    dropPieces();
    phase = Phase::CLOSED;
}

const Player* BandRegistration::getFoundPlayer() const {
    return pendingKey.isHeld() && heldProfile.hasRegistrationProfile() ? &heldProfile : nullptr;
}

bool BandRegistration::hasFailed() const { return phase == Phase::FAILED; }

void BandRegistration::setOnPieceTaken(std::function<void()> callback) {
    onPieceTaken = std::move(callback);
}

void BandRegistration::onRecordKey(SerialIdentifier jack, const uint8_t* payload, size_t length) {
    if (phase != Phase::OPEN) return;
    if (length != RECORD_KEY_SIZE) {
        LOG_W(TAG, "Ignoring a record key of %u bytes", static_cast<unsigned>(length));
        return;
    }
    // A frame names no sender; the jack's Connected peer is who wrote it.
    if (!isConnectedFdn(jack)) {
        LOG_W(TAG, "Ignoring a record key from a jack without a Connected FDN");
        return;
    }
    if (!bindAttempt(jack, remoteDeviceCoordinator->getPeerMac(jack))) return;
    pendingKey.set(payload);
    if (onPieceTaken) onPieceTaken();
}

void BandRegistration::onProvision(const uint8_t* fromMac, const PlayerProvisionPacket& packet) {
    // The admit filter let it through, so the sender is an FDN Connected on a jack.
    if (!bindAttempt(*fdnJackOf(fromMac), fromMac)) return;
    if (packet.status == static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED)) {
        LOG_E(TAG, "The FDN reported the band lookup failed");
        phase = Phase::FAILED;
    } else if (!PlayerProvisionCodec::decode(packet, heldProfile)) {
        LOG_W(TAG, "Ignoring an undecodable provision packet");
        return;
    }
    if (onPieceTaken) onPieceTaken();
}

bool BandRegistration::isConnectedFdn(SerialIdentifier jack) const {
    return remoteDeviceCoordinator->getJackStatus(jack) == JackStatus::CONNECTED &&
           remoteDeviceCoordinator->getPeerDeviceType(jack) == DeviceType::FDN;
}

std::optional<SerialIdentifier> BandRegistration::fdnJackOf(const uint8_t* mac) const {
    for (SerialIdentifier jack : RemoteDeviceCoordinator::HELLO_JACKS) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(jack);
        if (isConnectedFdn(jack) && peerMac != nullptr && memcmp(peerMac, mac, 6) == 0) return jack;
    }
    return std::nullopt;
}

bool BandRegistration::bindAttempt(SerialIdentifier jack, const uint8_t* mac) {
    if (!attemptIsBound) {
        memcpy(attemptMac.data(), mac, attemptMac.size());
        attemptJack = jack;
        attemptIsBound = true;
        return true;
    }
    if (attemptJack != jack || memcmp(attemptMac.data(), mac, attemptMac.size()) != 0) {
        LOG_W(TAG, "Ignoring a piece from an FDN other than the attempt's");
        return false;
    }
    return true;
}

void BandRegistration::dropPieces() {
    heldProfile.clearRegistrationProfile();
    pendingKey.clear();
    attemptIsBound = false;
}
