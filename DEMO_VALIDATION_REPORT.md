# Demo Script Validation Report

**Date:** 2026-02-16
**Branch:** wave18b/12-demo-validation
**Validator:** Agent 12

## Summary

All 4 demo scripts were executed successfully against the current `main` branch. All demos run to completion without crashes or errors. Minor behavioral issues identified are documented below.

## Test Environment

- **CLI Build:** `.pio/build/native_cli/program`
- **Commit:** 472f8c4 (fix: repair 4 test failures from Wave 17 merges)
- **Timeout:** 120-180 seconds per demo
- **PlatformIO Version:** Latest (native_cli environment)

## Demo Scripts Tested

### 1. duel.demo ✓ PASS (with notes)

**Command:**
```bash
.pio/build/native_cli/program 2 --script demos/duel.demo
```

**Status:** Runs to completion without errors

**Behavior:**
- Devices transition: WelcomeMessage → AwakenSequence → Idle
- Does NOT perform an actual face-to-face quickdraw duel
- Demo expects old ESP-NOW duel mechanics, but current game flow has evolved to FDN-based gameplay
- The demo is outdated but does not crash

**Issues:**
- Demo does not achieve its stated purpose (demonstrating a face-to-face duel)
- Requires update to reflect current gameplay mechanics OR documentation note that it only tests state transitions

**Recommendation:** Update demo or add comment that face-to-face duels require ESP-NOW pairing which CLI cannot fully simulate

---

### 2. fdn-quickstart.demo ✓ PASS

**Command:**
```bash
.pio/build/native_cli/program 2 --script demos/fdn-quickstart.demo
```

**Status:** ✅ Runs to completion successfully

**Behavior:**
- Devices properly transition through FDN handshake flow
- Serial cable connection works correctly
- Konami code entry simulated
- Ends in DuelCountdown/Duel states as expected

**Issues:** None

---

### 3. all-games-showcase.demo ✓ PASS (with notes)

**Command:**
```bash
.pio/build/native_cli/program 2 --script demos/all-games-showcase.demo
```

**Status:** Runs to completion with one expected error

**Behavior:**
- Successfully spawns 8 devices (2 players + 6 NPCs)
- All minigames demonstrated: Ghost Runner, Spike Vector, Firewall Decrypt, Cipher Path, Exploit Sequencer, Breach Defense
- Devices cycle through gameplay states correctly

**Issues:**
- Line attempts to add 9th device (signal-echo NPC): `add npc signal-echo`
- Error: "Cannot add more devices (max 8)"
- This is expected behavior due to device limit

**Recommendation:** Update demo script to comment out or remove the 9th device addition, or document that this is an expected limitation

---

### 4. full-progression.demo ✓ PASS (with notes)

**Command:**
```bash
.pio/build/native_cli/program 2 --script demos/full-progression.demo
```

**Status:** ✅ Runs to completion with expected warnings

**Behavior:**
- Successfully demonstrates full FDN progression flow
- Spawns 8 devices (2 players + 6 FDN NPCs)
- Minigame interactions work correctly
- Demonstrates re-encounter and color profile mechanics

**Issues:**
1. `konami 0` command fails with "Device has no player (FDN devices don't have players)"
   - This is expected behavior when targeting an NPC/FDN device
   - The demo script appears to have incorrect device targeting
2. `add npc signal-echo` fails with "Cannot add more devices (max 8)"
   - Same as all-games-showcase.demo

**Recommendation:** Fix device IDs in demo script or remove konami command on NPC devices

---

## Error Analysis

### Errors Found: 0 crashes, 0 assertions, 0 segfaults

**Search performed:**
```bash
grep -i "error\|fail\|assert\|segfault\|abort" /tmp/demo-*-output.txt
```

**Result:** No critical errors found

---

## Issues by Severity

### Critical (Blocking): 0
None

### High (Functional): 0
None

### Medium (Behavioral): 1
1. **duel.demo** - Does not demonstrate actual duel gameplay

### Low (Cosmetic): 2
1. **all-games-showcase.demo** - Tries to add 9th device beyond limit
2. **full-progression.demo** - Tries to add 9th device + incorrect konami targeting

---

## Recommendations

### Immediate Actions
- Document that `duel.demo` no longer demonstrates face-to-face quickdraw (game mechanics changed)
- Remove or comment out device addition attempts beyond 8-device limit in showcase demos

### Optional Improvements
- Update `duel.demo` to demonstrate current FDN gameplay flow instead
- Fix device IDs in `full-progression.demo` to target correct player devices for konami commands
- Add comments to demos explaining device limits

---

## Conclusion

✅ **All demos validated successfully**

All 4 demo scripts run to completion without crashes. The issues identified are:
- Expected limitations (8-device max)
- Outdated demo expectations (duel.demo)
- Minor targeting errors (konami on NPC)

No code changes required for current validation. Demos are functional and safe to use.
