#pragma once

#include <cstddef>
#include <cstdint>

//PktType determines which callback will handle the packet on the receiving end
enum class PktType : uint8_t {
    kPlayerInfoBroadcast = 0,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kMatchCommand = 1,
    kDebugPacket = 2,
    kChainGameEvent = 6,
    kChainConfirm = 7,
    kRoleAnnounce = 8,
    kShootoutCommand = 11,
    kShootoutCommandAck = 12,
    kSymbolMatchCommand = 13,
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
    // Was 15 on main, where this branch had already taken that slot. Nothing
    // reads the number — one firmware version ships at a time — so it moves
    // rather than renumbering the context types every handler already uses.
    // NOLINTNEXTLINE(readability-identifier-naming)
    kCrashLog = 21,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kFirmwareUpdate = 22,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kMatchCollection = 23,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kMatchCollectionAck = 24,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kPlayerProvision = 25,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kPlayerProvisionRequest = 26,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kReliableAck = 27,
    kNumPacketTypes  // Not a real packet type, DO NOT USE
};

// Device-level chain role: HEAD has no in-peer, CHILD has one, RING is head-only
// for a closed ring (ring membership is isInRing()).
enum class ChainRole : uint8_t {
    STANDALONE = 0,
    HEAD = 1,
    CHILD = 2,
    RING = 3,
};
static_assert(sizeof(ChainRole) == 1, "ChainRole must be 1 byte on the wire");
static_assert(static_cast<uint8_t>(ChainRole::STANDALONE) == 0, "ChainRole wire value");
static_assert(static_cast<uint8_t>(ChainRole::HEAD) == 1, "ChainRole wire value");
static_assert(static_cast<uint8_t>(ChainRole::CHILD) == 2, "ChainRole wire value");
static_assert(static_cast<uint8_t>(ChainRole::RING) == 3, "ChainRole wire value");

// No peer id known: an FDN peer, an unregistered player, or no peer at all.
constexpr uint16_t PEER_USER_ID_NONE = 0xFFFF;

// Packed wire format; RDC lifts userId for getPeerUserId() and forwards the raw
// struct to the game layer.
struct PlayerProfile {
    uint16_t userId = PEER_USER_ID_NONE;
    uint8_t gameRole;  // 1 = hunter, 0 = bounty
    uint8_t allegiance;
    char faction[8];
    char name[16];
    uint8_t registered;  // 1 once a profile is applied, demo included
} __attribute__((packed));
static_assert(sizeof(PlayerProfile) == 29, "PlayerProfile wire size");

struct PdnConnectionContext {
    uint8_t seqId;
    ChainRole chainRole;
    PlayerProfile player;
} __attribute__((packed));
static_assert(sizeof(PdnConnectionContext) == 31, "PdnConnectionContext wire size");

// FDN self-description exchanged on cabled connection context. Carries the FDN's
// synchronized UTC wall clock in epoch milliseconds (or 0 when unsynchronized).
struct FdnProfile {
    int64_t utcUnixMs;
} __attribute__((packed));

// FDN-to-neighbour connection context; same header as PdnConnectionContext, FDN body.
struct FdnConnectionContext {
    uint8_t seqId;
    ChainRole chainRole;
    FdnProfile fdn;
} __attribute__((packed));

// Frozen: older builds must parse a newer build's firmware frames (this header, kFirmwareUpdate,
// FirmwareChunkHeader, FIRMWARE_CHUNK_SIZE, ESPNOW_CHANNEL) or they can only be updated over USB.
struct DataPktHdr
{
    PktType packetType;
} __attribute__((packed));
static_assert(sizeof(DataPktHdr) == 1, "older builds parse firmware frames with a one-byte header");

struct ChainConfirmPayload
{
    uint8_t originatorMac[6];
    uint8_t seqId;
} __attribute__((packed));

// Sent by a supporter straight to the challenger whose MAC reached it through the
// role-announce cascade. The cascade only ever travels downstream, so this is
// the sole evidence a challenger gets that a device more than one cable away
// follows it — and the only thing that lets that device's press count.
// challengerMac is the challenger the sender believes in, so a frame that arrives
// by radio accident cannot enrol anyone in the wrong chain's roster.
struct ChainJoinPayload {
    uint8_t challengerMac[6];
    uint8_t seqId;
} __attribute__((packed));

struct RoleAnnouncePayload
{
    uint8_t challengerMac[6];
    uint8_t seqId;
} __attribute__((packed));

// A fan-out recipient's answer to one reliable frame: names the channel the
// frame rode and the seqId it carried. Routed by the ReliableTransport, which
// is why one PktType serves every fan-out channel.
struct ReliableAckPayload {
    PktType ackedType;
    uint8_t seqId;
} __attribute__((packed));

enum class ShootoutCmd : uint8_t {
    CONFIRM = 0,
    BRACKET = 1,
    MATCH_START = 2,
    MATCH_RESULT = 3,
    TOURNAMENT_END = 4,
    ABORT = 5,
    // Coordinator -> members on ring closure. Carries the ring roster because
    // only the head's RDC serves one; every other member gets its copy here.
    RING_CLOSED = 6,
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

// ---- Head roster management ----
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

constexpr uint16_t FIRMWARE_CHUNK_SIZE = 1400;

constexpr size_t FIRMWARE_IMAGE_ID_LENGTH = 32;

/**
 * Unsigned: IDF checks the assembled image's signature, so a forged frame can spoil a download
 * or wipe the spare slot but cannot install an image.
 */
struct FirmwareChunkHeader {
    /** The image's esp_app_desc_t::app_elf_sha256. */
    uint8_t imageId[FIRMWARE_IMAGE_ID_LENGTH];
    /** Length of the whole image, signature sector included. */
    uint32_t imageLength;
    uint16_t index;
} __attribute__((packed));
