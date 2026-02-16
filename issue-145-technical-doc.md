## Technical Implementation Document: Issue #145

### Summary
**Current status**: KonamiPuzzle properly cleans up secondary button callbacks (as of commit 1c4b417). However, **Duel state has the identical bug** - it registers callbacks on both buttons but never removes them.

### Root Cause (Confirmed)

**File: `src/game/quickdraw-states/duel-state.cpp`**

Duel state registers callbacks on both buttons in `onStateMounted()` but NEVER calls `removeButtonCallbacks()` in `onStateDismounted()`:

```cpp
// onStateMounted (lines 61-65):
void Duel::onStateMounted(Device *PDN) {
    // ... setup code ...

    PDN->getPrimaryButton()->setButtonPress(
        matchManager->getDuelButtonPush(), matchManager, ButtonInteraction::CLICK);

    PDN->getSecondaryButton()->setButtonPress(
        matchManager->getDuelButtonPush(), matchManager, ButtonInteraction::CLICK);  // ❌ LEAKED

    // ... more setup ...
}

// onStateDismounted (lines 128-137):
void Duel::onStateDismounted(Device *PDN) {
    LOG_I(DUEL_TAG, "Duel state dismounted - Cleanup");

    duelTimer.invalidate();
    LOG_I(DUEL_TAG, "Duel timer invalidated");

    transitionToDuelReceivedResultState = false;
    transitionToIdleState = false;
    transitionToDuelPushedState = false;
    // ❌ MISSING: PDN->getPrimaryButton()->removeButtonCallbacks();
    // ❌ MISSING: PDN->getSecondaryButton()->removeButtonCallbacks();
}
```

**Why this is broken:**

1. **Button drivers hold dangling pointers**: The callbacks registered at lines 61-65 store a pointer to `matchManager`. When the state is dismounted without cleanup, the button drivers retain these callbacks indefinitely.

2. **Shared MatchManager lifetime**: Unlike states that pass `this` as the callback context, Duel passes `matchManager` (which has longer lifetime). This makes the bug LESS likely to cause immediate crashes, but still represents:
   - **Resource leak**: Callbacks accumulate on re-entry to Duel state
   - **Stale behavior**: Old callbacks may fire with outdated state
   - **Memory pressure**: Each callback closure allocates heap memory

3. **Pattern violation**: Every other state that registers button callbacks (18+ states) properly calls `removeButtonCallbacks()` in `onStateDismounted()`. Duel state is the ONLY exception.

**Comparison with correct pattern** (from `src/game/quickdraw-states/color-profile-picker.cpp:82-86`):

```cpp
void ColorProfilePicker::onStateDismounted(Device* PDN) {
    profileList.clear();
    PDN->getPrimaryButton()->removeButtonCallbacks();       // ✅ Primary cleaned
    PDN->getSecondaryButton()->removeButtonCallbacks();     // ✅ Secondary cleaned
}
```

### Implementation Plan

#### Changes Required

**1. File: `src/game/quickdraw-states/duel-state.cpp`**
   - **Lines 128-137**: Add button callback cleanup in `onStateDismounted()`

   **Old code:**
   ```cpp
   void Duel::onStateDismounted(Device *PDN) {
       LOG_I(DUEL_TAG, "Duel state dismounted - Cleanup");

       duelTimer.invalidate();
       LOG_I(DUEL_TAG, "Duel timer invalidated");

       transitionToDuelReceivedResultState = false;
       transitionToIdleState = false;
       transitionToDuelPushedState = false;
   }
   ```

   **New code:**
   ```cpp
   void Duel::onStateDismounted(Device *PDN) {
       LOG_I(DUEL_TAG, "Duel state dismounted - Cleanup");

       duelTimer.invalidate();
       LOG_I(DUEL_TAG, "Duel timer invalidated");

       // Clean up button callbacks registered in onStateMounted
       PDN->getPrimaryButton()->removeButtonCallbacks();
       PDN->getSecondaryButton()->removeButtonCallbacks();

       transitionToDuelReceivedResultState = false;
       transitionToIdleState = false;
       transitionToDuelPushedState = false;
   }
   ```

   **Rationale**:
   - Matches the cleanup pattern used by ALL other button-registering states
   - Prevents callback accumulation on repeated Duel state entry
   - Follows RAII principle: acquire in `onStateMounted()`, release in `onStateDismounted()`
   - Button drivers expect callbacks to be cleaned up per state lifecycle

#### Similar Patterns to Fix

**✅ KonamiPuzzle** (mentioned in issue title): Already fixed as of commit 1c4b417
   - File: `src/game/quickdraw-states/konami-puzzle-state.cpp:130-131`
   - Status: Properly calls `removeButtonCallbacks()` on both buttons

**States verified to be correct** (all properly clean up secondary buttons):
- `AllegiancePickerState` (lines 38-39)
- `ChooseRoleState` (lines dismount)
- `ColorProfilePicker` (lines 84-85)
- `ColorProfilePrompt` (lines dismount)
- `ConfirmOfflineState` (lines dismount)
- `DuelCountdown` (lines dismount)
- `FdnReencounter` (lines dismount)
- `Idle` (lines 126-127)
- `PlayerRegistration` (lines dismount)

**Only Duel state is broken.**

### Verification

```bash
# Build and run CLI tests
pio run -e native_cli_test
pio test -e native_cli_test -vvv

# Specific tests for Duel state callback cleanup:
# 1. Check for memory leaks on repeated Duel entry
# 2. Verify callbacks don't fire after state transition
# 3. Confirm no dangling pointer access

# Recommended new test to add in test/test_cli/:
TEST_F(DuelTestSuite, ButtonCallbacksRemovedOnDismount) {
    // Mount Duel state
    duelState->onStateMounted(device->pdn);

    // Verify callbacks are registered
    int primaryCallbackCount = device->pdn->getPrimaryButton()->getCallbackCount();
    int secondaryCallbackCount = device->pdn->getSecondaryButton()->getCallbackCount();
    EXPECT_GT(primaryCallbackCount, 0);
    EXPECT_GT(secondaryCallbackCount, 0);

    // Dismount state
    duelState->onStateDismounted(device->pdn);

    // Verify callbacks are removed
    EXPECT_EQ(device->pdn->getPrimaryButton()->getCallbackCount(), 0);
    EXPECT_EQ(device->pdn->getSecondaryButton()->getCallbackCount(), 0);
}
```

### Risk Assessment

- **Blast radius**: Duel state only (core gameplay state, frequently entered)
  - Affects ALL duels between players
  - High-traffic code path

- **Regression risk**: VERY LOW
  - Change adds missing cleanup, doesn't alter control flow
  - Identical pattern used by 18+ other states
  - `removeButtonCallbacks()` is idempotent (safe if called multiple times)
  - No dependencies on callback persistence between state transitions

- **Test coverage**: MODERATE
  - CLI tests exercise Duel state entry/exit cycles
  - Existing comprehensive integration tests cover duel flows
  - Missing: explicit test for callback cleanup (recommended above)

### Triage Learnings

- **Reference**: Issue #144 (Device destructor), Issue #142 (TearDown ordering)
- **Pattern**: Resource cleanup must be symmetric with resource acquisition
  - ALL states that call `setButtonPress()` MUST call `removeButtonCallbacks()` in `onStateDismounted()`
  - This is not optional - button drivers accumulate callbacks indefinitely until removed
  - Even if the callback context has longer lifetime (like `matchManager`), the callback itself is state-specific

- **Prevention**:
  1. **Code review checklist**: "Does this state register button callbacks? If yes, does `onStateDismounted()` remove them?"
  2. **Static analysis rule**: Flag any `onStateMounted()` with `setButtonPress()` that lacks corresponding `removeButtonCallbacks()` in `onStateDismounted()`
  3. **Unit test template**: Every state with button callbacks should have a test like:
     ```cpp
     TEST_F(StateTest, ButtonCallbacksCleanedUpOnDismount) {
         state->onStateMounted(device);
         EXPECT_GT(device->getPrimaryButton()->getCallbackCount(), 0);
         state->onStateDismounted(device);
         EXPECT_EQ(device->getPrimaryButton()->getCallbackCount(), 0);
     }
     ```
  4. **Architecture consideration**: Consider RAII wrapper for button callbacks:
     ```cpp
     class ScopedButtonCallback {
         Button* button_;
     public:
         ScopedButtonCallback(Button* btn, callbackFunction cb) : button_(btn) {
             button_->setButtonPress(cb);
         }
         ~ScopedButtonCallback() {
             button_->removeButtonCallbacks();
         }
     };
     ```

- **Root cause of issue #145 confusion**: KonamiPuzzle was likely broken in an earlier version, then fixed in commit 1c4b417 ("Fix inverted button mappings in color picker and konami puzzle") or earlier. The issue may have been filed against old code. However, the pattern IS still present in Duel state, making this a valid bug class.

---

*Technical document by Senior Engineer Agent 02*
