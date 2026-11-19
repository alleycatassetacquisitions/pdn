#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "utils/uuid-bytes.hpp"
#include "wireless/match-packet.hpp"

// Per-match secret the Hunter draws from the hardware RNG and sends to the
// opponent jack; both PDNs sign that standalone match's frames with it.
inline constexpr size_t MATCH_SECRET_SIZE = 16;
using MatchSecret = std::array<uint8_t, MATCH_SECRET_SIZE>;

// An ApplicationFrameKind::MATCH_SECRET frame: the match ID's 16 bytes, then the secret.
inline constexpr size_t MATCH_SECRET_FRAME_LENGTH = UUIDBytes::SIZE + MATCH_SECRET_SIZE;

// One standalone match's frame signing: the secret, the counter of the last
// frame signed and that of the last opponent frame admitted. The secret is
// wiped when the value goes.
class MatchFrameAuth {
public:
    /** Signs and checks under @p secret; opponent frames must count above @p opponentCounter. */
    explicit MatchFrameAuth(const MatchSecret& secret, uint32_t opponentCounter = 0);
    /** Wipes the secret. */
    ~MatchFrameAuth();
    /** A copy carries the secret and both counters, and wipes its secret in turn. */
    MatchFrameAuth(const MatchFrameAuth& other) = default;
    /** Takes @p other's secret and counters. */
    MatchFrameAuth& operator=(const MatchFrameAuth& other) = default;

    /**
     * Gives @p packet the next counter and its tag: the first MATCH_TAG_SIZE
     * bytes of HMAC-SHA-256 under the secret over the packet with seqId and tag
     * zeroed (the channel stamps seqId after signing).
     */
    void sign(MatchPacket& packet);

    /**
     * True when @p packet is the opponent's: it names the other draw slot from
     * @p localIsHunter, its tag verifies and its counter is above every one
     * admitted before. Only then is its counter recorded; a refused packet
     * changes nothing.
     */
    bool admit(const MatchPacket& packet, bool localIsHunter);

private:
    MatchSecret secret;
    uint32_t sentCounter = 0;
    uint32_t admittedCounter;
};
