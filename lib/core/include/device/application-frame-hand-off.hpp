#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "device/application-frame-kind.hpp"
#include "device/serial-frame-parser.hpp"
#include "utils/secure-wipe.hpp"

// How an application frame that was accepted for a jack ended.
enum class ApplicationFrameOutcome : uint8_t {
    // Every byte was handed to the jack's UART or, at link-down, the writer had
    // begun it and will finish it. It may still be shifting out, and nothing
    // confirms the peer parsed it.
    WRITTEN,
    // The peer left before the writer took the frame; no byte of it was written.
    DROPPED,
};

// One accepted frame whose outcome is settled.
struct SettledApplicationFrame {
    ApplicationFrameKind kind;
    ApplicationFrameOutcome outcome;
};

// One application frame on its way from the main loop to the HELLO writer,
// which on the ESP32 is another task. offer(), drop() and takeWritten() belong
// to the main loop; takeForWrite() and finish() to the writer.
//
// The two sides share the frame buffer, a plain array, and the state, which is
// atomic and says whose the buffer is. The main loop holds it while EMPTY and
// fills it before publishing OFFERED. From OFFERED exactly one side wins a
// compare-exchange: the writer, which then holds the buffer through WRITING
// (and SETTLED) until finish(), or the main loop, which takes it back in
// drop(). Nobody reads or writes the buffer without holding it, so a frame goes
// out whole or not at all, and each side clears the buffer before letting go.
class ApplicationFrameHandOff {
public:
    /** Wipes a frame still waiting; the writer must already be gone. */
    ~ApplicationFrameHandOff() { clearFrame(); }

    /**
     * Main loop. Frames `header` (whose length is at most
     * APPLICATION_PAYLOAD_MAX_LEN) and its payload for the writer. False,
     * taking nothing, unless the slot is empty: an earlier frame is still
     * waiting, being written, or written with takeWritten() yet to collect it.
     */
    bool offer(const ApplicationFrameHeader& header, const uint8_t* payload) {
        // Only this side ever leaves EMPTY, so the buffer cannot change hands
        // between the load and the store that publishes it.
        if (state.load() != State::EMPTY) return false;
        frameLength = encodeApplicationFrame(header, payload, frame);
        frameKind = static_cast<ApplicationFrameKind>(header.kind);
        state.store(State::OFFERED);
        return true;
    }

    /**
     * Writer. The offered frame and its `length`, the caller's to read until
     * it calls finish(), or nullptr when none is waiting.
     */
    const uint8_t* takeForWrite(size_t& length) {
        State expected = State::OFFERED;
        if (!state.compare_exchange_strong(expected, State::WRITING)) return nullptr;
        length = frameLength;
        return frame.data();
    }

    /**
     * Writer, once every byte of the frame from takeForWrite() is written.
     * Clears the buffer and lets go of it.
     */
    void finish() {
        clearFrame();
        // A drop() that came too late has already reported this frame, so the
        // slot goes straight back to empty with nothing left to collect.
        State expected = State::WRITING;
        if (!state.compare_exchange_strong(expected, State::WRITTEN)) {
            state.store(State::EMPTY);
        }
    }

    /**
     * Main loop, when the peer the frame was for has gone. Settles whatever
     * the slot holds, so nothing is left to report once another peer is on the
     * jack: DROPPED for a frame the writer had not taken, cleared here;
     * WRITTEN for one it had, which is past recall and will be finished.
     * Empty when there was nothing to settle.
     */
    std::optional<SettledApplicationFrame> drop() {
        State seen = State::OFFERED;
        if (state.compare_exchange_strong(seen, State::EMPTY)) {
            clearFrame();
            return SettledApplicationFrame{frameKind, ApplicationFrameOutcome::DROPPED};
        }
        // Losing that exchange to the writer leaves WRITING; losing this one
        // too means it finished in between, which leaves WRITTEN.
        if (seen == State::WRITING && state.compare_exchange_strong(seen, State::SETTLED)) {
            return SettledApplicationFrame{frameKind, ApplicationFrameOutcome::WRITTEN};
        }
        if (seen != State::WRITTEN) return std::nullopt;
        state.store(State::EMPTY);
        return SettledApplicationFrame{frameKind, ApplicationFrameOutcome::WRITTEN};
    }

    /**
     * Main loop. The frame the writer finished, exactly once, unless drop()
     * already settled it; the slot is empty from then on.
     */
    std::optional<SettledApplicationFrame> takeWritten() {
        State expected = State::WRITTEN;
        if (!state.compare_exchange_strong(expected, State::EMPTY)) return std::nullopt;
        return SettledApplicationFrame{frameKind, ApplicationFrameOutcome::WRITTEN};
    }

private:
    // SETTLED is WRITING with its outcome already reported by drop().
    enum class State : uint8_t { EMPTY, OFFERED, WRITING, WRITTEN, SETTLED };

    void clearFrame() { secureWipe(frame.data(), frame.size()); }

    ApplicationFrame frame{};
    size_t frameLength = 0;
    // The main loop's alone: the writer never reads it.
    ApplicationFrameKind frameKind = ApplicationFrameKind::RECORD_KEY;
    std::atomic<State> state{State::EMPTY};
};
