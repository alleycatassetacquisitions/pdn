# GitHub Issue: docs: establish API conventions and agent reference for fleet development

**Labels:** documentation, fleet

---

## Summary

To ensure fleet agents (02-12) produce code that aligns with Cyonic's architectural vision and existing codebase patterns, we need standardized API reference documentation. This reduces ramp-up time, prevents pattern drift, and makes contributions look cohesive.

## Key Documents Created

### `docs/AGENT-REFERENCE.md`
Comprehensive API cheat sheet covering:
- **Architecture block diagram** — Device/StateMachine/State hierarchy
- **MiniGame state flow** — Intro → Show → Gameplay → Evaluate → Win/Lose
- **Display API** — fluent chain pattern with `invalidateScreen()`, `setGlyphMode()`, `drawText()`, `render()`
- **Button input** — lambda callback pattern with `parameterizedCallbackFunction`
- **LED animations** — `AnimationConfig` with type, speed, curve, initialState
- **Haptics** — `setIntensity()`, `max()`, `off()`
- **SimpleTimer** — `setTimer()`, `expired()`, `invalidate()`
- **State transitions** — `addTransition()` with `std::bind` condition callbacks
- **Config/Session pattern** — difficulty tuning vs runtime state separation
- **MiniGameOutcome** — WON/LOST/IN_PROGRESS with score and hardMode

### `docs/DUPLICATION-ANALYSIS.md`
Full duplication audit with:
- Hotspot severity map (7 categories, ~1,882 lines total)
- Side-by-side code comparisons
- Phased refactoring strategy
- Anti-bloat tooling recommendations

## Coding Conventions (from Cyonic's code)

| Element | Convention | Example |
|---------|-----------|---------|
| Classes | PascalCase | `GhostRunner`, `StateMachine` |
| Methods | camelCase | `onStateMounted()`, `getConfig()` |
| Constants | SCREAMING_SNAKE_CASE | `INTRO_DURATION_MS` |
| File names | kebab-case | `ghost-runner-win.cpp` |
| Headers | `.hpp` extension | `ghost-runner-states.hpp` |
| Header guards | `#pragma once` | |
| Indentation | 4 spaces (no tabs) | |
| Braces | K&R (same line) | `void foo() {` |
| Commit format | `{type}: {desc} (#{issue})` | `feat: add X (#123)` |

## Cyonic's Core Design Principles

1. **StateMachine IS-A State** — enables composable state machines (state machines within state machines)
2. **App Pool Pattern** — all apps pre-allocated in Device's AppConfig map at startup
3. **Pause/Resume with Snapshots** — `onStatePaused()` returns `unique_ptr<Snapshot>` for state preservation
4. **Terminal States** — `isTerminalState()` returns true in managedMode, triggering `returnToPreviousApp()`
5. **Config/Session Separation** — Config is set-once difficulty tuning, Session is per-game runtime state

## Acceptance Criteria

- [ ] `docs/AGENT-REFERENCE.md` is complete and accurate
- [ ] `docs/DUPLICATION-ANALYSIS.md` reflects current state of codebase
- [ ] Conventions match Cyonic's actual code patterns (verified against commits)
- [ ] Documents are accessible to fleet agents
- [ ] CLAUDE.md references these docs for agent onboarding

## References

- Cyonic's commits: `git log --author="Elli Furedy"` or `git log --author="FinallyEve"`
- `include/state/state-machine.hpp` — StateMachine IS-A State implementation
- `include/game/minigame.hpp` — MiniGame base class
- `include/device/device.hpp` — Device with AppConfig and app switching
