#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "device/drivers/button.hpp"
#include "utils/uuid-bytes.hpp"

/** Inputs in one round. */
constexpr size_t NODE_SEQUENCE_LENGTH = 5;

/** Correct inputs (out of NODE_SEQUENCE_LENGTH) needed for the round to succeed. */
constexpr uint8_t NODE_SUCCESS_THRESHOLD = 4;

/** Most buttons one player presses in a single step. */
constexpr uint8_t NODE_MAX_STEP_INPUTS = 2;

/** Time to press a one-button step, measured from when the step is shown. */
constexpr unsigned long NODE_INPUT_WINDOW_MS = 1400;

/** Pause after a one-button step's window closes. */
constexpr unsigned long NODE_STEP_GAP_MS = 1700;

/** Window of a one-player step with two buttons. */
constexpr unsigned long NODE_DOUBLE_INPUT_WINDOW_MS = 2100;

/** Pause after a two-button step's window closes. */
constexpr unsigned long NODE_DOUBLE_STEP_GAP_MS = 2400;

/** Window of a step both players press, whatever their button counts. */
constexpr unsigned long NODE_BOTH_INPUT_WINDOW_MS = 3000;

/** Pause after a both-player step's window closes. */
constexpr unsigned long NODE_BOTH_STEP_GAP_MS = 3300;

/** Countdown length; step 0 starts this long after the initiator sends SEQUENCE. */
constexpr unsigned long NODE_COUNTDOWN_MS = 6000;

/** SYNC probes the initiator sends to measure the clock offset. */
constexpr uint8_t NODE_SYNC_PROBE_COUNT = 5;

/** Spacing of SYNC probes. */
constexpr unsigned long NODE_SYNC_PROBE_INTERVAL_MS = 50;

/** How long after the last probe the initiator waits for missing SYNC_REPLYs. */
constexpr unsigned long NODE_SYNC_SETTLE_MS = 200;

/** Best acceptable sync round trip; worse means the round is aborted. */
constexpr uint32_t NODE_SYNC_MAX_RTT_MS = 75;

/** Responder aborts when SEQUENCE has not arrived this long after entering the countdown. */
constexpr unsigned long NODE_SEQUENCE_WAIT_MS = 2000;

/** How long after the last window the initiator waits for missing INPUT_RESULTs. */
constexpr unsigned long NODE_RESULT_GRACE_MS = 1000;

/** How long after the last window the responder waits for OUTCOME. */
constexpr unsigned long NODE_OUTCOME_WAIT_MS = 2500;

/** Buttons one player must press, in order. A count of 0 means the player does not press. */
struct NodePrompt {
    uint8_t count = 0;
    ButtonIdentifier buttons[NODE_MAX_STEP_INPUTS] = {ButtonIdentifier::PRIMARY_BUTTON,
                                                      ButtonIdentifier::PRIMARY_BUTTON};
};

/**
 * One step. When only one player presses, that player sees NOW and the other sees the prompt;
 * when both press, each sees their own prompt and the step passes only if both are correct.
 */
struct NodeInput {
    NodePrompt initiator;
    NodePrompt responder;
};

struct NodeInputSequence {
    NodeInput inputs[NODE_SEQUENCE_LENGTH];
};

/** @return true when both players press in @p step. */
bool nodeStepIsBothPress(const NodeInput& step);

/**
 * @return the input window of @p step: the both-player window when both press, else the double
 * window when the prompt has two buttons.
 */
unsigned long nodeStepWindowMs(const NodeInput& step);

/** @return the window plus the pause that follows it. */
unsigned long nodeStepPeriodMs(const NodeInput& step);

/** @return how long after step 0 starts that @p step starts. */
unsigned long nodeStepOffsetMs(const NodeInputSequence& sequence, uint8_t step);

/** Returns uniformly distributed 32-bit values. */
using NodeRandomSource = std::function<uint32_t()>;

/**
 * Builds a round. Each step is a one-button step for one player (2 in 6), a two-button step for
 * one player (3 in 6), or a step where both press one or two buttons each (1 in 6).
 */
NodeInputSequence generateNodeInputSequence(const NodeRandomSource& random);
