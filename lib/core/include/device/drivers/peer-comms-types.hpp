#pragma once

#include <cstddef>
#include <cstdint>

// PktType determines which callback will handle the packet on the receiving end
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
    kDeviceTestCommand = 17,
    kNumPacketTypes  // Not a real packet type, DO NOT USE
};

// Frozen: older builds must parse a newer build's firmware frames (this header, kFirmwareUpdate,
// FirmwareChunkHeader, FIRMWARE_CHUNK_SIZE, ESPNOW_CHANNEL) or they can only be updated over USB.
struct DataPktHdr {
    PktType packetType;
} __attribute__((packed));
static_assert(sizeof(DataPktHdr) == 1, "older builds parse firmware frames with a one-byte header");

struct ChainConfirmPayload {
    uint8_t originatorMac[6];
    uint8_t seqId;
} __attribute__((packed));

struct RoleAnnouncePayload {
    uint8_t role;  // 1 = hunter, 0 = target/bounty
    uint8_t championMac[6];
    uint8_t seqId;
} __attribute__((packed));

struct RoleAnnounceAckPayload {
    uint8_t seqId;
} __attribute__((packed));

struct ChainGameEventAckPayload {
    uint8_t seqId;
} __attribute__((packed));

enum class ShootoutCmd : uint8_t {
    CONFIRM = 0,
    BRACKET = 1,
    MATCH_START = 2,
    MATCH_RESULT = 3,
    TOURNAMENT_END = 4,
    PEER_LOST = 5,
    ABORT = 6,
};

struct ShootoutPacket {
    ShootoutCmd cmd;
    uint8_t seqId;  // nonzero for reliable commands; 0 = no ack expected
    uint8_t payload[];
} __attribute__((packed));

struct ShootoutAckPayload {
    ShootoutCmd cmd;
    uint8_t seqId;
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
