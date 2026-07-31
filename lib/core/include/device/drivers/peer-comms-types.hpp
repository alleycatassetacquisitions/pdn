#pragma once

#include <cstdint>

//PktType determines which callback will handle the packet on the receiving end
enum class PktType : uint8_t {
    kPlayerInfoBroadcast = 0,
    kQuickdrawCommand = 1,
    kDebugPacket = 2,
    kChainGameEvent = 6,
    kChainConfirm = 7,
    kRoleAnnounce = 8,
    kRoleAnnounceAck = 9,
    kChainGameEventAck = 10,
    kShootoutCommand = 11,
    kShootoutCommandAck = 12,
    kSymbolMatchCommand = 13,
    kFdnConnect = 14,
    kPdnConnectionContext = 15,
    kFdnConnectionContext = 16,
    kConnectionAnnounce = 17,
    kDisconnectReport = 18,
    kHeadTransfer = 19,
    // The lint runs only on lines a commit adds, so this new member trips the
    // UPPER_CASE enum-constant rule its grandfathered siblings never see. It keeps
    // their kFoo spelling rather than splitting the enum across two conventions.
    // NOLINTNEXTLINE(readability-identifier-naming)
    kChainJoin = 20,
    kNumPacketTypes  // Not a real packet type, DO NOT USE
};

// A player's identity + game state, exchanged once per jack connection so a
// neighbour knows who it is plugged into. Packed: the struct layout IS the
// wire format (both ends run the same firmware). RDC never reads these fields —
// it forwards the profile to the game layer opaquely.
struct PlayerProfile {
    uint16_t userId;   // 0xFFFF while unregistered
    uint8_t gameRole;  // 1 = hunter, 0 = target/bounty, as in RoleAnnouncePayload
    uint8_t allegiance;
    char faction[8];
    char name[16];
} __attribute__((packed));

// PDN-to-neighbour connection context. seqId is stamped by the ReliableChannel.
// chainRole is the sender's own ChainRole as of the send (0 = standalone), recorded
// by the receiver. The peer's device kind arrives out-of-band in HELLO and picks
// which context struct to decode.
struct PdnConnectionContext {
    uint8_t seqId;
    uint8_t chainRole;
    PlayerProfile player;
} __attribute__((packed));

// FDN self-description. ponytail: fields TBD (#156-era); one reserved byte keeps
// the struct non-empty and packed until the FDN profile is designed.
struct FdnProfile {
    uint8_t reserved;
} __attribute__((packed));

// FDN-to-neighbour connection context; same header as PdnConnectionContext, FDN body.
struct FdnConnectionContext {
    uint8_t seqId;
    uint8_t chainRole;
    FdnProfile fdn;
} __attribute__((packed));

struct DataPktHdr
{
    // Total packet length including header. Wide enough for a full ESP-NOW v2
    // frame: the fleet is single-firmware on IDF 5.5, so every link negotiates
    // v2 and a payload can exceed 255 bytes.
    uint16_t pktLen;
    PktType packetType;
} __attribute__((packed));

struct ChainConfirmPayload
{
    uint8_t originatorMac[6];
    uint8_t seqId;
} __attribute__((packed));

// Sent by a supporter straight to the champion whose MAC reached it through the
// role-announce cascade. The cascade only ever travels downstream, so this is
// the sole evidence a champion gets that a device more than one cable away
// follows it — and the only thing that lets that device's press count.
// championMac is the champion the sender believes in, so a frame that arrives
// by radio accident cannot enrol anyone in the wrong chain's roster.
struct ChainJoinPayload {
    uint8_t championMac[6];
} __attribute__((packed));

struct RoleAnnouncePayload
{
    uint8_t role;               // 1 = hunter, 0 = target/bounty
    uint8_t championMac[6];
    uint8_t seqId;
} __attribute__((packed));

struct RoleAnnounceAckPayload
{
    uint8_t seqId;
} __attribute__((packed));

struct ChainGameEventAckPayload
{
    uint8_t seqId;
} __attribute__((packed));

enum class ShootoutCmd : uint8_t
{
    CONFIRM = 0,
    BRACKET = 1,
    MATCH_START = 2,
    MATCH_RESULT = 3,
    TOURNAMENT_END = 4,
    PEER_LOST = 5,
    ABORT = 6,
};

struct ShootoutPacket
{
    ShootoutCmd cmd;
    uint8_t     seqId;   // nonzero for reliable commands; 0 = no ack expected
    uint8_t     payload[];
} __attribute__((packed));

struct ShootoutAckPayload
{
    ShootoutCmd cmd;
    uint8_t     seqId;
} __attribute__((packed));

// ---- Head roster management (#158) ----
// Point-to-point ReliableChannel payloads; the chain head is the sole consumer.
// seqId is stamped by the channel on send.

// Head-roster capacity, sized to the event envelope rather than to a frame: a
// single ESP-NOW v2 HeadTransfer frame holds well over a hundred (member,
// upstream) pairs, so framing is not what bounds this.
//
// Overflow contract: past this cap the head drops the announce, and the member
// cannot learn that. SEND_SUCCESS on the announce is the only delivery signal
// the no-ack design has, and the radio acks a frame the head then discards, so
// the member's HELLO confirmed bit rises while it is absent from the roster.
// The cap is set past any real chain so the divergence is unreachable in
// practice; closing it would need an admission reply the design excludes.
constexpr uint8_t MAX_CHAIN_MEMBERS = 64;

// Sent by a newly joined member directly to the head it read from its upstream
// neighbour's HELLO. upstreamMac names the sender's direct upstream (INPUT) peer.
struct ConnectionAnnouncePayload {
    uint8_t seqId;
    uint8_t upstreamMac[6];
} __attribute__((packed));

// Sent to the head by the upstream neighbour of a departed member — the only
// node that observes the HELLO timeout of a non-adjacent (to the head) link.
struct DisconnectReportPayload {
    uint8_t seqId;
    uint8_t disconnectedMac[6];
} __attribute__((packed));

// Sent once by a demoted head to its successor; the only place a member list
// ever travels, and it is a single unicast. Entries are (member, upstream)
// pairs — memberMacs[i]'s recorded direct upstream is upstreamMacs[i] — so the
// receiver keeps the chain's true order instead of flattening every
// transferred member onto the demoted head. At MAX_CHAIN_MEMBERS=64 this is
// 770 bytes. The ceiling is the driver's reliable-payload budget
// (MAX_PKT_DATA_SIZE, one ESP-NOW v2 frame), asserted where the driver includes
// this header.
struct HeadTransferPayload {
    uint8_t seqId;
    uint8_t memberCount;
    uint8_t memberMacs[MAX_CHAIN_MEMBERS][6];
    uint8_t upstreamMacs[MAX_CHAIN_MEMBERS][6];
} __attribute__((packed));
