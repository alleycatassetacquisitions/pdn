# GitHub Issue: refactor: extract base state templates for minigame boilerplate (~1,400 lines)

**Labels:** enhancement, refactor

---

## Summary

Codebase audit identified **~2,000 lines of copy-paste boilerplate** across the 7 FDN minigames (Ghost Runner, Signal Echo, Spike Vector, Cipher Path, Exploit Sequencer, Breach Defense, Firewall Decrypt). All follow an identical state flow — Intro → Show → Gameplay → Evaluate → Win/Lose — with only game-specific text, LED states, and evaluate logic differing.

This issue tracks the phased refactoring to eliminate ~1,400 lines while aligning with Cyonic's **StateMachine-IS-A-State** composability pattern.

## Duplication Hotspot Map

| Severity | Copies | Lines/Copy | Total | What |
|----------|--------|-----------|-------|------|
| CRITICAL | x7 | 65 | 455 | `populateStateMap()` boilerplate |
| CRITICAL | x7 | 50 | 350 | Win state (`onStateMounted`) |
| CRITICAL | x7 | 45 | 315 | Intro state (full lifecycle) |
| CRITICAL | x7 | 45 | 315 | Lose state (full lifecycle) |
| HIGH | x7 | 30 | 210 | Evaluate state (framework) |
| MEDIUM | x7 | 6 | 42 | `seedRng()` (identical) |

**Total: ~1,882 lines of duplication**

## Refactoring Plan (4 Phases)

### Phase 1: Move `seedRng()` to MiniGame base (Quick win)
- 100% identical across all 7 games
- Move to `MiniGame` base class
- **Lines saved: ~42**

### Phase 2: Template base states for Win/Lose/Intro
Create CRTP or template base states where each game overrides only 2-3 methods:
- `victoryText()` / `defeatText()` / `introTitle()`
- `isHardMode()` detection
- LED state constants

Each game's Win/Lose/Intro drops from ~50 lines to ~10 lines of overrides.
- **Lines saved: ~840**

### Phase 3: Generalize `populateStateMap()`
Extract standard 6-state wiring into a MiniGame helper method.
- **Lines saved: ~400**

### Phase 4: Display rendering utilities
Common title+score rendering, LED animation setup, button registration.
- **Lines saved: ~100**

## Design Alignment with Cyonic's Vision

This refactoring preserves the core architectural patterns:
- **StateMachine IS-A State** — composability unchanged
- **App pool pattern** — all 9 apps still pre-allocated in Device
- **Pause/Resume with Snapshots** — base states forward lifecycle correctly
- **Terminal state behavior** — `managedMode` routing preserved
- **Config/Session separation** — games keep their own config structs

## Acceptance Criteria

- [ ] `seedRng()` lives in `MiniGame` base class
- [ ] `BaseWinState`, `BaseLoseState`, `BaseIntroState` templates exist
- [ ] All 7 games use base templates with game-specific overrides only
- [ ] `populateStateMap()` uses shared wiring helper
- [ ] `pio run -e native` builds clean
- [ ] `pio test -e native` passes
- [ ] No behavioral changes (all games play identically)

## References

- `docs/DUPLICATION-ANALYSIS.md` — full audit with side-by-side comparisons
- `docs/AGENT-REFERENCE.md` — API cheat sheet for implementing agents
