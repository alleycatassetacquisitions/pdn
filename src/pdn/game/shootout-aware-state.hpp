#pragma once

#include <functional>

#include "utils/debounced-condition.hpp"
#include "game/shootout-manager.hpp"

/// Shared shootout-abort/idle policy mixed into game states that must
/// coordinate with a live tournament. States compose this alongside
/// ConnectState<PDN> (or TypedState<PDN>) so the debounce window and the two
/// policy predicates live in one place instead of being copy-pasted across the
/// duel and shootout states. Non-owning: holds only a borrowed ShootoutManager.
class ShootoutAwareState {
public:
    /// Ring-break debounce window. A cable nudge flickers the loop state for a
    /// tick or two on real hardware; act only once the break has settled.
    static constexpr unsigned long kLoopBreakDebounceMs = 500;

    explicit ShootoutAwareState(ShootoutManager* shootoutManager)
        : shootoutManager(shootoutManager) {}

    /// Idle-return is suppressed while a tournament is live: a mid-tournament
    /// cable wiggle must not bail a duelist to Idle; the shootout's own
    /// PEER_LOST/ABORT teardown owns that path. The persistence check is passed
    /// as a predicate and only sampled when no tournament is active: sampling it
    /// advances the caller's disconnect debounce, and doing so mid-shootout
    /// would let a duelist bail to Idle the instant the shootout ends instead of
    /// after a fresh window.
    bool disconnectedBackToIdle(const std::function<bool()>& isPersistentlyDisconnected) {
        if (isShootoutActive()) return false;
        return isPersistentlyDisconnected();
    }

    /// Debounced ring-open guard. Once the ring has stayed open for the full
    /// window, abort the whole tournament: abortTournament broadcasts ABORT to
    /// the ring and lands the local device on the aborted screen (via Phase::
    /// ABORTED), rather than silently dropping just this device to Idle. It is
    /// idempotent, so a debounced guard firing repeatedly is harmless. Returns
    /// true on the tick the guard fires so the caller can raise its own
    /// transition flag.
    bool tickAbortGuard(bool ringSettledOpen) {
        if (!debounce.heldFor(ringSettledOpen, kLoopBreakDebounceMs)) return false;
        shootoutManager->abortTournament();
        return true;
    }

    void resetAbortGuard() { debounce.reset(); }

protected:
    bool isShootoutActive() const {
        return shootoutManager && shootoutManager->active();
    }

    ShootoutManager* shootoutManager = nullptr;

private:
    DebouncedCondition debounce;
};
