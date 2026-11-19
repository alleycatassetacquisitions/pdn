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
    isOpen = true;
    answer = Answer::NONE;
}

void BandRegistration::close(bool keepKey) {
    dropPieces();
    if (!keepKey) playerProvider->getRecordKey()->clear();
    isOpen = false;
    answer = Answer::NONE;
}

BandRegistration::Answer BandRegistration::getAnswer() const { return answer; }

const Player* BandRegistration::getFoundPlayer() const {
    return answer == Answer::FOUND ? &heldProfile : nullptr;
}

void BandRegistration::setOnAnswerChanged(std::function<void()> callback) {
    onAnswerChanged = std::move(callback);
}

void BandRegistration::onRecordKey(SerialIdentifier jack, const uint8_t* payload, size_t length) {
    if (!isOpen) return;
    if (length != RECORD_KEY_SIZE) {
        LOG_W(TAG, "Ignoring a record key of %u bytes", static_cast<unsigned>(length));
        return;
    }
    // A frame names no sender; the jack's Connected peer is who wrote it.
    const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(jack);
    if (peerMac == nullptr || !isConnectedFdn(jack, peerMac)) {
        LOG_W(TAG, "Ignoring a record key from a jack without a Connected FDN");
        return;
    }
    if (!bindAttempt(jack, peerMac)) return;
    playerProvider->getRecordKey()->set(payload);
    setAnswer(profileIsHeld ? Answer::FOUND : Answer::PARTIAL);
}

void BandRegistration::onProvision(const uint8_t* fromMac, const PlayerProvisionPacket& packet) {
    if (!isOpen) return;
    std::optional<SerialIdentifier> fdnJack;
    for (SerialIdentifier jack : RemoteDeviceCoordinator::HELLO_JACKS) {
        if (isConnectedFdn(jack, fromMac)) fdnJack = jack;
    }
    if (!fdnJack.has_value()) {
        LOG_W(TAG, "Ignoring a provision packet from a sender not Connected as an FDN");
        return;
    }
    if (!bindAttempt(*fdnJack, fromMac)) return;

    // The failure ends the attempt; FetchUser wipes what it held as it shows it.
    if (packet.status == static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED)) {
        LOG_E(TAG, "The FDN reported the band lookup failed");
        isOpen = false;
        setAnswer(Answer::FAILED);
        return;
    }
    if (!PlayerProvisionCodec::decode(packet, heldProfile)) {
        LOG_W(TAG, "Ignoring an undecodable provision packet");
        return;
    }
    profileIsHeld = true;
    setAnswer(playerProvider->getRecordKey()->isHeld() ? Answer::FOUND : Answer::PARTIAL);
}

bool BandRegistration::isConnectedFdn(SerialIdentifier jack, const uint8_t* mac) const {
    const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(jack);
    return remoteDeviceCoordinator->getJackStatus(jack) == JackStatus::CONNECTED &&
           remoteDeviceCoordinator->getPeerDeviceType(jack) == DeviceType::FDN &&
           peerMac != nullptr && memcmp(peerMac, mac, 6) == 0;
}

bool BandRegistration::bindAttempt(SerialIdentifier jack, const uint8_t* mac) {
    if (!attemptFdn.has_value()) {
        attemptFdn.emplace();
        memcpy(attemptFdn->mac.data(), mac, attemptFdn->mac.size());
        attemptFdn->jack = jack;
        return true;
    }
    if (attemptFdn->jack != jack || memcmp(attemptFdn->mac.data(), mac, 6) != 0) {
        LOG_W(TAG, "Ignoring a piece from an FDN other than the attempt's");
        return false;
    }
    return true;
}

void BandRegistration::dropPieces() {
    heldProfile.clearRegistrationProfile();
    profileIsHeld = false;
    attemptFdn.reset();
}

void BandRegistration::setAnswer(Answer changed) {
    answer = changed;
    if (onAnswerChanged) onAnswerChanged();
}
