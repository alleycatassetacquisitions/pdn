#pragma once

#include "utils/debounced-condition.hpp"
#include "game/shootout-manager.hpp"

/// Debounced loop-break abort guard. Composed by a state that must abort a live
/// tournament once the physical ring settles open. Holds only a borrowed
/// ShootoutManager, non-null by constructor contract.
class ShootoutAwareState {
public:
    /// Ring-break debounce window. A cable nudge flickers the loop state for a
    /// tick or two on real hardware; act only once the break has settled.
    static constexpr unsigned long kLoopBreakDebounceMs = 500;

    explicit ShootoutAwareState(ShootoutManager* shootoutManager)
        : shootoutManager(shootoutManager) {}

    /// Aborts the tournament once the ring has stayed open for the full
    /// debounce window; a flicker shorter than the window is ignored.
    void tickAbortGuard(bool ringSettledOpen) {
        if (!debounce.heldFor(ringSettledOpen, kLoopBreakDebounceMs)) return;
        shootoutManager->abortTournament();
    }

    void resetAbortGuard() { debounce.reset(); }

protected:
    ShootoutManager* shootoutManager = nullptr;

private:
    DebouncedCondition debounce;
};
