#include "wireless/match-packet.hpp"

#include "device/drivers/peer-comms-types.hpp"
#include "utils/constant-time-equal.hpp"
#include "utils/hmac-sha256.hpp"
#include "utils/secure-wipe.hpp"

#include <cstring>

static_assert(MATCH_TAG_SIZE <= HMAC_SHA256_SIZE, "a match tag is a prefix of the HMAC output");
static_assert(MATCH_KEY_SIZE > 0, "the HMAC refuses only an empty key or a null message");

namespace {

// The packet type byte binds the tag to kMatchCommand; seqId is left out
// because the channel stamps it after signing, and the tag cannot cover itself.
// @p tag may be @p packet's own: the packet is copied before the tag is written.
void computeMatchPacketTag(const MatchKey& key, const MatchPacket& packet,
                           uint8_t (&tag)[MATCH_TAG_SIZE]) {
    uint8_t message[1 + sizeof(MatchPacket)];
    message[0] = static_cast<uint8_t>(PktType::kMatchCommand);
    MatchPacket* body = reinterpret_cast<MatchPacket*>(&message[1]);
    memcpy(body, &packet, sizeof(MatchPacket));
    body->seqId = 0;
    memset(body->tag, 0, sizeof(body->tag));

    uint8_t hmacOutput[HMAC_SHA256_SIZE] = {};
    // Cannot fail: the key is never empty and the message is a local buffer.
    static_cast<void>(
    computeHmacSha256(key.data(), key.size(), message, sizeof(message), hmacOutput));
    memcpy(tag, hmacOutput, MATCH_TAG_SIZE);
    // The bytes past the tag are HMAC output nobody is meant to see.
    secureWipe(hmacOutput, sizeof(hmacOutput));
}

}  // namespace

void signMatchPacket(const MatchKey& key, MatchPacket& packet) {
    computeMatchPacketTag(key, packet, packet.tag);
}

bool matchPacketTagVerifies(const MatchKey& key, const MatchPacket& packet) {
    uint8_t expected[MATCH_TAG_SIZE] = {};
    computeMatchPacketTag(key, packet, expected);
    const bool verifies = constantTimeEqual(expected, packet.tag, MATCH_TAG_SIZE);
    secureWipe(expected, sizeof(expected));
    return verifies;
}
