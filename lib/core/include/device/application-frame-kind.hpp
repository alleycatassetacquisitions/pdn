#pragma once

#include <cstddef>
#include <cstdint>

// The kind byte of a serial application frame. Both ends of a jack, often
// different firmware (an FDN and a PDN), must agree on each value, so every
// kind is listed here once, as PktType lists ESP-NOW packets. The serial parser
// carries the byte without reading it; the coordinator routes on it.
enum class ApplicationFrameKind : uint8_t {
    RECORD_KEY = 0,    // FDN to PDN at registration: the player's record key
    MATCH_SECRET = 1,  // Hunter to Bounty: the match ID and the per-match secret
};

// No kind value outside the enum exists, so kind-indexed tables cannot be overrun.
constexpr size_t APPLICATION_FRAME_KIND_COUNT =
static_cast<size_t>(ApplicationFrameKind::MATCH_SECRET) + 1;
