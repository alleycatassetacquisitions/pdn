#include "device/serial-frame-parser.hpp"

#include "fuzz-common.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/crc16.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Exact-sized heap buffer so ASan catches any read past len.
void feedExactCopy(SerialFrameParser& parser, const uint8_t* src, size_t len) {
    ExactBuffer exactBuf(src, len);
    parser.feed(exactBuf.data(), len);
}

constexpr uint8_t ACTION_FEED_RAW_CHUNK = 0;
constexpr uint8_t ACTION_FEED_VALID_CRC_FRAME = 1;
constexpr uint8_t ACTION_ADVANCE_CLOCK = 2;
constexpr uint8_t ACTION_REQUEST_RESET = 3;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzStepClock clock;
    SimpleTimer::setPlatformClock(&clock);

    SerialFrameParser parser;
    HelloPayload lastDecoded{};
    size_t decodedCount = 0;

    parser.setHelloFrameHandler([&](const HelloPayload& hello) {
        lastDecoded = hello;
        ++decodedCount;
    });

    feedExactCopy(parser, data, size);

    // Mode 2: Action stream driving chunking, timeouts, requestReset(), and valid frames.
    parser.requestReset();

    size_t pos = 0;
    while (pos < size) {
        uint8_t control = data[pos++];
        uint8_t action = control & 0x03U;
        size_t param = (control >> 2U) + 1U;

        if (action == ACTION_FEED_RAW_CHUNK) {
            size_t len = param;
            if (len > size - pos) {
                len = size - pos;
            }
            if (len > 0) {
                feedExactCopy(parser, data + pos, len);
                pos += len;
            }
        } else if (action == ACTION_FEED_VALID_CRC_FRAME) {
            if (size - pos < HELLO_PAYLOAD_LEN) {
                break;
            }
            HelloPayload payload{};
            std::memcpy(&payload, data + pos, sizeof(HelloPayload));
            pos += sizeof(HelloPayload);

            std::vector<uint8_t> framed = encodeFramed(payload);
            feedExactCopy(parser, framed.data(), framed.size());
        } else if (action == ACTION_ADVANCE_CLOCK) {
            clock.advance(static_cast<unsigned long>(param));
        } else if (action == ACTION_REQUEST_RESET) {
            parser.requestReset();
        }
    }

    if (decodedCount > 0) {
        std::vector<uint8_t> reencoded = encodeFramed(lastDecoded);
        constexpr size_t EXPECTED_HELLO_FRAME_LEN = 2 + 1 + HELLO_PAYLOAD_LEN + 2;
        if (reencoded.size() != EXPECTED_HELLO_FRAME_LEN) {
            __builtin_trap();
        }
    }

    // After >50ms (PARSER_TIMEOUT_MS), any partial frame must expire and a valid HELLO must decode.
    clock.advance(55);
    HelloPayload sentinel{};
    for (int i = 0; i < 6; ++i) {
        sentinel.source[i] = static_cast<uint8_t>(0x10 + i);
        sentinel.headMac[i] = static_cast<uint8_t>(0xA0 + i);
    }
    sentinel.deviceType = 1;
    sentinel.flags = HELLO_FLAG_CONFIRMED;

    size_t countBeforeSentinel = decodedCount;
    std::vector<uint8_t> validFrame = encodeFramed(sentinel);
    size_t split = (size > 0) ? (data[0] % validFrame.size()) : 0;
    feedExactCopy(parser, validFrame.data(), split);
    feedExactCopy(parser, validFrame.data() + split, validFrame.size() - split);

    if (decodedCount != countBeforeSentinel + 1) {
        __builtin_trap();
    }
    if (std::memcmp(&lastDecoded, &sentinel, sizeof(HelloPayload)) != 0) {
        __builtin_trap();
    }

    SimpleTimer::setPlatformClock(nullptr);
    return 0;
}
