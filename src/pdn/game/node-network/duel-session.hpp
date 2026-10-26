#pragma once

#include <cstdint>

#include "alleycat-server/quickdraw-match-codec.hpp"
#include "utils/uuid-bytes.hpp"

/** Which end of the cable this device is: OUTPUT jack initiates, INPUT jack responds. */
enum class DuelLinkSide {
    INITIATOR,
    RESPONDER,
};

/** One duelist as the server knows them. A nil team id means no team. */
struct DuelParticipant {
    UUIDBytes playerId;
    UUIDBytes teamId;
};

/** State of a single duel, from handshake to upload record. Holds no policy about sync. */
class DuelSession {
public:
    /** Starts a session; clears any earlier state. */
    void prime(DuelLinkSide localSide, const UUIDBytes& matchId, const uint8_t* opponentMac,
               const DuelParticipant& localParticipant, int64_t occurredAtUnixMs);

    /** Returns to the inactive state. */
    void clear();

    bool isActive() const;
    DuelLinkSide getLocalSide() const;
    const UUIDBytes& getMatchId() const;
    const uint8_t* getOpponentMac() const;

    /** @return true when @p mac is the opponent's and @p matchId is this session's match. */
    bool isFromOpponent(const uint8_t* mac, const UUIDBytes& matchId) const;

    const DuelParticipant& getLocalParticipant() const;
    const DuelParticipant& getOpponentParticipant() const;
    bool hasOpponentParticipant() const;
    void setOpponentParticipant(const DuelParticipant& participant);

    bool isHandshakeComplete() const;
    void markHandshakeComplete();

    int64_t getOccurredAtUnixMs() const;
    void setOccurredAtUnixMs(int64_t occurredAtUnixMs);

    /** @return false and keeps the first value when the local time was already recorded. */
    bool recordLocalDrawTimeMs(uint64_t drawTimeMs);

    /** @return false and keeps the first value when the opponent's time was already recorded. */
    bool recordRemoteDrawTimeMs(uint64_t drawTimeMs);

    bool hasLocalDrawTime() const;
    bool hasRemoteDrawTime() const;
    bool hasInitiatorDrawTime() const;
    bool hasResponderDrawTime() const;
    uint64_t getInitiatorDrawTimeMs() const;
    uint64_t getResponderDrawTimeMs() const;

    void setOpponentNeverPressed();
    void setGraceExpiredWithoutPress();
    bool didOpponentNeverPress() const;
    bool didGraceExpireWithoutPress() const;

    /** @return true when both times are known or the outcome is already decided as a miss. */
    bool isExchangeComplete() const;

    /**
     * Builds the upload record with the initiator as player A.
     *
     * @return false, leaving @p record unchanged, unless the handshake is complete, both times are
     *         present, and both player ids are non-nil.
     */
    bool tryBuildUploadRecord(QuickdrawMatchRecord& record) const;

private:
    bool hasDrawTime(DuelLinkSide side) const;
    uint64_t getDrawTime(DuelLinkSide side) const;
    bool recordDrawTime(DuelLinkSide side, uint64_t drawTimeMs);
    DuelLinkSide getRemoteSide() const;

    bool active = false;
    bool handshakeComplete = false;
    bool opponentParticipantKnown = false;
    bool opponentNeverPressed = false;
    bool graceExpiredWithoutPress = false;
    bool drawTimePresent[2] = {false, false};
    uint64_t drawTimes[2] = {0, 0};
    DuelLinkSide localSide = DuelLinkSide::INITIATOR;
    UUIDBytes matchId;
    uint8_t opponentMac[6] = {};
    DuelParticipant localParticipant;
    DuelParticipant opponentParticipant;
    int64_t occurredAtUnixMs = 0;
};
