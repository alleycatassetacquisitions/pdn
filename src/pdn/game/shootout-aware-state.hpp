#pragma once

#include "utils/debounced-condition.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/shootout-manager.hpp"

/// Debounced loop-break abort guard. Composed by a state that must abort a live
/// tournament once the physical ring settles open.
class ShootoutAwareState {
public:
    /// Ring-break debounce window. A cable nudge flickers the loop state for a
    /// tick or two on real hardware; act only once the break has settled.
    static constexpr unsigned long LOOP_BREAK_DEBOUNCE_MS = 500;

    /// Both managers are borrowed and must outlive the state. shootoutManager
    /// is non-null by contract; a null chainDuelManager disables the guard.
    ShootoutAwareState(ShootoutManager* shootoutManager, ChainDuelManager* chainDuelManager)
        : shootoutManager(shootoutManager)
        , chainDuelManager(chainDuelManager) {}

    /// Aborts the tournament once the ring-break has held for the full
    /// debounce window.
    void tickAbortGuard() {
        bool ringSettledOpen = chainDuelManager && !chainDuelManager->isLoop();
        if (!debounce.heldFor(ringSettledOpen, LOOP_BREAK_DEBOUNCE_MS)) return;
        shootoutManager->abortTournament();
    }

    /// Discards any in-flight debounce run; call from onStateDismounted.
    void resetAbortGuard() { debounce.reset(); }

protected:
    ShootoutManager* shootoutManager = nullptr;

private:
    ChainDuelManager* chainDuelManager = nullptr;
    DebouncedCondition debounce;
};
