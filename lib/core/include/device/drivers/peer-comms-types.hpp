#pragma once

#include <cstddef>
#include <cstdint>

//PktType determines which callback will handle the packet on the receiving end
enum class PktType : uint8_t {
    kPlayerInfoBroadcast = 0,
    kQuickdrawCommand = 1,
    kDebugPacket = 2,
    kHandshakeCommand = 3,
    kChainAnnouncement = 4,
    kChainAnnouncementAck = 5,
    kChainGameEvent = 6,
    kChainConfirm = 7,
    kRoleAnnounce = 8,
    kRoleAnnounceAck = 9,
    kChainGameEventAck = 10,
    kShootoutCommand = 11,
    kShootoutCommandAck = 12,
    kSymbolMatchCommand = 13,
    kFdnConnect = 14,
    kCrashLog = 15,
    kFirmwareUpdate = 16,
    kNumPacketTypes  // Not a real packet type, DO NOT USE
};

struct DataPktHdr
{
    /// Total packet length including this header. Two bytes because a v2 frame
    /// carries more than a byte can count.
    uint16_t pktLen;
    PktType packetType;
} __attribute__((packed));

/// One ESP-NOW v2 frame's payload, less this protocol's header. Stated here
/// rather than derived from the IDF macro so the native suite can see it; the
/// driver asserts the two agree.
constexpr size_t MAX_PKT_DATA_SIZE = 1470 - sizeof(DataPktHdr);

struct ChainConfirmPayload
{
    uint8_t originatorMac[6];
    uint8_t seqId;
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

/// Firmware distribution frames. The seed broadcasts an OFFER every second and
/// loops CHUNKs through the whole image a fixed number of times; receivers listen
/// and never answer. Nothing here is signed: IDF verifies the assembled image's
/// signature in esp_ota_end, against the keys that signed the image already
/// running, so a forged offer can only waste a download.
enum class FirmwareCmd : uint8_t {
    OFFER = 1,
    CHUNK = 2,
};

/// Bytes of image per CHUNK.
constexpr uint16_t FIRMWARE_CHUNK_SIZE = 1400;

/// Identifies an image: its esp_app_desc_t::app_elf_sha256.
constexpr size_t FIRMWARE_IMAGE_ID_LENGTH = 32;

/// Announces the image the seed is streaming.
struct FirmwareOfferPayload {
    uint8_t command;
    uint8_t imageId[FIRMWARE_IMAGE_ID_LENGTH];
    uint32_t imageLength;
    uint16_t chunkCount;
} __attribute__((packed));

/// Header of a CHUNK; FIRMWARE_CHUNK_SIZE bytes of image follow, fewer for the last.
struct FirmwareChunkHeader {
    uint8_t command;
    /// First bytes of the image id, so a chunk from another run is not written.
    uint8_t imageIdPrefix[4];
    uint16_t index;
} __attribute__((packed));

static_assert(sizeof(FirmwareChunkHeader) + FIRMWARE_CHUNK_SIZE <= MAX_PKT_DATA_SIZE,
              "a chunk must fit one ESP-NOW v2 frame");
