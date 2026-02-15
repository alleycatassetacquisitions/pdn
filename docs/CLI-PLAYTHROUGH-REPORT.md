# CLI Simulator Playthrough Report — Agent 01

> Date: 2026-02-15
> Build: native_cli (latest main, commit f5ed815)
> Goal: Play through all 7 Konami games, enter code sequence, unlock color palettes

## Executive Summary

**The FDN encounter flow is broken.** A handshake deadlock prevents games from launching
via the cable connection pathway. Standalone game mode partially works but games get stuck
in EchoPlayerInput. The full Konami progression (7 games → code entry → color palettes)
cannot be completed in the current build.

---

## Critical Findings

### BUG 1: FdnDetected Handshake Deadlock (BLOCKER)

**Severity:** Critical — completely blocks the FDN game flow
**Matches:** Issue #143

**Root Cause:** Race condition in the NPC handshake protocol.

1. Player sends `smac<MAC>` once → NpcIdle receives it
2. NpcIdle uses `smac` to trigger transition to NpcHandshake (consumes the message)
3. NpcHandshake waits for a **second** `smac` to reply with `fack`
4. Player waits for `fack` that will never arrive
5. **DEADLOCK** — both sides waiting forever

**Evidence:**
```
Device 0010 stays in FdnDetected for the entire demo run.
NPC transitions NpcIdle → NpcHandshake but never sends fack.
After 10s timeout, FdnDetected → ConnectionLost.
```

**Fix:** NpcIdle should send `fack` immediately when it receives `smac`, before transitioning
to NpcHandshake. Or NpcHandshake should send `fack` unconditionally in onStateMounted.

**Files:**
- `src/game/fdn-states/npc-idle.cpp` (lines 66-71: consumes smac without replying)
- `src/game/fdn-states/npc-handshake.cpp` (lines 26-36: waits for second smac)
- `src/game/quickdraw-states/fdn-detected-state.cpp` (lines 61-67: sends smac once)

---

### BUG 2: Standalone Game Doesn't Reach Win/Lose

**Severity:** High — standalone testing is the only working path

When using `add challenge signal-echo`, the game transitions through:
- EchoIntro → EchoShowSequence → EchoPlayerInput → (loops back to ShowSequence)

But never reaches EchoWin or EchoLose. The game cycles between ShowSequence and
PlayerInput across rounds, but the win condition (`currentRound >= numSequences`) either
isn't met within the button presses attempted, or there's a transition issue.

**Possible causes:**
- Default easy config has more rounds than expected
- Random sequence mismatch (can't see display to know correct inputs)
- Missing evaluate transition in standalone mode

---

### BUG 3: "pure virtual method called" Crash

**Severity:** Medium — crash on script completion

Every script run ends with:
```
pure virtual method called
terminate called without an active exception
timeout: the monitored command dumped core
```

This suggests a virtual destructor or method is being called on a deleted object
during shutdown. Likely related to StateMachine/State cleanup ordering.

---

### BUG 4: `konami` Command Device Selection

**Severity:** Low — CLI usability issue

After `add npc <game>`, the new NPC becomes the selected device. Subsequent
`konami 0` targets the NPC (which has no player), returning "Device has no player."
The demo scripts all fail to check konami progress because of this.

**Fix:** `konami` should accept device IDs (e.g., `konami 0010`), or `add npc`
should not change the selected device.

---

## Missing Features for Playable Terminal Experience

### MISSING 1: No Real-Time Display in Script Mode

The NativeDisplayDriver captures all drawText/drawXBMP content, and there's a
braille renderer. But:
- `captions on` and `mirror on` produce no visible output in script mode
- The display content is only visible in the live terminal dashboard
- Script output only shows state names, not what the player would see on screen

### MISSING 2: No Way to See Game Sequence Before Input

Signal Echo shows a sequence (UP/DOWN pattern), then asks the player to repeat it.
In the CLI, there's no way to see what was shown — button presses are blind guesses.
Need: display captions visible in script output, or a `display` command that prints
current screen content.

### MISSING 3: Interactive Keyboard Controls

Currently: must type `b 0` or `b2 0` as text commands.
Desired: arrow keys or keyboard shortcuts for instant input during gameplay.
The dashboard has UP/DOWN arrow bindings but they only work in live terminal mode.

### MISSING 4: NPC Display Content

NPC devices show game titles on their "display" but this isn't visible in the
CLI output. Need: multi-device display rendering showing both player and NPC screens.

### MISSING 5: XBM Graphics Not Rendered

The NativeDisplayDriver captures XBM bitmap data and can render it as braille
Unicode art. But this is only available via `mirror on` in live mode, and even
then it's a toggle, not an automatic overlay.

---

## Features Not Tested (Blocked by BUG 1)

| Feature | Status | Blocker |
|---------|--------|---------|
| Beat all 7 minigames | NOT TESTED | FdnDetected deadlock |
| Konami progress tracking | NOT TESTED | Can't complete games |
| Konami code entry (puzzle #8) | NOT TESTED | Need all 7 buttons first |
| Hard mode unlock via boon | NOT TESTED | Need Konami code completion |
| Color palette unlock | NOT TESTED | Need hard mode wins |
| Color palette equip | NOT TESTED | Need palette unlocks |
| FdnReencounter difficulty choice | NOT TESTED | Need first completion |
| KonamiMetaGame state routing | NOT TESTED | Need all 7 buttons |

---

## Spec Gap Analysis

Checked against `docs/GAME-MECHANICS.md` and `docs/ARCHITECTURE.md`:

| Feature | Spec'd? | Implemented? | Notes |
|---------|---------|-------------|-------|
| FDN handshake protocol | Yes (ARCHITECTURE.md) | Broken | Deadlock in smac/fack exchange |
| 7-game progression | Yes (GAME-MECHANICS.md) | Partially | Games exist but can't be reached |
| Konami code entry | Yes | Implemented (KonamiCodeEntry) | Not testable yet |
| Hard mode routing | Yes | Implemented (FdnReencounter) | Not testable yet |
| Color profiles | Yes | Partially | ColorProfilePrompt exists |
| Standalone game mode | Not spec'd | Exists (`add challenge`) | Partially working |
| CLI display rendering | Not spec'd | Partial (captions/mirror) | Not visible in scripts |
| Interactive play from terminal | Not spec'd | Missing | Need keyboard input mode |

---

## Recommended Priority

1. **Fix FdnDetected deadlock** — BUG 1 (Critical, blocks everything)
2. **Fix "pure virtual method called"** — BUG 3 (Medium, causes data loss)
3. **Add display content to script output** — MISSING 1 (High, needed for testing)
4. **Fix `konami` device selection** — BUG 4 (Low, quick fix)
5. **Add interactive keyboard mode** — MISSING 3 (High, user experience)
6. **Multi-device display rendering** — MISSING 4 (Medium, nice to have)

---

*Generated by Agent 01 — 2026-02-15*
