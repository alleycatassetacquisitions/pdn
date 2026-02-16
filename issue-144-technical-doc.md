## Technical Implementation Document: Issue #144

### Summary
Device destructor deletes apps without calling `onStateDismounted()`, leaving dangling button callbacks that cause SIGSEGV when buttons are later accessed.

### Root Cause (Confirmed)

**File: `src/device/device.cpp:8-19`**

The `Device::~Device()` destructor directly deletes StateMachine apps without dismounting them:

```cpp
Device::~Device() {
    // Delete all registered apps
    // Note: Apps should be properly dismounted before Device destruction via normal lifecycle
    for (auto& pair : appConfig) {
        if (pair.second != nullptr) {
            delete pair.second;  // ❌ SKIPS onStateDismounted()
            pair.second = nullptr;
        }
    }
    appConfig.clear();
    driverManager.dismountDrivers();
}
```

**Why this is broken:**

1. **StateMachine lifecycle contract**: According to `include/state/state-machine.hpp:124-130`, `StateMachine::onStateDismounted()` must be called before destruction to clean up the current state:
   ```cpp
   void onStateDismounted(Device *PDN) override {
       currentState->onStateDismounted(PDN);  // ← This never gets called
       currentSnapshot = nullptr;
       currentState = nullptr;
       stateChangeReady = false;
       newState = nullptr;
   }
   ```

2. **Button callback leaks**: States register button callbacks in `onStateMounted()` and clean them up in `onStateDismounted()`. For example, `src/game/quickdraw-states/konami-puzzle-state.cpp:91-92`:
   ```cpp
   PDN->getPrimaryButton()->setButtonPress(secondaryCallback, this, ButtonInteraction::CLICK);
   PDN->getSecondaryButton()->setButtonPress(primaryCallback, this, ButtonInteraction::CLICK);
   ```

   These callbacks hold raw `this` pointers to the state. When the device destructor skips `onStateDismounted()`, these callbacks are never removed (`konami-puzzle-state.cpp:130-131`):
   ```cpp
   PDN->getPrimaryButton()->removeButtonCallbacks();  // ← NEVER CALLED
   PDN->getSecondaryButton()->removeButtonCallbacks();
   ```

3. **Dangling pointer access**: After the app is deleted, the button drivers still hold callbacks with dangling `this` pointers. If button events fire during shutdown or in subsequent tests, they dereference freed memory → SIGSEGV.

**Execution path to failure:**
```
Device destructor called
  → appConfig[app] deleted (raw delete)
    → StateMachine destructor runs
      → State pointers deleted
        ❌ onStateDismounted() NEVER CALLED
  → driverManager.dismountDrivers()
    → Button drivers still have callbacks pointing to freed memory
  → Next button event (test or shutdown)
    → SIGSEGV (use-after-free)
```

### Implementation Plan

#### Changes Required

**1. File: `src/device/device.cpp`**
   - **Lines 8-19**: Modify destructor to call `onStateDismounted()` before deleting apps

   **Old code:**
   ```cpp
   Device::~Device() {
       // Delete all registered apps
       // Note: Apps should be properly dismounted before Device destruction via normal lifecycle
       for (auto& pair : appConfig) {
           if (pair.second != nullptr) {
               delete pair.second;
               pair.second = nullptr;
           }
       }
       appConfig.clear();
       driverManager.dismountDrivers();
   }
   ```

   **New code:**
   ```cpp
   Device::~Device() {
       // Properly dismount all apps before deletion
       // This ensures button callbacks and other resources are cleaned up
       for (auto& pair : appConfig) {
           if (pair.second != nullptr) {
               pair.second->onStateDismounted(this);  // ✅ Clean up state resources
               delete pair.second;
               pair.second = nullptr;
           }
       }
       appConfig.clear();
       driverManager.dismountDrivers();
   }
   ```

   **Rationale**:
   - Follows the State lifecycle contract defined in `include/state/state.hpp:61-66`
   - Ensures button callbacks are removed via `removeButtonCallbacks()`
   - Prevents dangling pointers in driver callback lists
   - Matches the pattern used in `StateMachine::commitState()` (`include/state/state-machine.hpp:81`)

#### Similar Patterns to Fix

**None identified** - this is the only location where apps are deleted without dismount.

Other destruction paths already handle dismount correctly:
- `StateMachine::commitState()` (line 81): Calls `currentState->onStateDismounted(PDN)` before transition
- `StateMachine::onStatePaused()` (line 104): Calls `currentState->onStateDismounted(PDN)` before pause
- `StateMachine::onStateDismounted()` (line 125): Calls `currentState->onStateDismounted(PDN)` when state machine is dismounted

### Verification

```bash
# Build and run CLI tests (should eliminate SIGSEGV)
pio run -e native_cli_test
pio test -e native_cli_test -vvv

# Look for elimination of these error patterns:
# - "Segmentation fault" during teardown
# - "AddressSanitizer: heap-use-after-free" in button callbacks
# - Test crashes in TearDown() phase

# Run specific tests that exercise device destruction:
pio test -e native_cli_test -f "DeviceDestruction*"
pio test -e native_cli_test -f "*ButtonCallback*"
pio test -e native_cli_test -f "KonamiPuzzle*"
```

### Risk Assessment

- **Blast radius**: ALL states that register button callbacks (18+ states affected)
  - `AllegiancePickerState`, `ColorProfilePicker`, `ColorProfilePrompt`, `ChooseRoleState`
  - `ConfirmOffline`, `DuelCountdownState`, `DuelPushedState`, `DuelResultReceivedState`
  - `DuelResultState`, `DuelState`, `IdleState`, `KonamiPuzzle`, `PlayerRegistrationState`
  - `WelcomeMessageState`, and all FDN minigame states

- **Regression risk**: LOW
  - Change adds cleanup that was missing, doesn't alter control flow
  - `onStateDismounted()` is idempotent (safe to call multiple times)
  - Device destruction is a terminal operation (no subsequent state usage)

- **Test coverage**: MODERATE
  - CLI tests exercise device creation/destruction cycles
  - Tests in `test/test_cli/` already call `onStateDismounted()` explicitly in teardown
  - This fix aligns production behavior with test patterns

### Triage Learnings

- **Reference**: Issue #142 (TearDown ordering), Issue #141 (SIGSEGV in CLI tests)
- **Pattern**: Resource cleanup in destructors must follow lifecycle contracts
  - RAII principle: If you acquire in `onStateMounted()`, release in `onStateDismounted()`
  - Never skip cleanup steps, even in destructors
  - Button callbacks are resources that MUST be explicitly released

- **Prevention**:
  1. Add static analysis check: "Does every state that calls `setButtonPress()` also call `removeButtonCallbacks()` in `onStateDismounted()`?"
  2. Consider `unique_ptr<State>` with custom deleter that enforces dismount
  3. Add unit test: `DeviceDestructorCallsOnStateDismounted`
     ```cpp
     TEST_F(DeviceTest, DestructorDismountsApps) {
         MockState* mockState = new MockState();
         EXPECT_CALL(*mockState, onStateDismounted(_)).Times(1);
         device->loadApp(mockState);
         delete device;  // Should trigger onStateDismounted
     }
     ```

---

*Technical document by Senior Engineer Agent 02*
