#pragma once

#include <cstdint>

//PktType determines which callback will handle the packet on the receiving end
enum class PktType : uint8_t
{
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
    kNumPacketTypes //Not a real packet type, DO NOT USE
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

/// Bitmap covers 3072 chunks, keeping status report in one frame.
constexpr size_t FIRMWARE_BITMAP_BYTES = 384;

/// Maximum chunks per firmware offer; floors chunk size at 1322 bytes.
constexpr size_t FIRMWARE_MAX_CHUNKS = FIRMWARE_BITMAP_BYTES * 8;

/// SHA-256 digest length.
constexpr size_t FIRMWARE_SHA256_LENGTH = 32;

/// P-256 R||S signature length (no DER, no ASN.1).
constexpr size_t FIRMWARE_SIG_LENGTH = 64;

/// Version and label string length.
constexpr size_t FIRMWARE_LABEL_LENGTH = 16;

/// Firmware distribution commands: offer, chunk, poll, status, complete.
enum class FirmwareCmd : uint8_t {
  OFFER = 0,
  CHUNK = 1,
  POLL = 2,
  STATUS = 3,
  COMPLETE = 4,
};

/// Firmware distribution results: success, validation, flash errors.
enum class FirmwareResult : uint8_t {
  OK = 0,
  BAD_HASH = 1,
  BAD_SIGNATURE = 2,
  BAD_CERT = 3,
  STALE_CERT = 4,
  FLASH_FAILED = 5,
  TOO_LARGE = 6,
};

/// Delegation record: root signs this, signer signs images.
struct SignerCert {
  /// Key identifier, allows revocation by ID.
  uint8_t keyId[4];
  /// Generation counter for key rotation.
  uint8_t generation;
  /// P-256 public key (uncompressed, 64 bytes).
  uint8_t publicKey[64];
  /// Signer name or version label.
  char label[FIRMWARE_LABEL_LENGTH];
  /// Root signature over keyId|generation|publicKey|label.
  uint8_t rootSignature[FIRMWARE_SIG_LENGTH];
} __attribute__((packed));

/// Firmware offer: announces image, delegates signing, provides cert and sig.
struct FirmwareOfferPayload {
  /// Command type: FirmwareCmd::OFFER.
  uint8_t command;
  /// SHA-256 of complete image.
  uint8_t imageSha256[FIRMWARE_SHA256_LENGTH];
  /// Total image length in bytes.
  uint32_t imageLength;
  /// Bytes per chunk (except possibly the last).
  uint16_t chunkSize;
  /// Number of chunks; at most FIRMWARE_MAX_CHUNKS.
  uint16_t chunkCount;
  /// Delegation cert and root signature.
  SignerCert cert;
  /// Signature over imageSha256|imageLength|chunkSize|chunkCount.
  uint8_t imageSignature[FIRMWARE_SIG_LENGTH];
  /// Image version string.
  char version[FIRMWARE_LABEL_LENGTH];
} __attribute__((packed));

/// Chunk header prepended to chunk data in a frame.
struct FirmwareChunkHeader {
  /// Command type: FirmwareCmd::CHUNK.
  uint8_t command;
  /// Chunk index; at most FIRMWARE_MAX_CHUNKS - 1.
  uint16_t index;
  /// Bytes in this chunk (chunkSize except possibly last).
  uint16_t length;
} __attribute__((packed));

/// Poll: request status for an image.
struct FirmwarePollPayload {
  /// Command type: FirmwareCmd::POLL.
  uint8_t command;
  /// SHA-256 of image to poll.
  uint8_t imageSha256[FIRMWARE_SHA256_LENGTH];
} __attribute__((packed));

/// Status: bitmap of received chunks.
struct FirmwareStatusPayload {
  /// Command type: FirmwareCmd::STATUS.
  uint8_t command;
  /// SHA-256 of image; matches poll.
  uint8_t imageSha256[FIRMWARE_SHA256_LENGTH];
  /// Count of chunks received so far.
  uint16_t receivedCount;
  /// Bitmap: bit N set means chunk N received (384 bytes = 3072 bits).
  uint8_t bitmap[FIRMWARE_BITMAP_BYTES];
} __attribute__((packed));

/// Complete: reports final result after flashing.
struct FirmwareCompletePayload {
  /// Command type: FirmwareCmd::COMPLETE.
  uint8_t command;
  /// SHA-256 of image; confirms which update completed.
  uint8_t imageSha256[FIRMWARE_SHA256_LENGTH];
  /// FirmwareResult: OK or error code.
  uint8_t result;
} __attribute__((packed));
