#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "device/remote-device-coordinator.hpp"
#include "game/player-provider.hpp"
#include "game/player-types.hpp"
#include "game/player.hpp"
#include "wireless/reliable-channel.hpp"

class WirelessManager;

/**
 * The PDN half of band registration: the record key an FDN writes over its
 * jack and the profile, or lookup failure, it sends over the radio. Both are
 * taken only while open and only from an FDN Connected on one of this PDN's
 * jacks; the first one taken binds the attempt to that FDN's MAC and jack.
 * Constructing it claims ApplicationFrameKind::RECORD_KEY on the coordinator
 * and the kPlayerProvision packet type on the radio.
 */
class BandRegistration {
public:
    /** Where the attempt opened last stands. */
    enum class Answer : uint8_t {
        NONE,     // nothing taken since open()
        PARTIAL,  // the key or the profile, not both
        FOUND,    // key and profile both held
        FAILED,   // the FDN reported a failed lookup; nothing more is taken
    };

    /** The key goes into @p playerProvider's RecordKey as it arrives. */
    BandRegistration(WirelessManager* wirelessManager,
                     RemoteDeviceCoordinator* remoteDeviceCoordinator,
                     PlayerProvider* playerProvider);
    /** Gives the record-key frame kind back to the coordinator. */
    ~BandRegistration();

    /**
     * Drops the held profile and takes pieces until close(). The key lives in
     * the provider, which the caller clears first.
     */
    void open();

    /**
     * Stops taking pieces and drops the held profile, and drops and overwrites
     * the record key unless @p keepKey (its profile was applied).
     */
    void close(bool keepKey);

    /** @return where the attempt stands. */
    Answer getAnswer() const;

    /** @return the delivered profile while the answer is FOUND, else null. */
    const Player* getFoundPlayer() const;

    /**
     * One slot, told after every change of answer. It runs inside the radio or
     * jack callback that delivered the piece, so it only records.
     */
    void setOnAnswerChanged(std::function<void()> callback);

private:
    void onRecordKey(SerialIdentifier jack, const uint8_t* payload, size_t length);
    void onProvision(const uint8_t* fromMac, const PlayerProvisionPacket& packet);
    bool isConnectedFdn(SerialIdentifier jack, const uint8_t* mac) const;
    bool bindAttempt(SerialIdentifier jack, const uint8_t* mac);
    void dropPieces();
    void setAnswer(Answer changed);

    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    PlayerProvider* playerProvider = nullptr;
    ReliableChannel<PlayerProvisionPacket> provisionChannel;
    // The FDN the first piece came from; set while attemptIsBound.
    std::array<uint8_t, 6> attemptMac{};
    SerialIdentifier attemptJack = SerialIdentifier::OUTPUT_JACK;
    bool attemptIsBound = false;
    Player heldProfile;
    bool profileIsHeld = false;
    bool isOpen = false;
    Answer answer = Answer::NONE;
    std::function<void()> onAnswerChanged;
};
