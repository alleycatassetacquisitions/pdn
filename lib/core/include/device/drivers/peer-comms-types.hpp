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

/**
 * Every frame's header; the payload is the rest of the frame. A device takes a
 * firmware update only from a build newer than its own, so this header,
 * kFirmwareUpdate, FirmwareChunkHeader, FIRMWARE_CHUNK_SIZE and the ESP-NOW
 * channel must stay as they are, or devices in the field can only be updated
 * over USB.
 */
struct DataPktHdr
{
    PktType packetType;
} __attribute__((packed));

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

/** Bytes of image in every firmware frame but the last of an image. */
constexpr uint16_t FIRMWARE_CHUNK_SIZE = 1400;

constexpr size_t FIRMWARE_IMAGE_ID_LENGTH = 32;

/**
 * Firmware distribution's one frame: this header, then FIRMWARE_CHUNK_SIZE bytes
 * of the image the seed is running (fewer for the last chunk). The seed sends
 * every chunk a fixed number of times and repeats chunk 0 once a second;
 * receivers never answer. Chunk 0 carries the image's app description, 32 bytes
 * in, which a receiver judges before erasing anything. Nothing here is signed,
 * and IDF checks the assembled image against the key that signed the running
 * one, so forged frames cannot install an image. They can still spoil a
 * download, or make a receiver erase its spare slot and wait for chunks that
 * never come.
 */
struct FirmwareChunkHeader {
    /** The image's esp_app_desc_t::app_elf_sha256. */
    uint8_t imageId[FIRMWARE_IMAGE_ID_LENGTH];
    /** Length of the whole image, signature sector included. */
    uint32_t imageLength;
    uint16_t index;
} __attribute__((packed));

