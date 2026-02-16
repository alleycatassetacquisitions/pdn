# Test Suite Health Report — Wave 18b / Wave 17 Merge Status

**Date**: 2026-02-16  
**Branch**: main (commit 472f8c4)  
**Agent**: Wave 18b Agent 11  

## Executive Summary

✅ **Core unit tests**: 275/275 PASSED  
⚠️  **CLI integration tests**: 174/182 passed, **7 FAILING**  

All 7 failures are **pre-existing** from Wave 17 merges. Commit `472f8c4` already fixed 4 device test failures (MockStateMachine lifecycle counters). The remaining 7 CLI failures require further investigation.

---

## Test Results

### Core Unit Tests (`pio test -e native`)
All 275 tests pass successfully. No issues found.

### CLI Integration Tests (`pio test -e native_cli_test`) 

**Status**: 7 pre-existing failures (confirmed by testing on `main`)

#### Failed Tests

1. **`BreachDefenseManagedTestSuite.ManagedModeReturns`**
   - Location: `test/test_cli/breach-defense-tests.hpp:442`
   - Type: Assertion failure

2. **`CableDisconnectTestSuite.CableDisconnectDuringIntro`**
   - Location: `test/test_cli/cable-disconnect-tests.hpp:78`
   - Type: Assertion failure

3. **`CableDisconnectTestSuite.CableDisconnectDuringGameplay`**
   - Location: `test/test_cli/cable-disconnect-tests.hpp:116`
   - Type: Assertion failure

4. **`CableDisconnectTestSuite.CableReconnectToDifferentNpc`**
   - Location: `test/test_cli/cable-disconnect-tests.hpp:163`
   - Type: Assertion failure

5. **`CipherPathManagedTestSuite.ManagedModeReturns`**
   - Location: `test/test_cli/cipher-path-tests.hpp:514`
   - **Failure**: `Value of: cp->getConfig().managedMode`
     - `Actual: false`
     - `Expected: true`
   - **Root cause**: FDN handshake launches Cipher Path but `managedMode` flag is not set to `true`

6. **`ComprehensiveIntegrationTestSuite.SignalEchoEasyWinUnlocksButton`**
   - Location: `test/test_cli/comprehensive-integration-tests.hpp:153`
   - Type: Assertion failure

7. **`ComprehensiveIntegrationTestSuite.SignalEchoHardWinUnlocksColorProfile`**
   - Location: `test/test_cli/comprehensive-integration-tests.hpp:196`
   - Type: Assertion failure, **followed by SIGSEGV** (segmentation fault)

---

## Root Cause Analysis

All failures appear to stem from Wave 17 PR merges:

### 1. Managed Mode Flag Not Set (1 failure + likely cause of another)
- **Affected**: Cipher Path, Breach Defense managed mode tests
- **Cause**: When FDN launch states create minigame instances, they may not be setting `config.managedMode = true`
- **Fix**: Review FDN launch state implementations (likely in `src/game/quickdraw.cpp` or FDN-related states)

### 2. Cable Disconnect Regression (3 failures)
- **Affected**: Intro, Gameplay, and Reconnect scenarios
- **Cause**: Serial disconnect handling may have regressed in Wave 17 changes
- **Fix**: Review serial cable broker and disconnect logic changes from Wave 17 PRs

### 3. Unlock Logic Failures (2 failures)
- **Affected**: Button unlock and color profile unlock after Signal Echo wins
- **Cause**: KonamiMetaGame integration or reward propagation logic may be broken
- **Fix**: Review button/profile award logic in KonamiMetaGame and FDN Complete states

### 4. Memory Safety Issue (segfault after test 7)
- **Cause**: Use-after-free or null pointer dereference, possibly in cleanup after failed tests
- **Fix**: Run under valgrind or gdb to identify corruption source

---

## Already Fixed

Commit `472f8c4` fixed 4 device test failures:
- **Issue**: `MockStateMachine` tests tried to access `mountedCount`, `loopCount`, `pausedCount`, `resumedCount`, `wasPaused` directly
- **Root cause**: `Device::shutdownApps()` deletes registered apps, causing use-after-free when tests checked counters
- **Fix**: Changed `MockStateMachine` to use `shared_ptr<LifecycleCounters>` that survive object deletion
- **Tests fixed**: All device lifecycle tests now pass (275/275 core tests)

---

## Recommendations

1. **Managed Mode** (Priority: High)
   - Check how FDN launch states construct minigame instances
   - Ensure `config.managedMode = true` is set when games are launched via FDN
   - May require changes in FDN handshake completion flow

2. **Cable Disconnect** (Priority: Medium)
   - Review serial disconnect handling from Wave 17 PRs
   - Verify cable broker state transitions are correct
   - Check if disconnect callbacks are still properly registered

3. **Unlock Logic** (Priority: Medium)
   - Verify button/profile award logic in KonamiMetaGame
   - Check FDN Complete state reward propagation
   - Confirm unlock callbacks are still being invoked

4. **Segfault** (Priority: High - Memory Safety)
   - Run failed tests under valgrind: `valgrind --leak-check=full pio test -e native_cli_test`
   - Use gdb to get backtrace at crash point
   - Likely related to cleanup after test failures

---

## Conclusion

**Wave 18b introduces NO new test failures.** All 7 CLI test failures are pre-existing from Wave 17 merges. The test suite is in a partially broken state that requires remediation before further Wave 18 development proceeds.

**Next Steps**:
1. Create separate PR(s) to fix the 7 CLI test failures
2. Run full test suite after each Wave 18 PR to prevent regression
3. Consider adding CI job to block merges if tests fail

---

**Generated by**: Wave 18b Agent 11 (Test Suite Health Check)  
**Verification**: Run `pio test -e native && pio test -e native_cli_test` on main to confirm
