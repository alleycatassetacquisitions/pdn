#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// Serial frame protocol constants. Two opcodes share the wire: HELLO, which
// carries physical-connectivity discovery/liveness, and one opaque application
// frame for bytes that have to reach the jack's peer without crossing the
// radio. Everything else rides ESP-NOW. The framing (preamble + opcode +
// payload + CRC-16) recreates for UART what 802.11 gives ESP-NOW for free:
// message boundaries and integrity on a raw byte stream with flaky TRS contacts.
constexpr uint8_t FRAME_PREAMBLE_0 = 0xAA;
constexpr uint8_t FRAME_PREAMBLE_1 = 0x55;
constexpr uint8_t OP_HELLO = 0x00;
// Payload bytes after the opcode, before the CRC. Owned by the packed
// HelloPayload struct below; the constant lets the parser frame the payload
// without depending on the struct in switch tables.
constexpr size_t HELLO_PAYLOAD_LEN = 14;
constexpr size_t HELLO_FRAME_LEN = 2 + 1 + HELLO_PAYLOAD_LEN + 2;
// 0x01 is reserved.
constexpr uint8_t OP_APPLICATION = 0x02;
constexpr size_t APPLICATION_PAYLOAD_MAX_LEN = 32;

// Set once this device's context exchange with the head it currently holds has
// been delivered; cleared by a head change until re-confirmed under the new one.
constexpr uint8_t HELLO_FLAG_CONFIRMED = 0x01;
// Asserted by the device whose own MAC came back around the ring and relayed
// downstream by every child. Ring closure is visible only at that one device,
// yet every member has to know it sits on a ring, so the fact is propagated
// rather than rediscovered.
constexpr uint8_t HELLO_FLAG_RING_CLOSED = 0x02;

// The HELLO payload, exchanged jack-to-jack on every HELLO frame.
// Packed to a fixed 14-byte wire layout: the parser memcpys the raw payload
// straight into this struct, so the field order and packing ARE the wire
// contract.
struct HelloPayload {
    uint8_t source[6];   // this device's WiFi MAC
    uint8_t deviceType;  // DeviceType value; discriminates ConnectionContext deser
    uint8_t headMac[6];  // chain head MAC, all-zero if head/standalone; hop-propagated
    uint8_t flags;       // HELLO_FLAG_* bits
} __attribute__((packed));

static_assert(sizeof(HelloPayload) == HELLO_PAYLOAD_LEN,
              "HelloPayload must match the 14-byte HELLO wire layout");

// An application frame's payload: this header, then `length` bytes, at most
// APPLICATION_PAYLOAD_MAX_LEN. The parser reads only `length`, to frame it;
// the kind and the bytes mean something only to the coordinator's consumers.
struct ApplicationFrameHeader {
    uint8_t kind;
    uint8_t source[6];  // the sender's WiFi MAC, as in HELLO
    uint8_t length;
} __attribute__((packed));

constexpr size_t APPLICATION_HEADER_LEN = sizeof(ApplicationFrameHeader);
constexpr size_t APPLICATION_FRAME_MAX_LEN =
2 + 1 + APPLICATION_HEADER_LEN + APPLICATION_PAYLOAD_MAX_LEN + 2;

std::array<uint8_t, HELLO_FRAME_LEN> encodeHelloFrame(const HelloPayload& hello);
std::vector<uint8_t> encodeFramed(const HelloPayload& hello);

using HelloFrameHandler = std::function<void(const HelloPayload&)>;

using ApplicationFrame = std::array<uint8_t, APPLICATION_FRAME_MAX_LEN>;

/**
 * Frames `header` and its `header.length` bytes of `payload` into the front of
 * `frame`. Returns the bytes used, or 0, writing nothing, when `header.length`
 * exceeds APPLICATION_PAYLOAD_MAX_LEN.
 */
size_t encodeApplicationFrame(const ApplicationFrameHeader& header, const uint8_t* payload,
                              ApplicationFrame& frame);

/** Receives a CRC-valid application frame; `payload` is valid only for the call. */
using ApplicationFrameHandler =
std::function<void(const ApplicationFrameHeader& header, const uint8_t* payload)>;

// Parses one jack's serial byte stream into framed binary frames
// (preamble 0xAA 0x55 + opcode + payload + CRC-16). One instance per jack.
// Binary framing because the jacks are physically unreliable (TRS contacts,
// partial insertion): a corrupted delimited string still parses as *some*
// string, while a frame failing CRC is dropped and the preamble resyncs.
// feed() runs on the main loop today (the driver's exec() polls RX and fires
// the byte callback inline). requestReset() is an atomic flag honored at the
// start of the next feed(), so parser state stays single-owner even if
// feeding ever moves onto a UART event task.
class SerialFrameParser {
public:
    /**
     * Consumes one burst of raw serial bytes; fires the matching handler for
     * every CRC-valid frame completed within it.
     */
    void feed(const uint8_t* data, size_t len);

    /**
     * Request a parser reset from another thread. The actual clear happens at
     * the start of the next feed(), keeping parser state single-owner.
     */
    void requestReset() { resetRequested.store(true); }

    /** Registers the consumer for CRC-valid HELLO frames, decoded to HelloPayload. */
    void setHelloFrameHandler(HelloFrameHandler cb) { helloFrameHandler = std::move(cb); }

    /** Registers the consumer for CRC-valid application frames, of any kind. */
    void setApplicationFrameHandler(ApplicationFrameHandler handler) {
        applicationFrameHandler = std::move(handler);
    }

private:
    enum class ParseState {
        SCAN_FOR_SYNC,
        GOT_AA,
        READING_OPCODE,
        READING_PAYLOAD,
        READING_CRC,
    };

    // The largest payload any opcode frames: an application header and payload.
    // A fixed buffer, so a key never leaves copies behind in reallocated heap.
    static constexpr size_t PAYLOAD_CAPACITY = APPLICATION_HEADER_LEN + APPLICATION_PAYLOAD_MAX_LEN;
    static_assert(HELLO_PAYLOAD_LEN <= PAYLOAD_CAPACITY, "the payload buffer must hold a HELLO");

    struct ParserState {
        ParseState state = ParseState::SCAN_FOR_SYNC;
        uint8_t opcode = 0;
        std::array<uint8_t, PAYLOAD_CAPACITY> payloadBuf{};
        size_t payloadLength = 0;
        size_t expectedPayloadLen = 0;
        uint8_t crcBytes[2] = {0, 0};
        size_t crcBytesRead = 0;
    };

    ParserState parser;

    // Timestamp of the last arriving byte; checkTimeout() uses it to clear a
    // stalled mid-frame parse.
    unsigned long lastByteMs = 0;

    std::atomic<bool> resetRequested{false};

    HelloFrameHandler helloFrameHandler;
    ApplicationFrameHandler applicationFrameHandler;

    // Wall-clock window after which a mid-frame parser resets to scan-for-sync.
    static constexpr unsigned long PARSER_TIMEOUT_MS = 50;

    void resetParser();
    void checkTimeout();
    void consumeByte(uint8_t b);
    void resyncFromConsumedBytes();
    void dispatchFrame();
    // Opcode whitelist and each opcode's initial payload length; an application
    // frame's length byte extends it in consumeByte().
    static bool isKnownOpcode(uint8_t opcode, size_t& payloadLen);
    static unsigned long nowMs();
};
