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

    ApplicationPayload lastApplicationPayload{};
    ApplicationFrameKind lastApplicationKind = ApplicationFrameKind::RECORD_KEY;
    size_t applicationCount = 0;

    parser.setApplicationFrameHandler(
    [&](ApplicationFrameKind kind, const ApplicationPayload& payload) {
        // Only the opcodes of known kinds may be reported as one.
        if (kind != ApplicationFrameKind::RECORD_KEY && kind != ApplicationFrameKind::MATCH_KEY) {
            __builtin_trap();
        }
        lastApplicationKind = kind;
        lastApplicationPayload = payload;
        ++applicationCount;
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
        } else if (action == ACTION_FEED_VALID_CRC_FRAME && (param & 1U) == 0U) {
            // The parameter's low bit picks HELLO or an application frame; the
            // next bit picks which application kind.
            const ApplicationFrameKind kind =
            (param & 2U) == 0U ? ApplicationFrameKind::RECORD_KEY : ApplicationFrameKind::MATCH_KEY;
            if (size - pos < APPLICATION_PAYLOAD_LEN) {
                break;
            }
            ApplicationPayload payload{};
            std::memcpy(payload.data(), data + pos, payload.size());
            pos += payload.size();

            ApplicationFrame framed{};
            encodeApplicationFrame(kind, payload, framed);
            const size_t countBefore = applicationCount;
            feedExactCopy(parser, framed.data(), framed.size());
            // A partial frame left by earlier input may swallow this one, but a
            // frame decoded with this payload must carry the kind it was framed as.
            if (applicationCount == countBefore + 1 && lastApplicationPayload == payload &&
                lastApplicationKind != kind) {
                __builtin_trap();
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

    // The HELLO sentinel left the parser between frames, so a valid application
    // frame must decode as well, whatever the input did before.
    ApplicationPayload applicationSentinel{};
    for (size_t i = 0; i < applicationSentinel.size(); ++i) {
        applicationSentinel[i] = static_cast<uint8_t>(0xC0 + i);
    }

    // Each kind in turn; the first leaves the parser between frames for the second.
    const ApplicationFrameKind sentinelKinds[] = {ApplicationFrameKind::RECORD_KEY,
                                                  ApplicationFrameKind::MATCH_KEY};
    for (ApplicationFrameKind sentinelKind : sentinelKinds) {
        size_t countBeforeApplicationSentinel = applicationCount;
        ApplicationFrame applicationFrame{};
        encodeApplicationFrame(sentinelKind, applicationSentinel, applicationFrame);
        size_t applicationSplit = (size > 0) ? (data[0] % applicationFrame.size()) : 0;
        feedExactCopy(parser, applicationFrame.data(), applicationSplit);
        feedExactCopy(parser, applicationFrame.data() + applicationSplit,
                      applicationFrame.size() - applicationSplit);

        if (applicationCount != countBeforeApplicationSentinel + 1) {
            __builtin_trap();
        }
        if (lastApplicationPayload != applicationSentinel || lastApplicationKind != sentinelKind) {
            __builtin_trap();
        }
    }

    SimpleTimer::setPlatformClock(nullptr);
    return 0;
}
