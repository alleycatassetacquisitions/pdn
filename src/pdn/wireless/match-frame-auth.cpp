#include "wireless/match-frame-auth.hpp"

#include "utils/constant-time-equal.hpp"
#include "utils/hmac-sha256.hpp"
#include "utils/secure-wipe.hpp"

#include <cstring>

static_assert(MATCH_TAG_SIZE <= HMAC_SHA256_SIZE, "a match tag is a prefix of the HMAC output");

namespace {

// The tag covers every field but seqId, which the channel stamps after
// signing, and the tag itself.
void computeTag(const MatchSecret& secret, const MatchPacket& packet,
                uint8_t (&tag)[MATCH_TAG_SIZE]) {
    MatchPacket covered = packet;
    covered.seqId = 0;
    memset(covered.tag, 0, sizeof(covered.tag));
    uint8_t hmac[HMAC_SHA256_SIZE] = {};
    // Cannot fail: the secret is never empty and the message is a local.
    static_cast<void>(computeHmacSha256(secret.data(), secret.size(),
                                        reinterpret_cast<const uint8_t*>(&covered), sizeof(covered),
                                        hmac));
    memcpy(tag, hmac, MATCH_TAG_SIZE);
    secureWipe(hmac, sizeof(hmac));
}

}  // namespace

MatchFrameAuth::MatchFrameAuth(const MatchSecret& secret, uint32_t opponentCounter)
    : secret(secret)
    , admittedCounter(opponentCounter) {}

MatchFrameAuth::~MatchFrameAuth() { secureWipe(secret.data(), secret.size()); }

void MatchFrameAuth::sign(MatchPacket& packet) {
    packet.counter = ++sentCounter;
    computeTag(secret, packet, packet.tag);
}

bool MatchFrameAuth::admit(const MatchPacket& packet, bool localIsHunter) {
    // This device's own frame, reflected back, carries a good tag.
    if ((packet.isHunter != 0) == localIsHunter) return false;
    uint8_t expected[MATCH_TAG_SIZE] = {};
    computeTag(secret, packet, expected);
    const bool verifies = constantTimeEqual(expected, packet.tag, MATCH_TAG_SIZE);
    if (!verifies || packet.counter <= admittedCounter) return false;
    admittedCounter = packet.counter;
    return true;
}
