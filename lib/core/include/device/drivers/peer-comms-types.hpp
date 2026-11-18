#pragma once

#include <cstddef>
#include <cstdint>

//PktType determines which callback will handle the packet on the receiving end
enum class PktType : uint8_t {
    kPlayerInfoBroadcast = 0,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kMatchCommand = 1,
    // Reserved: older builds sent remote-debug packets on 2.
    kChainGameEvent = 6,
    kChainConfirm = 7,
    kRoleAnnounce = 8,
    kChainGameEventAck = 10,
    kShootoutCommand = 11,
    kShootoutCommandAck = 12,
    kSymbolMatchCommand = 13,
    kFdnConnect = 14,
    kPdnConnectionContext = 15,
    // Event firmware listens for firmware frames here; see the assert after DataPktHdr.
    // NOLINTNEXTLINE(readability-identifier-naming)
    kFirmwareUpdate = 16,
    kConnectionAnnounce = 17,
    kDisconnectReport = 18,
    kHeadTransfer = 19,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kChainJoin = 20,
    // Record relay, PDN to FDN and the FDN's ack back (wireless/record-relay-types.hpp).
    // NOLINTNEXTLINE(readability-identifier-naming)
    kCrashRecord = 23,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kMatchRecord = 24,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kRecordAck = 25,
    // NOLINTNEXTLINE(readability-identifier-naming)
    kFdnConnectionContext = 26,
    kNumPacketTypes  // Not a real packet type, DO NOT USE
};

// A player's identity + game state, exchanged once per jack connection so a
// neighbour knows who it is plugged into. Packed: the struct layout IS the
// wire format (both ends run the same firmware). RDC reads only userId; the
// rest is forwarded to the game layer opaquely.
struct PlayerProfile {
    uint16_t userId;   // 0xFFFF while unregistered
    uint8_t gameRole;  // 1 = hunter, 0 = bounty
    uint8_t allegiance;
    char faction[8];
    char name[16];
} __attribute__((packed));

// PDN-to-neighbour connection context. seqId is stamped by the ReliableChannel.
// chainRole is the sender's own ChainRole as of the send (0 = standalone), recorded
// by the receiver.
struct PdnConnectionContext {
    uint8_t seqId;
    uint8_t chainRole;
    PlayerProfile player;
} __attribute__((packed));

// FDN self-description exchanged on cabled connection context. Carries the FDN's
// synchronized UTC wall clock in epoch milliseconds (or 0 when unsynchronized).
struct FdnProfile {
    int64_t utcUnixMs;
} __attribute__((packed));

// FDN-to-neighbour connection context; same header as PdnConnectionContext, FDN body.
struct FdnConnectionContext {
    uint8_t seqId;
    uint8_t chainRole;
    FdnProfile fdn;
} __attribute__((packed));

// Frozen: older builds must parse a newer build's firmware frames (this header, kFirmwareUpdate,
// FirmwareChunkHeader, FIRMWARE_CHUNK_SIZE, ESPNOW_CHANNEL) or they can only be updated over USB.
struct DataPktHdr
{
    PktType packetType;
} __attribute__((packed));
static_assert(sizeof(DataPktHdr) == 1, "older builds parse firmware frames with a one-byte header");
static_assert(static_cast<uint8_t>(PktType::kFirmwareUpdate) == 16,
              "event firmware listens for firmware frames on 16");

struct ChainConfirmPayload
{
    uint8_t originatorMac[6];
    uint8_t seqId;
} __attribute__((packed));

// Supporter -> challenger, unicast: the only evidence a challenger gets of a follower more
// than one cable away. challengerMac guards against enrolling in the wrong chain.
struct ChainJoinPayload {
    uint8_t challengerMac[6];
    uint8_t seqId;
} __attribute__((packed));

struct RoleAnnouncePayload
{
    uint8_t challengerMac[6];
    uint8_t seqId;
} __attribute__((packed));

struct ChainGameEventAckPayload
{
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

// Head-roster cap, set past any real chain (not frame-bound). Past it the head
// drops the announce and the member still reads confirmed.
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

// Demoted head -> successor, one unicast of (member, upstream) pairs. 770 bytes at
// the cap; must fit MAX_PKT_DATA_SIZE (static_assert in esp-now-driver.hpp).
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
