#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include "alleycat-server/player-api.hpp"
#include "alleycat-server/player-provision-codec.hpp"
#include "device/drivers/band-reader.hpp"
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

/**
 * FDN side of player provisioning. Each PDN Connected on a jack with no applied
 * profile is queued for registration in arrival order. While the registration
 * screen is shown and no lookup result is on it, the queue's head is pending,
 * and a band tapped at the reader while it is pending is looked up for that PDN.
 * The pending PDN's turn is open: the connection context the FDN sends each
 * PDN says whether its turn is, and is sent again whenever the turn opens,
 * moves or closes. The answer goes to the pending jack as the player's record
 * key, then to the PDN's MAC as the server's profile on the radio, or as
 * LOOKUP_FAILED. The profile rides a reliable channel; the key never touches
 * the radio. A lookup owns Wi-Fi from its HTTP request to its answer; while
 * another component owns it the lookup waits, inside its time limit.
 */
class FdnProvisioningManager {
public:
    /**
     * Reports a finished lookup: how it ended, the jack it was for and, on
     * PROFILE_DELIVERED, the looked-up player's name (nullptr otherwise).
     */
    using OnLookupDone = std::function<void(ProvisionLookupResult result, SerialIdentifier jack,
                                            const char* playerName)>;

    /** How long a registration stays pending with no tap before going to the back of the queue. */
    static constexpr unsigned long REGISTRATION_PENDING_MS = 30000;

    /**
     * How long a finished lookup's result stays on the registration screen.
     * Nothing is pending and no turn is open meanwhile, so a tap neither lands
     * on the PDN while it shows its own 3000 ms failure page, which refuses a
     * key, nor binds the next jack's PDN before that PDN says it is its turn.
     */
    static constexpr unsigned long LOOKUP_RESULT_HOLD_MS = 3000;

    /**
     * Constructs the FDN provisioning manager wired to wireless, RDC and the band
     * reader driver, and becomes the source of the registration turn in the
     * contexts the coordinator sends. A null @p bandReader is a reader that never
     * answers: nobody can register at this FDN.
     */
    FdnProvisioningManager(WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc,
                           BandReader* bandReader)
        : wirelessManager(wirelessManager)
        , rdc(rdc)
        , bandReader(bandReader)
        , replyChannel(wirelessManager, PktType::kPlayerProvision) {
        rdc->setOnApplicationFrameOutcome(
        [this](SerialIdentifier jack, ApplicationFrameOutcome outcome) {
            onKeyFrameOutcome(jack, outcome);
        },
        this);
        rdc->setRegistrationTurnProvider(
        [this](const uint8_t* pdnMac) { return isRegistrationTurnOpenFor(pdnMac); }, this);
        if (!isBandReaderResponding()) {
            LOG_E("FDN_PROVISION", "Band reader not responding: no PDN can register here");
        }
    }

    /**
     * Stops listening for jack outcomes and answering for the turn; the
     * coordinator outlives this manager.
     */
    ~FdnProvisioningManager() {
        rdc->setOnApplicationFrameOutcome(nullptr, this);
        rdc->setRegistrationTurnProvider(nullptr, this);
    }

    /**
     * Past PROVISION_LOOKUP_TIMEOUT_MS ends a lookup the server has not answered
     * as SERVER_UNREACHABLE; starts a lookup that was waiting for Wi-Fi once it is
     * free; ends the hold of a lookup result after LOOKUP_RESULT_HOLD_MS; queues
     * PDNs that arrived and drops those that left; sends a registration pending
     * for REGISTRATION_PENDING_MS to the back of the queue; takes the band
     * reader's tap; and has the coordinator resend its contexts when the turn
     * opened, moved or closed. Call every tick.
     */
    void sync() {
        if (phase != LookupPhase::IDLE && lookupTimer.expired()) {
            LOG_W("FDN_PROVISION", "Player lookup timed out");
            endLookup(ProvisionLookupResult::SERVER_UNREACHABLE);
        }
        if (phase == LookupPhase::AWAITING_WIFI) {
            requestLookup();
        }
        if (lookupResultIsHeld && lookupResultTimer.expired()) {
            lookupResultIsHeld = false;
            lookupResultTimer.invalidate();
        }
        updateQueue();
        if (hasPendingRegistration() && pendingTimer.expired()) {
            LOG_I("FDN_PROVISION", "Registration pending with no tap; requeued at the back");
            sendPendingToBack();
        }
        if (hasPendingRegistration() && !pendingTimer.isRunning()) {
            pendingTimer.setTimer(REGISTRATION_PENDING_MS);
        }
        takeBandTap();
        followRegistrationTurn();
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
     * @return true while a band lookup is waiting for Wi-Fi, on the server, or
     * on the jack to report the record key written.
     */
    bool isLookupInFlight() const { return phase != LookupPhase::IDLE; }

    /**
     * @return true when a registration waits for a tap: the registration screen
     * is shown, one is queued, no lookup runs and no lookup result is held.
     */
    bool hasPendingRegistration() const {
        return registrationScreenIsShown && phase == LookupPhase::IDLE && !lookupResultIsHeld &&
               queueLength > 0;
    }

    /** @return true for LOOKUP_RESULT_HOLD_MS after a lookup ends, however it ended. */
    bool isHoldingLookupResult() const { return lookupResultIsHeld; }

    /** @return the pending registration's jack; meaningful only while hasPendingRegistration(). */
    SerialIdentifier getPendingJack() const { return queue[0].jack; }

    /**
     * @return true when the registration turn is with the PDN whose radio MAC is
     * @p pdnMac: its registration is pending, so a tap now registers it.
     */
    bool isRegistrationTurnOpenFor(const uint8_t* pdnMac) const {
        return hasPendingRegistration() && memcmp(queue[0].pdnMac.data(), pdnMac, 6) == 0;
    }

    /** @return true while the band reader answers; false with no reader at all. */
    bool isBandReaderResponding() const {
        return bandReader != nullptr && bandReader->isResponding();
    }

    /** Sets the lookup-finished listener; nullptr clears it. */
    void setOnLookupDone(OnLookupDone listener) { onLookupDone = std::move(listener); }

    /**
     * Registration is pending only while the registration screen is shown. Off
     * it, during symbol match, a queued PDN waits with no timer running and taps
     * are ignored, so a lookup's Wi-Fi excursion never cuts into a game.
     */
    void setRegistrationScreenShown(bool shown) {
        registrationScreenIsShown = shown;
        if (!shown) {
            pendingTimer.invalidate();
        }
    }

    /** @return true when a PDN with no applied profile is Connected on an FDN jack. */
    bool hasUnprovisionedPdnCabled() const { return hasCabledPdn(false); }

    /** @return true when a PDN with an applied profile, demo included, is Connected. */
    bool hasRegisteredPdnCabled() const { return hasCabledPdn(true); }

private:
    // Where the one lookup this manager runs at a time stands.
    enum class LookupPhase : uint8_t {
        IDLE,
        // Another component owns Wi-Fi; sync() asks again.
        AWAITING_WIFI,
        // This lookup owns Wi-Fi and its HTTP request is queued.
        AWAITING_SERVER,
        // Wi-Fi is given back; lookupJack has the key frame and has not reported on it.
        AWAITING_JACK,
    };

    // A PDN waiting for its registration, by the jack it is on and its MAC
    // there, so a different PDN plugged into the same jack is a new arrival.
    struct QueuedRegistration {
        SerialIdentifier jack = SerialIdentifier::INPUT_JACK;
        std::array<uint8_t, 6> pdnMac{};
    };

    static constexpr std::array<SerialIdentifier, 2> REGISTRATION_JACKS = {
    SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY};

    // The MAC of the PDN Connected on `jack` whose profile reads `registered`;
    // nullptr when the jack holds no such PDN.
    const uint8_t* getCabledPdnMac(SerialIdentifier jack, bool registered) const {
        if (rdc->getJackStatus(jack) != JackStatus::CONNECTED ||
            rdc->getPeerDeviceType(jack) != DeviceType::PDN) {
            return nullptr;
        }
        size_t length = 0;
        const uint8_t* bytes = rdc->getPeerProfile(jack, length);
        if (bytes == nullptr || length < sizeof(PlayerProfile)) return nullptr;
        PlayerProfile profile{};
        memcpy(&profile, bytes, sizeof(profile));
        // A demo player has no server id either, but is done registering.
        if ((profile.registered != 0) != registered) return nullptr;
        return rdc->getPeerMac(jack);
    }

    bool hasCabledPdn(bool registered) const {
        for (SerialIdentifier jack : REGISTRATION_JACKS) {
            if (getCabledPdnMac(jack, registered) != nullptr) return true;
        }
        return false;
    }

    bool isQueued(SerialIdentifier jack) const {
        for (size_t i = 0; i < queueLength; ++i) {
            if (queue[i].jack == jack) return true;
        }
        return false;
    }

    // Drops registrations whose PDN left its jack or registered, then queues newly
    // arrived unprovisioned PDNs at the back. A PDN whose registration just ended
    // is in neither the queue nor the lookup, so if still unprovisioned it rejoins
    // at the back here.
    void updateQueue() {
        const bool hadPending = hasPendingRegistration();
        const QueuedRegistration previousHead = queue[0];
        size_t kept = 0;
        for (size_t i = 0; i < queueLength; ++i) {
            const uint8_t* mac = getCabledPdnMac(queue[i].jack, false);
            if (mac != nullptr && memcmp(mac, queue[i].pdnMac.data(), 6) == 0) {
                queue[kept++] = queue[i];
            }
        }
        queueLength = kept;

        for (SerialIdentifier jack : REGISTRATION_JACKS) {
            const uint8_t* mac = getCabledPdnMac(jack, false);
            if (mac == nullptr || isQueued(jack)) continue;
            if (phase != LookupPhase::IDLE && jack == lookupJack &&
                memcmp(mac, lookupMac.data(), lookupMac.size()) == 0) {
                continue;
            }
            queue[queueLength].jack = jack;
            memcpy(queue[queueLength].pdnMac.data(), mac, 6);
            queueLength++;
        }

        const bool headChanged = queueLength == 0 || previousHead.jack != queue[0].jack ||
                                 memcmp(previousHead.pdnMac.data(), queue[0].pdnMac.data(), 6) != 0;
        if (hadPending && headChanged) {
            pendingTimer.invalidate();
        }
    }

    void sendPendingToBack() {
        std::rotate(queue.begin(), queue.begin() + 1, queue.begin() + queueLength);
        pendingTimer.invalidate();
    }

    // Has the coordinator resend its contexts when the turn opened, moved to
    // another PDN or closed since the last tick, so each PDN's screen follows it.
    void followRegistrationTurn() {
        const bool turnIsOpen = hasPendingRegistration();
        const bool turnChanged = turnIsOpen != announcedTurnIsOpen ||
                                 (turnIsOpen && (announcedTurn.jack != queue[0].jack ||
                                                 announcedTurn.pdnMac != queue[0].pdnMac));
        if (!turnChanged) return;
        announcedTurnIsOpen = turnIsOpen;
        if (turnIsOpen) announcedTurn = queue[0];
        rdc->resendContext();
    }

    void takeBandTap() {
        if (bandReader == nullptr) return;
        BandUid uid;
        if (!bandReader->takeTap(uid)) return;
        if (!hasPendingRegistration()) {
            LOG_I("FDN_PROVISION", "Band tap ignored: no registration pending");
        } else if (!uid.hasLookupLength()) {
            LOG_W("FDN_PROVISION", "Band tap ignored: a UID of %u bytes",
                  static_cast<unsigned>(uid.length));
        } else {
            startLookup(uid);
        }
        secureWipe(uid.bytes.data(), uid.bytes.size());
    }

    // Binds the tap to the PDN on the pending jack as of now: updateQueue() ran
    // this tick, so the head's PDN is still the one Connected there.
    void startLookup(const BandUid& uid) {
        lookupJack = queue[0].jack;
        lookupMac = queue[0].pdnMac;
        std::copy(queue.begin() + 1, queue.begin() + queueLength, queue.begin());
        queueLength--;
        pendingTimer.invalidate();

        lookupUid = uid;
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

        // The UID is held for its lookup and no longer; the request carries it from here.
        PlayerApi::getDevicePlayerByBand(
        wirelessManager, lookupUid, &decodedPlayer, &recordKey,
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
            endLookup(PlayerApi::classifyLookupError(error));
        });
        secureWipe(lookupUid.bytes.data(), lookupUid.bytes.size());
    }

    // The server answered with a profile. Its record key goes to the pending
    // jack first; the profile waits for the jack to report the key frame written.
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
        if (!rdc->isConnectedPeer(lookupJack, DeviceType::PDN, lookupMac.data())) {
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
        const bool accepted =
        rdc->sendApplicationFrame(lookupJack, ApplicationFrameKind::RECORD_KEY, keyFrame);
        secureWipe(keyFrame.data(), keyFrame.size());
        if (!accepted) {
            LOG_E("FDN_PROVISION", "Jack refused the record key frame");
            endLookup(ProvisionLookupResult::KEY_NOT_ISSUED);
        }
    }

    void onKeyFrameOutcome(SerialIdentifier jack, ApplicationFrameOutcome outcome) {
        // A frame whose lookup was cancelled still gets its outcome; nothing waits for it.
        if (phase != LookupPhase::AWAITING_JACK || jack != lookupJack) return;
        if (outcome != ApplicationFrameOutcome::WRITTEN) {
            LOG_E("FDN_PROVISION", "Record key frame dropped: the PDN left its jack");
            endLookup(ProvisionLookupResult::KEY_NOT_ISSUED);
            return;
        }
        // A frame the link went down on mid-write is reported written, to a PDN
        // that is gone.
        if (!rdc->isConnectedPeer(lookupJack, DeviceType::PDN, lookupMac.data())) {
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
        lookupResultTimer.setTimer(LOOKUP_RESULT_HOLD_MS);
        lookupResultIsHeld = true;
        recordKey.clear();
        secureWipe(lookupUid.bytes.data(), lookupUid.bytes.size());
        // Before the reply: it can only be sent with the radio back on peer-to-peer.
        if (ownedWifi) {
            wirelessManager->releaseWifi(this);
        }
        const bool delivered = result == ProvisionLookupResult::PROFILE_DELIVERED;
        if (onLookupDone) {
            onLookupDone(result, lookupJack,
                         delivered ? decodedPlayer.getServerPlayerName() : nullptr);
        }
        if (!rdc->isConnectedPeer(lookupJack, DeviceType::PDN, lookupMac.data())) {
            return;
        }
        PlayerProvisionPacket pkt{};
        if (delivered) {
            pkt = PlayerProvisionCodec::encode(decodedPlayer);
        } else {
            pkt.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
        }
        replyChannel.sendReliable(lookupMac.data(), pkt);
    }

    WirelessManager* wirelessManager = nullptr;
    RemoteDeviceCoordinator* rdc = nullptr;
    BandReader* bandReader = nullptr;
    ReliableChannel<PlayerProvisionPacket> replyChannel;
    LookupPhase phase = LookupPhase::IDLE;
    bool registrationScreenIsShown = false;
    std::array<QueuedRegistration, REGISTRATION_JACKS.size()> queue{};
    size_t queueLength = 0;
    SimpleTimer pendingTimer;
    // The turn the contexts last sent carry: open or not, and if open, whose.
    bool announcedTurnIsOpen = false;
    QueuedRegistration announcedTurn;
    SimpleTimer lookupTimer;
    SimpleTimer lookupResultTimer;
    bool lookupResultIsHeld = false;
    SerialIdentifier lookupJack = SerialIdentifier::INPUT_JACK;
    std::array<uint8_t, 6> lookupMac{};
    // Held from the tap until the lookup's request is queued.
    BandUid lookupUid;
    Player decodedPlayer{};
    // Held only between the response's decode and the hand-off to the jack.
    RecordKey recordKey;
    OnLookupDone onLookupDone;
};
