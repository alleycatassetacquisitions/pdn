#pragma once

#include <cstddef>
#include <cstdint>

#include "device/drivers/peer-comms-types.hpp"

// Shootout wire payloads, one PktType per command, ridden on channels owned by
// ShootoutManager. seqId is nonzero on a reliable fan-out and 0 on a frame that
// is rebroadcast on a timer instead.

// Fixed on-wire name length for CONFIRM. Local Player names are null-padded or
// truncated to this size.
constexpr size_t SHOOTOUT_NAME_LENGTH = 12;

// The ring as of closure (kShootoutRingClosed). Sized to the chain cap so one
// frame always holds it; `count` says how many entries are live, the rest are zero.
struct ShootoutRosterPayload {
    uint8_t seqId;
    uint8_t count;
    uint8_t macs[MAX_CHAIN_MEMBERS][6];
} __attribute__((packed));

// A member's press during the proposal, rebroadcast once a second until the
// bracket is drawn. The sender is the frame's source MAC.
struct ShootoutConfirmPayload {
    uint8_t seqId;
    char name[SHOOTOUT_NAME_LENGTH];
} __attribute__((packed));
