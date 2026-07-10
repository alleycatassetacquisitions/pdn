#pragma once

#include "utils/debounced-condition.hpp"
#include "game/shootout-manager.hpp"

/// Debounced loop-break abort guard. Composed by a state that must abort a live
/// tournament once the physical ring settles open. Holds only a borrowed
/// ShootoutManager.
class LoopBreakAbortState {
public:
    /// Ring-break debounce window. A cable nudge flickers the loop state for a
    /// tick or two on real hardware; act only once the break has settled.
    static constexpr unsigned long kLoopBreakDebounceMs = 500;

    explicit LoopBreakAbortState(ShootoutManager* shootoutManager)
        : shootoutManager(shootoutManager) {}

    /// Once the ring has stayed open for the full window, abort the whole
    /// tournament: abortTournament broadcasts ABORT to the ring and lands the
    /// local device on the aborted screen (Phase::ABORTED) rather than dropping
    /// just this device to Idle. Idempotent, so a debounced guard firing
    /// repeatedly is harmless. Returns true on the tick the guard fires so the
    /// caller can raise its own transition flag.
    bool tickAbortGuard(bool ringSettledOpen) {
        if (!debounce.heldFor(ringSettledOpen, kLoopBreakDebounceMs)) return false;
        if (!shootoutManager) return false;
        shootoutManager->abortTournament();
        return true;
    }

    void resetAbortGuard() { debounce.reset(); }

protected:
    ShootoutManager* shootoutManager = nullptr;

private:
    DebouncedCondition debounce;
};
