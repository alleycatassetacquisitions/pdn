---
description: Architecture patterns for PDN firmware — state machine, error handling, comments
globs: **/*.hpp, **/*.cpp
alwaysApply: false
---

# State Machine Pattern

Every game state follows this lifecycle:
- `onStateMounted` — enter actions, start timers, register button callbacks
- `onStateLoop` — polled each tick; sets `transitionToXState` flags
- `onStateDismounted` — cleanup, invalidate timers, remove callbacks, reset flags

Transitions are boolean predicate methods wired in `populateStateMap`:
```cpp
// In state header
bool transitionToIdle();

// In state .cpp — reads a private flag set in onStateLoop
bool Duel::transitionToIdle() {
    return transitionToIdleState;
}

// Wired in populateStateMap
duel->addTransition(new StateTransition(
    [duel]() { return duel->transitionToIdle(); },
    idle));
```

Use a named lambda stored in a local variable when the same predicate applies to multiple states:
```cpp
auto phaseIsAborted = [shMgr]() {
    return shMgr && shMgr->getPhase() == ShootoutManager::Phase::ABORTED;
};
idle->addTransition(new StateTransition(phaseIsAborted, shAborted));
duel->addTransition(new StateTransition(phaseIsAborted, shAborted));
```

---

# Error Handling
- No exceptions unless absolutely necessary to capture catastrophic failure states.
- Early-return guards with `LOG_E` before returning:
```cpp
if (dataLen != sizeof(QuickdrawPacket)) {
    LOG_E(TAG, "Unexpected packet len: got %lu expected %lu", dataLen, sizeof(QuickdrawPacket));
    return -1;
}
```
- Packet handler return codes: `1` = success, `-1` = failure.

---

# Comments

- `/** */` Doxygen blocks on all public method declarations in headers.
- `//` line comments for inline implementation notes — explain *why*, not *what*.
- `#define TAG` log tags in `.cpp` files only.
- Do not leave large blocks of commented-out code in files.
