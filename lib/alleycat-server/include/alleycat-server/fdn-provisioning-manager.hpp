#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include "alleycat-server/player-api.hpp"
#include "alleycat-server/player-provision-codec.hpp"
#include "device/application-frame-kind.hpp"
#include "device/drivers/band-reader.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-types.hpp"
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

/** The registration screen's facts; `result`, `registered` and `playerName` only for RESULT. */
struct RegistrationView {
    RegistrationScreen screen = RegistrationScreen::NONE;
    SerialIdentifier jack = SerialIdentifier::INPUT_JACK;
    ProvisionLookupResult result = ProvisionLookupResult::LOOKUP_FAILED;
    // The PDN got its record key and then its profile.
    bool registered = false;
    char playerName[PlayerLimits::NAME_BUFFER_SIZE] = {};

    /** @return true when both would draw the same screen. */
    bool operator==(const RegistrationView& other) const {
        return screen == other.screen && jack == other.jack && result == other.result &&
               registered == other.registered && strcmp(playerName, other.playerName) == 0;
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
    if (view.registered) return {"WELCOME", view.playerName};
    switch (view.result) {
        // A key was issued, but its PDN left before it got the key and profile.
        case ProvisionLookupResult::KEY_ISSUED: return {"PDN LEFT", jackLabel};
        case ProvisionLookupResult::KEY_NOT_ISSUED: return {"UNABLE TO", "ISSUE KEY"};
        case ProvisionLookupResult::NO_PLAYER: return {"SEE DESK", jackLabel};
        case ProvisionLookupResult::SERVER_NOT_TRUSTED: return {"SERVER", "NOT TRUSTED"};
        case ProvisionLookupResult::LOOKUP_FAILED: break;
    }
    return {"UNABLE TO", "LOCATE ASSET"};
}

/**
 * FDN side of registration. Each PDN Connected on an input jack whose context
 * reports no applied profile is queued in arrival order, learnt from the
 * coordinator's jack and context events. The queue's head holds the turn while
 * the registration screen is shown, the band reader answers and no lookup or
 * result hold runs; every context the FDN sends tells its PDN whether it holds
 * the turn, and the contexts go again whenever the turn opens, moves or closes.
 * A band tapped while the turn is open is looked up for that PDN, by its MAC.
 * With a record key in the answer, the key goes over that PDN's jack and, once
 * written, the profile goes to that MAC over the radio; anything else registers
 * nobody, and a PDN still cabled is told the lookup failed. Each lookup ends in
 * a LOOKUP_RESULT_HOLD_MS hold during which taps are ignored. Lives as long as
 * the FDN: a lookup's HTTP callback holds this manager.
 */
class FdnProvisioningManager {
public:
    /** Told of every change to what the registration screen shows. */
    using OnViewChanged = std::function<void(const RegistrationView& view)>;

    /** How long the turn stays with one PDN, with no tap, while another waits. */
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
    }

    /**
     * Pumps the reply's retries, reads whether the reader answers, ends a lookup
     * past LOOKUP_TIMEOUT_MS and a hold past LOOKUP_RESULT_HOLD_MS, moves a turn
     * held REGISTRATION_PENDING_MS with no tap to the next PDN, then resends the
     * contexts if the turn changed and tells the screen if its view did. Call
     * every tick.
     */
    void sync() {
        resender.sync();
        readerResponds = bandReader != nullptr && bandReader->isResponding();
        if (phase != LookupPhase::IDLE && lookupTimer.expired()) {
            LOG_W("FDN_PROVISION", "Band lookup timed out");
            endLookup(ProvisionLookupResult::LOOKUP_FAILED, false);
        }
        if (holdingResult && resultHoldTimer.expired()) {
            holdingResult = false;
            resultHoldTimer.invalidate();
        }
        if (isTurnOpen() && queueLength > 1 && pendingTimer.expired()) {
            LOG_I("FDN_PROVISION", "No tap in time; the next PDN takes the turn");
            const QueuedPdn head = queue[0];
            queue[0] = queue[1];
            queue[1] = head;
        }
        announceTurn();
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

    /** @return true while a PDN is queued, a lookup runs or its result holds. */
    bool hasRegistrationWork() const {
        return queueLength > 0 || phase != LookupPhase::IDLE || holdingResult;
    }

private:
    enum class LookupPhase : uint8_t {
        IDLE,
        // The request is with the server; the lookup owns the radio for Wi-Fi.
        AWAITING_SERVER,
        // The record key frame waits for its outcome on the lookup's jack.
        AWAITING_JACK,
    };

    // A PDN by its jack and its MAC there, so another PDN on the same jack is
    // another registration.
    struct QueuedPdn {
        SerialIdentifier jack = SerialIdentifier::INPUT_JACK;
        std::array<uint8_t, 6> mac{};
    };

    static constexpr std::array<SerialIdentifier, 2> REGISTRATION_JACKS = {
    SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY};

    // The MAC of the PDN Connected on `jack` whose context reports no applied
    // profile; nullptr for anything else.
    const uint8_t* getUnregisteredPdnMac(SerialIdentifier jack) const {
        if (remoteDeviceCoordinator->getJackStatus(jack) != JackStatus::CONNECTED ||
            remoteDeviceCoordinator->getPeerDeviceType(jack) != DeviceType::PDN) {
            return nullptr;
        }
        size_t length = 0;
        const uint8_t* bytes = remoteDeviceCoordinator->getPeerProfile(jack, length);
        if (bytes == nullptr || length < sizeof(PlayerProfile)) return nullptr;
        PlayerProfile profile{};
        memcpy(&profile, bytes, sizeof(profile));
        if (profile.registered != 0) return nullptr;
        return remoteDeviceCoordinator->getPeerMac(jack);
    }

    // True while `pdn` is still the PDN Connected on its jack.
    bool isStillCabled(const QueuedPdn& pdn) const {
        const uint8_t* mac = remoteDeviceCoordinator->getPeerMac(pdn.jack);
        return remoteDeviceCoordinator->getJackStatus(pdn.jack) == JackStatus::CONNECTED &&
               mac != nullptr && memcmp(mac, pdn.mac.data(), pdn.mac.size()) == 0;
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
            const bool inLookup = phase != LookupPhase::IDLE && lookupPdn.jack == jack &&
                                  memcmp(mac, lookupPdn.mac.data(), lookupPdn.mac.size()) == 0;
            if (queued || inLookup) continue;
            queue[queueLength].jack = jack;
            memcpy(queue[queueLength].mac.data(), mac, queue[queueLength].mac.size());
            queueLength++;
        }
    }

    bool isTurnOpen() const {
        return registrationScreenIsShown && readerResponds && phase == LookupPhase::IDLE &&
               !holdingResult && queueLength > 0;
    }

    bool isTurnOpenFor(const uint8_t* peerMac) const {
        return isTurnOpen() && memcmp(queue[0].mac.data(), peerMac, queue[0].mac.size()) == 0;
    }

    void announceTurn() {
        const bool turnOpen = isTurnOpen();
        const bool moved =
        turnOpen && (announcedTurn.jack != queue[0].jack || announcedTurn.mac != queue[0].mac);
        if (turnOpen == announcedTurnOpen && !moved) return;
        announcedTurnOpen = turnOpen;
        announcedTurn = queue[0];
        if (turnOpen) {
            pendingTimer.setTimer(REGISTRATION_PENDING_MS);
        } else {
            pendingTimer.invalidate();
        }
        remoteDeviceCoordinator->resendContext();
    }

    RegistrationView getView() const {
        RegistrationView view;
        if (phase != LookupPhase::IDLE) {
            view.screen = RegistrationScreen::LOOKING_UP;
            view.jack = lookupPdn.jack;
        } else if (holdingResult) {
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

    // Binds the tap to the PDN holding the turn as the queue stands now.
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
        phase = LookupPhase::AWAITING_SERVER;
        lookupTimer.setTimer(LOOKUP_TIMEOUT_MS);
        const uint32_t number = ++lookupNumber;
        PlayerApi::getDevicePlayerByBand(
        wirelessManager, uid,
        [this, number](ProvisionLookupResult result, const Player& player, const RecordKey& key) {
            onLookupAnswer(number, result, player, key);
        });
    }

    void onLookupAnswer(uint32_t number, ProvisionLookupResult result, const Player& player,
                        const RecordKey& key) {
        // An answer after its lookup timed out finds the radio already given back.
        if (phase != LookupPhase::AWAITING_SERVER || number != lookupNumber) return;
        // The key goes over the jack, but the reply after it needs the radio.
        wirelessManager->enablePeerCommsMode();
        phase = LookupPhase::AWAITING_JACK;
        if (result != ProvisionLookupResult::KEY_ISSUED) {
            endLookup(result, false);
            return;
        }
        if (!isStillCabled(lookupPdn)) {
            LOG_E("FDN_PROVISION", "PDN left during its lookup: no key written");
            endLookup(result, false);
            return;
        }
        // The coordinator wipes its copy once written or dropped; `key` is wiped by the lookup.
        if (!remoteDeviceCoordinator->sendApplicationFrame(
            lookupPdn.jack, ApplicationFrameKind::RECORD_KEY, key.getBytes(), RECORD_KEY_SIZE)) {
            endLookup(result, false);
            return;
        }
        pendingProfile = PlayerProvisionCodec::encode(player);
    }

    void onKeyFrameOutcome(SerialIdentifier jack, ApplicationFrameOutcome outcome) {
        if (phase != LookupPhase::AWAITING_JACK || jack != lookupPdn.jack) return;
        // A frame the link went down on mid-write is reported written, to a PDN that is gone.
        const bool delivered =
        outcome == ApplicationFrameOutcome::WRITTEN && isStillCabled(lookupPdn);
        if (delivered) {
            replyChannel.sendReliable(lookupPdn.mac.data(), pendingProfile);
        } else {
            LOG_E("FDN_PROVISION", "PDN left before its record key was written");
        }
        endLookup(ProvisionLookupResult::KEY_ISSUED, delivered);
    }

    void endLookup(ProvisionLookupResult result, bool registered) {
        if (phase == LookupPhase::AWAITING_SERVER) wirelessManager->enablePeerCommsMode();
        phase = LookupPhase::IDLE;
        lookupTimer.invalidate();
        heldResult = RegistrationView{};
        heldResult.screen = RegistrationScreen::RESULT;
        heldResult.jack = lookupPdn.jack;
        heldResult.result = result;
        heldResult.registered = registered;
        if (registered) {
            memcpy(heldResult.playerName, pendingProfile.playerName, sizeof(heldResult.playerName));
        } else if (isStillCabled(lookupPdn)) {
            PlayerProvisionPacket failed{};
            failed.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
            replyChannel.sendReliable(lookupPdn.mac.data(), failed);
        }
        pendingProfile = PlayerProvisionPacket{};
        holdingResult = true;
        resultHoldTimer.setTimer(LOOKUP_RESULT_HOLD_MS);
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
    // Ties an HTTP answer to the lookup that asked.
    uint32_t lookupNumber = 0;
    QueuedPdn lookupPdn;
    PlayerProvisionPacket pendingProfile{};
    SimpleTimer lookupTimer;
    SimpleTimer resultHoldTimer;
    bool holdingResult = false;
    RegistrationView heldResult;
    SimpleTimer pendingTimer;
    bool announcedTurnOpen = false;
    QueuedPdn announcedTurn;
    RegistrationView announcedView;
    OnViewChanged onViewChanged;
};
