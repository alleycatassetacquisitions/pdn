#pragma once

#include <array>
#include <cstring>
#include <functional>
#include <string>
#include "alleycat-server/player-api.hpp"
#include "alleycat-server/player-provision-codec.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "game/player.hpp"
#include "game/player-types.hpp"
#include "game/record-key.hpp"
#include "utils/secure-wipe.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/reliable-channel.hpp"

/** How a PIN lookup ended, for the FDN's registration screen. */
enum class ProvisionLookupResult : uint8_t {
    // The record key was written to the PDN's jack and the profile sent after it.
    PROFILE_DELIVERED,
    // The server gave no profile: no such player, an error, or no answer in time.
    LOOKUP_FAILED,
    // The server gave a profile, but the jack did not report the key written to
    // a still-Connected PDN, or the lookup was cancelled first.
    KEY_NOT_ISSUED,
    // The server presented a key other than the one pinned in this FDN's
    // firmware, or the firmware pins none, so nothing it sent was used. The PDN
    // is told LOOKUP_FAILED.
    SERVER_NOT_TRUSTED,
};

/**
 * FDN side of player provisioning: answers a cabled PDN's PIN request with the
 * player's record key on the jack and then the server's profile on the radio,
 * or with LOOKUP_FAILED. Request and reply ride reliable channels; the key
 * never does. A lookup owns Wi-Fi from its HTTP request to its answer; while
 * another component owns it the lookup waits, inside its time limit.
 */
class FdnProvisioningManager {
public:
    /**
     * Reports a finished lookup: how it ended and, on PROFILE_DELIVERED, the
     * looked-up player's name (nullptr otherwise).
     */
    using OnLookupDone = std::function<void(ProvisionLookupResult result, const char* playerName)>;

    /** Constructs the FDN provisioning manager wired to wireless and RDC. */
    FdnProvisioningManager(WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc)
        : wirelessManager(wirelessManager)
        , rdc(rdc)
        , requestChannel(wirelessManager, PktType::kPlayerProvisionRequest)
        , replyChannel(wirelessManager, PktType::kPlayerProvision) {
        requestChannel.onReceive(
        [this](const uint8_t* fromMac, const PlayerProvisionRequestPacket& request) {
            onProvisionRequest(fromMac, request);
        });
        rdc->setOnApplicationFrameOutcome(
        [this](SerialIdentifier jack, ApplicationFrameOutcome outcome) {
            onKeyFrameOutcome(jack, outcome);
        },
        this);
    }

    /** Stops listening for jack outcomes; the coordinator outlives this manager. */
    ~FdnProvisioningManager() { rdc->setOnApplicationFrameOutcome(nullptr, this); }

    /**
     * Serves a parked request once its PDN is Connected and no lookup is in
     * flight, starts a lookup that was waiting for Wi-Fi once it is free, and
     * past PROVISION_LOOKUP_TIMEOUT_MS cancels a lookup the server has not
     * answered (see cancelLookup). Call every tick.
     */
    void sync() {
        if (phase != LookupPhase::IDLE && lookupTimer.expired()) {
            LOG_W("FDN_PROVISION", "Player lookup timed out");
            cancelLookup();
        }
        if (phase == LookupPhase::AWAITING_WIFI) {
            requestLookup();
        }
        if (!parkedTimer.isRunning()) return;
        if (parkedTimer.expired()) {
            parkedTimer.invalidate();
        } else if (phase == LookupPhase::IDLE &&
                   rdc->isConnectedPeer(DeviceType::PDN, parkedMac.data())) {
            parkedTimer.invalidate();
            provisionPdn(parkedMac.data(), parkedPin);
        }
    }

    /**
     * Ends the lookup in flight, if any, without waiting for the deadline:
     * with LOOKUP_FAILED while the server has not answered, giving Wi-Fi up at
     * once; with KEY_NOT_ISSUED once the record key is with the jack, and no
     * profile then follows the key.
     */
    void cancelLookup() {
        if (phase == LookupPhase::IDLE) return;
        if (phase == LookupPhase::AWAITING_JACK) {
            LOG_E("FDN_PROVISION", "Lookup cancelled before the jack reported the key written");
            endLookup(ProvisionLookupResult::KEY_NOT_ISSUED);
            return;
        }
        endLookup(ProvisionLookupResult::LOOKUP_FAILED);
    }

    /**
     * @return true while a PIN lookup is waiting for Wi-Fi, on the server, or
     * on the jack to report the record key written.
     */
    bool isLookupInFlight() const { return phase != LookupPhase::IDLE; }

    /** Sets the lookup-finished listener; nullptr clears it. */
    void setOnLookupDone(OnLookupDone listener) { onLookupDone = std::move(listener); }

    /**
     * While not allowed, a request is dropped if a registered PDN is cabled, so a
     * lookup's Wi-Fi excursion never cuts into that PDN's symbol-match game.
     */
    void setLookupDuringPlayAllowed(bool allowed) { lookupDuringPlayAllowed = allowed; }

    /** @return true when a PDN with no applied profile is Connected on an FDN jack. */
    bool hasUnprovisionedPdnCabled() { return hasCabledPdn(false); }

    /** @return true when a PDN with an applied profile, demo included, is Connected. */
    bool hasRegisteredPdnCabled() { return hasCabledPdn(true); }

private:
    // Where the one lookup this manager runs at a time stands.
    enum class LookupPhase : uint8_t {
        IDLE,
        // Another component owns Wi-Fi; sync() asks again.
        AWAITING_WIFI,
        // This lookup owns Wi-Fi and its HTTP request is queued.
        AWAITING_SERVER,
        // Wi-Fi is given back; keyJack has the key frame and has not reported on it.
        AWAITING_JACK,
    };

    // How long a request that cannot be served yet is kept. The PDN can read
    // its jack Connected, and send, a tick before this side's jack commits; or
    // the request lands while another PDN's key is on its way to the jack. The
    // radio acks the request, so the PDN never sends it again. One request is
    // parked at a time: a later one takes its place, and the PDN it displaces
    // gets no answer and fails at the end of its own wait for a reply.
    static constexpr unsigned long PARK_REQUEST_MS = 1000;

    bool hasCabledPdn(bool registered) {
        for (SerialIdentifier jack :
             {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
            if (rdc->getJackStatus(jack) != JackStatus::CONNECTED ||
                rdc->getPeerDeviceType(jack) != DeviceType::PDN) {
                continue;
            }
            size_t length = 0;
            const uint8_t* bytes = rdc->getPeerProfile(jack, length);
            if (bytes == nullptr || length < sizeof(PlayerProfile)) continue;
            PlayerProfile profile{};
            memcpy(&profile, bytes, sizeof(profile));
            // A demo player has no server id either, but is done registering.
            if ((profile.registered != 0) == registered) return true;
        }
        return false;
    }

    void provisionPdn(const uint8_t* pdnMac, const std::string& pin) {
        if (phase != LookupPhase::IDLE) return;

        memcpy(lookupMac.data(), pdnMac, lookupMac.size());
        lookupPin = pin;
        phase = LookupPhase::AWAITING_WIFI;
        // Covers the wait for Wi-Fi as well as the request.
        lookupTimer.setTimer(PROVISION_LOOKUP_TIMEOUT_MS);
        requestLookup();
    }

    // Queues the lookup's HTTP request if Wi-Fi can be had. While another
    // component owns it the lookup stays in flight, and sync() comes back here.
    void requestLookup() {
        if (!wirelessManager->acquireWifi(this)) return;
        phase = LookupPhase::AWAITING_SERVER;
        decodedPlayer = Player{};

        PlayerApi::getDevicePlayerByPin(
        wirelessManager, lookupPin, &decodedPlayer, &recordKey,
        [this](Player&) {
            if (phase != LookupPhase::AWAITING_SERVER) {
                // The answer of a lookup that has ended; its key was decoded all the same.
                recordKey.clear();
                return;
            }
            handOffRecordKey();
        },
        [this](const WirelessErrorInfo& error) {
            // Giving Wi-Fi up fails the request of a lookup that has already ended.
            if (phase != LookupPhase::AWAITING_SERVER) return;
            endLookup(error.code == WirelessError::SERVER_NOT_TRUSTED
                      ? ProvisionLookupResult::SERVER_NOT_TRUSTED
                      : ProvisionLookupResult::LOOKUP_FAILED);
        });
    }

    // The server answered with a profile. Its record key goes to the PDN's jack
    // first; the profile waits for the jack to report the key frame written.
    void handOffRecordKey() {
        // The jack needs no Wi-Fi, and whatever reply ends this lookup can only
        // be sent with the radio back on peer-to-peer.
        phase = LookupPhase::AWAITING_JACK;
        lookupTimer.invalidate();
        wirelessManager->releaseWifi(this);
        if (!recordKey.isHeld()) {
            LOG_E("FDN_PROVISION", "Lookup answered with no usable record key");
            endLookup(ProvisionLookupResult::KEY_NOT_ISSUED);
            return;
        }
        if (!findPdnJack(keyJack)) {
            LOG_E("FDN_PROVISION", "PDN left before its record key could be written");
            endLookup(ProvisionLookupResult::KEY_NOT_ISSUED);
            return;
        }
        static_assert(sizeof(ApplicationPayload) == RECORD_KEY_SIZE,
                      "the application frame carries the record key and nothing else");
        ApplicationPayload keyFrame{};
        memcpy(keyFrame.data(), recordKey.getBytes(), keyFrame.size());
        // The coordinator keeps its own copy of an accepted frame and clears it
        // as the frame is written or dropped, so neither copy here outlives the call.
        recordKey.clear();
        const bool accepted = rdc->sendApplicationFrame(keyJack, keyFrame);
        secureWipe(keyFrame.data(), keyFrame.size());
        if (!accepted) {
            LOG_E("FDN_PROVISION", "Jack refused the record key frame");
            endLookup(ProvisionLookupResult::KEY_NOT_ISSUED);
        }
    }

    void onKeyFrameOutcome(SerialIdentifier jack, ApplicationFrameOutcome outcome) {
        // A frame whose lookup was cancelled still gets its outcome; nothing waits for it.
        if (phase != LookupPhase::AWAITING_JACK || jack != keyJack) return;
        if (outcome != ApplicationFrameOutcome::WRITTEN) {
            LOG_E("FDN_PROVISION", "Record key frame dropped: the PDN left its jack");
            endLookup(ProvisionLookupResult::KEY_NOT_ISSUED);
            return;
        }
        // A frame the link went down on mid-write is reported written, to a PDN
        // that is gone.
        if (!rdc->isConnectedPeer(keyJack, DeviceType::PDN, lookupMac.data())) {
            LOG_E("FDN_PROVISION", "PDN left as its record key was written");
            endLookup(ProvisionLookupResult::KEY_NOT_ISSUED);
            return;
        }
        endLookup(ProvisionLookupResult::PROFILE_DELIVERED);
    }

    void endLookup(ProvisionLookupResult result) {
        const bool ownedWifi = phase == LookupPhase::AWAITING_SERVER;
        phase = LookupPhase::IDLE;
        lookupTimer.invalidate();
        recordKey.clear();
        // Before the reply: it can only be sent with the radio back on peer-to-peer.
        if (ownedWifi) {
            wirelessManager->releaseWifi(this);
        }
        const bool delivered = result == ProvisionLookupResult::PROFILE_DELIVERED;
        if (onLookupDone) {
            onLookupDone(result, delivered ? decodedPlayer.getServerPlayerName() : nullptr);
        }
        if (!rdc->isConnectedPeer(DeviceType::PDN, lookupMac.data())) {
            return;
        }
        PlayerProvisionPacket pkt{};
        if (delivered) {
            pkt = PlayerProvisionCodec::encode(decodedPlayer);
        } else {
            pkt.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
        }
        strncpy(pkt.pin, lookupPin.c_str(), sizeof(pkt.pin) - 1);
        replyChannel.sendReliable(lookupMac.data(), pkt);
    }

    // The FDN jack the lookup's PDN is Connected on, written to `jack`; false,
    // leaving `jack` alone, when it is on neither.
    bool findPdnJack(SerialIdentifier& jack) {
        for (SerialIdentifier candidate :
             {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
            if (rdc->isConnectedPeer(candidate, DeviceType::PDN, lookupMac.data())) {
                jack = candidate;
                return true;
            }
        }
        return false;
    }

    void onProvisionRequest(const uint8_t* fromMac, const PlayerProvisionRequestPacket& request) {
        if (!lookupDuringPlayAllowed && hasRegisteredPdnCabled()) {
            LOG_W("FDN_PROVISION", "Dropping provision request during a registered PDN's game");
            return;
        }
        // The PIN becomes a URL path segment, so only four ASCII digits leave the FDN.
        for (size_t i = 0; i < 4; ++i) {
            if (request.pin[i] < '0' || request.pin[i] > '9') {
                LOG_W("FDN_PROVISION", "Dropping provision request with a malformed PIN");
                return;
            }
        }
        const std::string pin(request.pin, 4);
        if (phase != LookupPhase::IDLE) {
            // The lookup in flight is already this PDN's answer.
            if (memcmp(fromMac, lookupMac.data(), lookupMac.size()) == 0) return;
            parkRequest(fromMac, pin);
            return;
        }
        if (!rdc->isConnectedPeer(DeviceType::PDN, fromMac)) {
            parkRequest(fromMac, pin);
            return;
        }
        provisionPdn(fromMac, pin);
    }

    void parkRequest(const uint8_t* fromMac, const std::string& pin) {
        memcpy(parkedMac.data(), fromMac, parkedMac.size());
        parkedPin = pin;
        parkedTimer.setTimer(PARK_REQUEST_MS);
    }

    WirelessManager* wirelessManager = nullptr;
    RemoteDeviceCoordinator* rdc = nullptr;
    ReliableChannel<PlayerProvisionRequestPacket> requestChannel;
    ReliableChannel<PlayerProvisionPacket> replyChannel;
    LookupPhase phase = LookupPhase::IDLE;
    bool lookupDuringPlayAllowed = false;
    SerialIdentifier keyJack = SerialIdentifier::INPUT_JACK;
    SimpleTimer lookupTimer;
    SimpleTimer parkedTimer;
    std::array<uint8_t, 6> parkedMac{};
    std::string parkedPin;
    std::array<uint8_t, 6> lookupMac{};
    std::string lookupPin;
    Player decodedPlayer{};
    // Held only between the response's decode and the hand-off to the jack.
    RecordKey recordKey;
    OnLookupDone onLookupDone;
};
