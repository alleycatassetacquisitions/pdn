#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include "device/remote-device-coordinator.hpp"
#include "game/player-provider.hpp"
#include "game/player-types.hpp"
#include "game/player.hpp"
#include "game/record-key.hpp"
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
    /** A key taken with its profile goes to @p playerProvider's RecordKey on close(true). */
    BandRegistration(WirelessManager* wirelessManager,
                     RemoteDeviceCoordinator* remoteDeviceCoordinator,
                     PlayerProvider* playerProvider);
    /** Gives the record-key frame kind back to the coordinator. */
    ~BandRegistration();

    /** Takes pieces until close() or a reported failure. */
    void open();

    /**
     * Stops taking pieces and wipes both; on @p commitKey (its profile was
     * applied) the key goes to the provider first.
     */
    void close(bool commitKey);

    /** @return the delivered profile once key and profile are both held, else null. */
    const Player* getFoundPlayer() const;

    /** @return true once the FDN reported the lookup failed; nothing more is taken. */
    bool hasFailed() const;

    /**
     * One slot, told after each piece taken. It runs inside the radio or jack
     * callback that delivered the piece, so it only records.
     */
    void setOnPieceTaken(std::function<void()> callback);

private:
    enum class Phase : uint8_t { CLOSED, OPEN, FAILED };

    void onRecordKey(SerialIdentifier jack, const uint8_t* payload, size_t length);
    void onProvision(const uint8_t* fromMac, const PlayerProvisionPacket& packet);
    bool isConnectedFdn(SerialIdentifier jack) const;
    std::optional<SerialIdentifier> fdnJackOf(const uint8_t* mac) const;
    bool bindAttempt(SerialIdentifier jack, const uint8_t* mac);
    void dropPieces();

    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    PlayerProvider* playerProvider = nullptr;
    ReliableChannel<PlayerProvisionPacket> provisionChannel;
    // The FDN the first piece came from; set while attemptIsBound.
    std::array<uint8_t, 6> attemptMac{};
    SerialIdentifier attemptJack = SerialIdentifier::OUTPUT_JACK;
    bool attemptIsBound = false;
    RecordKey pendingKey;
    Player heldProfile;
    Phase phase = Phase::CLOSED;
    std::function<void()> onPieceTaken;
};
