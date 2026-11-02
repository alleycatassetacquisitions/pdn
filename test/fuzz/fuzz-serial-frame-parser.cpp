#include "device/serial-frame-parser.hpp"

#include "device/drivers/logger.hpp"
#include "device/drivers/platform-clock.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/crc16.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

class FuzzNullLogger : public LoggerInterface {
public:
    void vlog(LogLevel, const char*, const char*, int, const char*, va_list) override {}
};

class FuzzStepClock : public PlatformClock {
public:
    unsigned long milliseconds() override {
        return currentTimeMs;
    }

    void setTime(unsigned long timeMs) {
        currentTimeMs = timeMs;
    }

    void advance(unsigned long deltaMs) {
        currentTimeMs += deltaMs;
    }

private:
    unsigned long currentTimeMs = 1000;
};

// Copy into an exact-sized heap allocation (Tip #5) so ASan catches even a
// 1-byte read past `len` instead of reading silently into the fuzzer's buffer.
void feedExactCopy(SerialFrameParser& parser, const uint8_t* src, size_t len) {
    auto* exactBuf = new uint8_t[len];
    if (len > 0) {
        std::memcpy(exactBuf, src, len);
    }
    parser.feed(exactBuf, len);
    delete[] exactBuf;
}

constexpr uint8_t ACTION_FEED_RAW_CHUNK = 0;
constexpr uint8_t ACTION_FEED_VALID_CRC_FRAME = 1;
constexpr uint8_t ACTION_ADVANCE_CLOCK = 2;
constexpr uint8_t ACTION_REQUEST_RESET = 3;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static FuzzNullLogger nullLogger;
    g_logger = &nullLogger;

    FuzzStepClock clock;
    SimpleTimer::setPlatformClock(&clock);

    SerialFrameParser parser;
    HelloPayload lastDecoded{};
    size_t decodedCount = 0;

    parser.setHelloFrameHandler([&](const HelloPayload& hello) {
        lastDecoded = hello;
        ++decodedCount;
    });

    // Mode 1: Direct raw byte-stream fuzzing on an exact-sized heap copy.
    feedExactCopy(parser, data, size);

    // Mode 2: Structured action stream (tests chunk boundaries, mid-frame timeouts,
    // cross-thread reset requests, and valid-CRC frame decoding).
    parser.requestReset();
    clock.setTime(1000);

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

    // Mode 3 (Invariant Oracle): After any arbitrary corrupt byte stream followed by
    // the mid-frame timeout window (> 50ms), a valid HELLO frame fed in fuzz-sized
    // chunks MUST always be recovered and decoded identically.
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
