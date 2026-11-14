#include "game/node-network/node-input-sequence.hpp"

namespace {
NodePrompt randomPrompt(const NodeRandomSource& random, uint8_t count) {
    NodePrompt prompt;
    prompt.count = count;
    for (uint8_t press = 0; press < count; press++) {
        prompt.buttons[press] = (random() & 1u) == 0 ? ButtonIdentifier::PRIMARY_BUTTON
                                                    : ButtonIdentifier::SECONDARY_BUTTON;
    }
    return prompt;
}

bool hasDoublePrompt(const NodeInput& step) {
    return step.initiator.count > 1 || step.responder.count > 1;
}
}  // namespace

bool nodeStepIsBothPress(const NodeInput& step) {
    return step.initiator.count > 0 && step.responder.count > 0;
}

unsigned long nodeStepWindowMs(const NodeInput& step) {
    if (nodeStepIsBothPress(step)) {
        return NODE_BOTH_INPUT_WINDOW_MS;
    }
    return hasDoublePrompt(step) ? NODE_DOUBLE_INPUT_WINDOW_MS : NODE_INPUT_WINDOW_MS;
}

unsigned long nodeStepPeriodMs(const NodeInput& step) {
    if (nodeStepIsBothPress(step)) {
        return NODE_BOTH_INPUT_WINDOW_MS + NODE_BOTH_STEP_GAP_MS;
    }
    return hasDoublePrompt(step) ? NODE_DOUBLE_INPUT_WINDOW_MS + NODE_DOUBLE_STEP_GAP_MS
                                 : NODE_INPUT_WINDOW_MS + NODE_STEP_GAP_MS;
}

unsigned long nodeStepOffsetMs(const NodeInputSequence& sequence, uint8_t step) {
    unsigned long offset = 0;
    for (uint8_t earlier = 0; earlier < step && earlier < NODE_SEQUENCE_LENGTH; earlier++) {
        offset += nodeStepPeriodMs(sequence.inputs[earlier]);
    }
    return offset;
}

NodeInputSequence generateNodeInputSequence(const NodeRandomSource& random) {
    NodeInputSequence sequence;
    for (NodeInput& step : sequence.inputs) {
        const uint32_t kind = random() % 6;
        if (kind == 5) {
            step.initiator = randomPrompt(random, (random() & 1u) == 0 ? 1 : 2);
            step.responder = randomPrompt(random, (random() & 1u) == 0 ? 1 : 2);
            continue;
        }
        const NodePrompt prompt = randomPrompt(random, kind < 2 ? 1 : 2);
        if ((random() & 1u) == 0) {
            step.initiator = prompt;
        } else {
            step.responder = prompt;
        }
    }
    return sequence;
}
