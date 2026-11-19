#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include "alleycat-server/player-api.hpp"
#include "alleycat-server/player-provision-codec.hpp"
#include "device/application-frame-kind.hpp"
#include "device/drivers/band-reader.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-types.hpp"
#include "utils/secure-wipe.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"

/** What the FDN registration screen shows. */
enum class RegistrationScreen : uint8_t {
    // Nothing queued, no lookup, no result held: the screen is not needed.
    NONE,
    // The turn is open for the PDN on `jack`.
    TAP_BAND,
    // The PDN on `jack` is next, but the band reader does not answer.
    NO_READER,
    // A tap's lookup for the PDN on `jack` runs.
    LOOKING_UP,
    // How the last lookup for `jack` ended.
    RESULT,
};

/** How a registration attempt ended. */
enum class RegistrationOutcome : uint8_t {
    // The record key was written to the PDN's jack and its profile sent.
    REGISTERED,
    // A key was issued, but the PDN left before its key and profile went out.
    PDN_LEFT,
    KEY_NOT_ISSUED,
    NO_PLAYER,
    SERVER_NOT_TRUSTED,
    LOOKUP_FAILED,
};

/** The registration screen's facts; `outcome` and `playerName` only for RESULT. */
struct RegistrationView {
    RegistrationScreen screen = RegistrationScreen::NONE;
    SerialIdentifier jack = SerialIdentifier::INPUT_JACK;
    RegistrationOutcome outcome = RegistrationOutcome::LOOKUP_FAILED;
    char playerName[PlayerLimits::NAME_BUFFER_SIZE] = {};

    /** @return true when both would draw the same screen. */
    bool operator==(const RegistrationView& other) const {
        return screen == other.screen && jack == other.jack && outcome == other.outcome &&
               strcmp(playerName, other.playerName) == 0;
    }
};

/** The two text lines of the FDN registration screen. */
struct RegistrationScreenLines {
    const char* top;
    const char* bottom;
};

/**
 * The text for @p view. The jack labels are the FDN's: JACK 1 for the primary
 * input jack, JACK 2 for the secondary. A registered result shows the player's
 * name from @p view, so the lines live as long as @p view does.
 */
inline RegistrationScreenLines getRegistrationScreenLines(const RegistrationView& view) {
    const char* jackLabel =
    view.jack == SerialIdentifier::INPUT_JACK_SECONDARY ? "JACK 2" : "JACK 1";
    switch (view.screen) {
        case RegistrationScreen::NONE: return {"", ""};
        case RegistrationScreen::TAP_BAND: return {"TAP BAND", jackLabel};
        case RegistrationScreen::NO_READER: return {"NO READER", jackLabel};
        case RegistrationScreen::LOOKING_UP: return {"LOOKING", "UP"};
        case RegistrationScreen::RESULT: break;
    }
    switch (view.outcome) {
        case RegistrationOutcome::REGISTERED: return {"WELCOME", view.playerName};
        case RegistrationOutcome::PDN_LEFT: return {"PDN LEFT", jackLabel};
        case RegistrationOutcome::KEY_NOT_ISSUED: return {"UNABLE TO", "ISSUE KEY"};
        case RegistrationOutcome::NO_PLAYER: return {"SEE DESK", jackLabel};
        case RegistrationOutcome::SERVER_NOT_TRUSTED: return {"SERVER", "NOT TRUSTED"};
        case RegistrationOutcome::LOOKUP_FAILED: break;
    }
    return {"UNABLE TO", "LOCATE ASSET"};
}

/**
 * FDN side of band registration: a queue of cabled unregistered PDNs, the
 * turn in every context the FDN sends, and tap → lookup → key frame, then
 * profile. Lives as long as the FDN: a pending lookup's HTTP callback captures
 * this manager.
 */
class FdnProvisioningManager {
public:
    /** Told of every change to what the registration screen shows. */
    using OnViewChanged = std::function<void(const RegistrationView& view)>;

    /** How long one PDN holds the turn with no tap before it passes to a waiting PDN. */
    static constexpr unsigned long REGISTRATION_PENDING_MS = 30000;
    /**
     * How long a lookup's result holds. No turn is open meanwhile, so a tap
     * cannot hand a key to a PDN still showing its own failure.
     */
    static constexpr unsigned long LOOKUP_RESULT_HOLD_MS = 3000;
    /**
     * A lookup with no answer by then failed, so the turn and its hold never
     * wedge; covers Wi-Fi association and the HTTP client's own 10 s timeout.
     */
    static constexpr unsigned long LOOKUP_TIMEOUT_MS = 15000;
    /**
     * After a tap, how long the lookup waits for the context closing the turn
     * to reach the tapped PDN before Wi-Fi takes the radio from ESP-NOW.
     */
    static constexpr unsigned long TURN_CLOSE_MS = 500;

    /**
     * Listens to @p remoteDeviceCoordinator's jacks and contexts, becomes the
     * source of the turn its contexts carry and the owner of record-key frames,
     * and queues the unregistered PDNs already cabled. A null @p bandReader is
     * a reader that never answers: nobody registers at this FDN.
     */
    FdnProvisioningManager(WirelessManager* wirelessManager,
                           RemoteDeviceCoordinator* remoteDeviceCoordinator, BandReader* bandReader)
        : wirelessManager(wirelessManager)
        , remoteDeviceCoordinator(remoteDeviceCoordinator)
        , bandReader(bandReader)
        , resender(wirelessManager)
        , replyChannel(wirelessManager, &resender, PktType::kPlayerProvision, nullptr) {
        remoteDeviceCoordinator->setOnApplicationFrame(
        ApplicationFrameKind::RECORD_KEY, nullptr,
        [this](SerialIdentifier jack, ApplicationFrameOutcome outcome) {
            onKeyFrameOutcome(jack, outcome);
        });
        remoteDeviceCoordinator->setRegistrationTurnProvider(
        [this](const uint8_t* peerMac) { return isTurnOpenFor(peerMac); });
        remoteDeviceCoordinator->setChainChangeCallback([this]() { updateQueue(); });
        remoteDeviceCoordinator->setOnContextReceived(
        [this](SerialIdentifier, DeviceType, const uint8_t*, size_t) { updateQueue(); });
        if (bandReader != nullptr) {
            bandReader->setTapHandler([this](const BandUid& uid) { onBandTap(uid); });
        }
        readerResponds = bandReader != nullptr && bandReader->isResponding();
        if (!readerResponds)
            LOG_E("FDN_PROVISION", "No band reader answers: nobody registers here");
        updateQueue();
    }

    /** Lets go of every slot it took on the coordinator and the reader. */
    ~FdnProvisioningManager() {
        remoteDeviceCoordinator->setOnApplicationFrame(ApplicationFrameKind::RECORD_KEY, nullptr,
                                                       nullptr);
        remoteDeviceCoordinator->setRegistrationTurnProvider(nullptr);
        remoteDeviceCoordinator->setChainChangeCallback(nullptr);
        remoteDeviceCoordinator->setOnContextReceived(nullptr);
        if (bandReader != nullptr) bandReader->setTapHandler(nullptr);
        secureWipe(lookupUid.bytes.data(), lookupUid.bytes.size());
    }

    /**
     * Runs the reply retries, the lookup deadline, the result hold and the
     * untapped-turn rotation, pushes turn changes, starts a tapped lookup once
     * its turn is closed, and pushes view changes. Call every tick.
     */
    void sync() {
        resender.sync();
        readerResponds = bandReader != nullptr && bandReader->isResponding();
        if (phase == LookupPhase::AWAITING_SERVER && phaseTimer.expired()) {
            LOG_W("FDN_PROVISION", "Band lookup timed out");
            giveRadioBack();
            endLookup(RegistrationOutcome::LOOKUP_FAILED);
        } else if (phase == LookupPhase::HOLDING_RESULT && phaseTimer.expired()) {
            phase = LookupPhase::IDLE;
            phaseTimer.invalidate();
        }
        if (isTurnOpen() && pendingTimer.expired()) {
            if (queueLength > 1) {
                LOG_I("FDN_PROVISION", "No tap in time; the next PDN takes the turn");
                const QueuedPdn head = queue[0];
                queue[0] = queue[1];
                queue[1] = head;
            } else {
                pendingTimer.setTimer(REGISTRATION_PENDING_MS);
            }
        }
        announceTurn();
        if (phase == LookupPhase::CLOSING_TURN &&
            (phaseTimer.expired() ||
             !remoteDeviceCoordinator->isContextSendPending(lookupPdn.mac.data()))) {
            startLookup();
        }
        publishView();
    }

    /**
     * Turns are open only while the registration screen is shown, so a lookup's
     * Wi-Fi excursion never cuts into symbol match.
     */
    void setRegistrationScreenShown(bool shown) { registrationScreenIsShown = shown; }

    /**
     * Sets the one view listener, nullptr clears it, and tells it the current
     * view at once.
     */
    void setOnViewChanged(OnViewChanged listener) {
        onViewChanged = std::move(listener);
        announcedView = getView();
        if (onViewChanged) onViewChanged(announcedView);
    }

private:
    enum class LookupPhase : uint8_t {
        IDLE,
        // A tap is bound; the context closing its turn is on its way to the PDN.
        CLOSING_TURN,
        // The request is with the server; the lookup owns the radio for Wi-Fi.
        AWAITING_SERVER,
        // The record key frame waits for its outcome on the lookup's jack.
        AWAITING_JACK,
        // The lookup ended; its result shows and no turn is open.
        HOLDING_RESULT,
    };

    // A PDN by its jack and its MAC there, so another PDN on the same jack is
    // another registration.
    struct QueuedPdn {
        SerialIdentifier jack = SerialIdentifier::INPUT_JACK;
        std::array<uint8_t, 6> mac{};

        bool operator==(const QueuedPdn& other) const {
            return jack == other.jack && mac == other.mac;
        }
    };

    static constexpr std::array<SerialIdentifier, 2> REGISTRATION_JACKS = {
    SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY};

    // The MAC of the PDN Connected on `jack` whose context reports no applied
    // profile; nullptr for anything else.
    const uint8_t* getUnregisteredPdnMac(SerialIdentifier jack) const {
        PlayerProfile profile{};
        if (remoteDeviceCoordinator->getJackStatus(jack) != JackStatus::CONNECTED ||
            !remoteDeviceCoordinator->getPeerPlayerProfile(jack, profile) ||
            profile.registered != 0) {
            return nullptr;
        }
        return remoteDeviceCoordinator->getPeerMac(jack);
    }

    // True while `pdn` is still the PDN Connected on its jack.
    bool isStillCabled(const QueuedPdn& pdn) const {
        const uint8_t* mac = remoteDeviceCoordinator->getPeerMac(pdn.jack);
        return remoteDeviceCoordinator->getJackStatus(pdn.jack) == JackStatus::CONNECTED &&
               mac != nullptr && memcmp(mac, pdn.mac.data(), pdn.mac.size()) == 0;
    }

    bool isLookingUp() const {
        return phase == LookupPhase::CLOSING_TURN || phase == LookupPhase::AWAITING_SERVER ||
               phase == LookupPhase::AWAITING_JACK;
    }

    // Drops PDNs that left or registered, then queues newly cabled unregistered
    // ones at the back; the PDN of a running lookup rejoins once it ends.
    void updateQueue() {
        size_t kept = 0;
        for (size_t i = 0; i < queueLength; ++i) {
            const uint8_t* mac = getUnregisteredPdnMac(queue[i].jack);
            if (mac != nullptr && memcmp(mac, queue[i].mac.data(), queue[i].mac.size()) == 0) {
                queue[kept++] = queue[i];
            }
        }
        queueLength = kept;
        for (SerialIdentifier jack : REGISTRATION_JACKS) {
            const uint8_t* mac = getUnregisteredPdnMac(jack);
            if (mac == nullptr) continue;
            bool queued = false;
            for (size_t i = 0; i < queueLength; ++i) {
                if (queue[i].jack == jack) queued = true;
            }
            const bool inLookup = isLookingUp() && lookupPdn.jack == jack &&
                                  memcmp(mac, lookupPdn.mac.data(), lookupPdn.mac.size()) == 0;
            if (queued || inLookup) continue;
            queue[queueLength].jack = jack;
            memcpy(queue[queueLength].mac.data(), mac, queue[queueLength].mac.size());
            queueLength++;
        }
    }

    // A context goes to a radio MAC, and a tap's key to a jack: with the head's
    // MAC heard on both jacks (a peer claiming another's MAC in its HELLO), a
    // turn told to that MAC could be tapped for the other jack, so none opens.
    bool isTurnOpen() const {
        if (!registrationScreenIsShown || !readerResponds || phase != LookupPhase::IDLE ||
            queueLength == 0) {
            return false;
        }
        size_t jacksWithHeadMac = 0;
        for (SerialIdentifier jack : REGISTRATION_JACKS) {
            const uint8_t* mac = remoteDeviceCoordinator->getPeerMac(jack);
            if (mac != nullptr && memcmp(mac, queue[0].mac.data(), queue[0].mac.size()) == 0) {
                jacksWithHeadMac++;
            }
        }
        return jacksWithHeadMac == 1;
    }

    // The queue follows the jack events, so its head is still the peer on its jack.
    bool isTurnOpenFor(const uint8_t* peerMac) const {
        return isTurnOpen() && memcmp(queue[0].mac.data(), peerMac, queue[0].mac.size()) == 0;
    }

    void announceTurn() {
        const std::optional<QueuedPdn> turn =
        isTurnOpen() ? std::optional<QueuedPdn>(queue[0]) : std::nullopt;
        if (turn == announcedTurn) return;
        announcedTurn = turn;
        if (turn) {
            pendingTimer.setTimer(REGISTRATION_PENDING_MS);
        } else {
            pendingTimer.invalidate();
        }
        remoteDeviceCoordinator->resendContext();
    }

    RegistrationView getView() const {
        RegistrationView view;
        if (isLookingUp()) {
            view.screen = RegistrationScreen::LOOKING_UP;
            view.jack = lookupPdn.jack;
        } else if (phase == LookupPhase::HOLDING_RESULT) {
            view = heldResult;
        } else if (queueLength > 0) {
            view.screen =
            readerResponds ? RegistrationScreen::TAP_BAND : RegistrationScreen::NO_READER;
            view.jack = queue[0].jack;
        }
        return view;
    }

    void publishView() {
        const RegistrationView view = getView();
        if (view == announcedView) return;
        announcedView = view;
        if (onViewChanged) onViewChanged(announcedView);
    }

    // Binds the tap to the PDN holding the turn as the queue stands now. The
    // lookup starts once the context closing the turn has reached that PDN.
    void onBandTap(const BandUid& uid) {
        if (!isTurnOpen()) {
            LOG_I("FDN_PROVISION", "Band tap ignored: no turn open");
            return;
        }
        if (!uid.hasLookupLength()) {
            LOG_W("FDN_PROVISION", "Band tap ignored: a UID of %u bytes",
                  static_cast<unsigned>(uid.length));
            return;
        }
        lookupPdn = queue[0];
        queue[0] = queue[1];
        queueLength--;
        lookupUid = uid;
        phase = LookupPhase::CLOSING_TURN;
        phaseTimer.setTimer(TURN_CLOSE_MS);
    }

    void startLookup() {
        phase = LookupPhase::AWAITING_SERVER;
        phaseTimer.setTimer(LOOKUP_TIMEOUT_MS);
        PlayerApi::getDevicePlayerByBand(
        wirelessManager, lookupUid,
        [this](ProvisionLookupResult result, const Player& player, const RecordKey& key) {
            onLookupAnswer(result, player, key);
        });
        // The UID is held only for its lookup; the request carries it from here.
        secureWipe(lookupUid.bytes.data(), lookupUid.bytes.size());
    }

    // Leaves AWAITING_SERVER before the radio goes back: giving Wi-Fi up fails a
    // request still queued, synchronously, and that answer must find the lookup over.
    void giveRadioBack() {
        phase = LookupPhase::HOLDING_RESULT;
        wirelessManager->enablePeerCommsMode();
    }

    void onLookupAnswer(ProvisionLookupResult result, const Player& player, const RecordKey& key) {
        if (phase != LookupPhase::AWAITING_SERVER) return;
        // The key goes over the jack, but the reply after it needs the radio.
        giveRadioBack();
        switch (result) {
            case ProvisionLookupResult::KEY_ISSUED: break;
            case ProvisionLookupResult::KEY_NOT_ISSUED:
                endLookup(RegistrationOutcome::KEY_NOT_ISSUED);
                return;
            case ProvisionLookupResult::NO_PLAYER:
                endLookup(RegistrationOutcome::NO_PLAYER);
                return;
            case ProvisionLookupResult::SERVER_NOT_TRUSTED:
                endLookup(RegistrationOutcome::SERVER_NOT_TRUSTED);
                return;
            case ProvisionLookupResult::LOOKUP_FAILED:
                endLookup(RegistrationOutcome::LOOKUP_FAILED);
                return;
        }
        if (!isStillCabled(lookupPdn)) {
            LOG_E("FDN_PROVISION", "PDN left during its lookup: no key written");
            endLookup(RegistrationOutcome::PDN_LEFT);
            return;
        }
        // The coordinator wipes its copy once written or dropped; `key` is wiped by the lookup.
        if (!remoteDeviceCoordinator->sendApplicationFrame(
            lookupPdn.jack, ApplicationFrameKind::RECORD_KEY, key.getBytes(), RECORD_KEY_SIZE)) {
            endLookup(RegistrationOutcome::LOOKUP_FAILED);
            return;
        }
        pendingProfile = PlayerProvisionCodec::encode(player);
        // The coordinator reports every frame's outcome before another peer can
        // be Connected on the jack, so this wait needs no deadline.
        phase = LookupPhase::AWAITING_JACK;
    }

    void onKeyFrameOutcome(SerialIdentifier jack, ApplicationFrameOutcome outcome) {
        if (phase != LookupPhase::AWAITING_JACK || jack != lookupPdn.jack) return;
        // A frame the link went down on mid-write is reported written, to a PDN that is gone.
        if (outcome == ApplicationFrameOutcome::WRITTEN && isStillCabled(lookupPdn)) {
            replyChannel.sendReliable(lookupPdn.mac.data(), pendingProfile);
            endLookup(RegistrationOutcome::REGISTERED);
            return;
        }
        LOG_E("FDN_PROVISION", "PDN left before its record key was written");
        endLookup(RegistrationOutcome::PDN_LEFT);
    }

    void endLookup(RegistrationOutcome outcome) {
        phase = LookupPhase::HOLDING_RESULT;
        phaseTimer.setTimer(LOOKUP_RESULT_HOLD_MS);
        heldResult = RegistrationView{};
        heldResult.screen = RegistrationScreen::RESULT;
        heldResult.jack = lookupPdn.jack;
        heldResult.outcome = outcome;
        if (outcome == RegistrationOutcome::REGISTERED) {
            memcpy(heldResult.playerName, pendingProfile.playerName, sizeof(heldResult.playerName));
        } else if (isStillCabled(lookupPdn)) {
            PlayerProvisionPacket failed{};
            failed.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
            replyChannel.sendReliable(lookupPdn.mac.data(), failed);
        }
        pendingProfile = PlayerProvisionPacket{};
        updateQueue();
    }

    WirelessManager* wirelessManager = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    BandReader* bandReader = nullptr;
    // Declared before the channel, which holds it and is destroyed first.
    Resender resender;
    ReliableChannel<PlayerProvisionPacket> replyChannel;
    std::array<QueuedPdn, REGISTRATION_JACKS.size()> queue{};
    size_t queueLength = 0;
    bool registrationScreenIsShown = false;
    bool readerResponds = false;
    LookupPhase phase = LookupPhase::IDLE;
    // The deadline of the phase that has one: closing the turn, the server, the hold.
    SimpleTimer phaseTimer;
    QueuedPdn lookupPdn;
    // Held from the tap until its request is queued.
    BandUid lookupUid;
    PlayerProvisionPacket pendingProfile{};
    RegistrationView heldResult;
    SimpleTimer pendingTimer;
    std::optional<QueuedPdn> announcedTurn;
    RegistrationView announcedView;
    OnViewChanged onViewChanged;
};
