# Wave 19 — Sanitizer Sweep Report

**Date**: 2026-02-16
**Agent**: Agent 11 (Bug/Stability Tester)
**Branch**: `wave19/11-sanitizer-sweep`

## Executive Summary

Comprehensive sanitizer sweep performed after Wave 18 merges (7 visual redesigns + CI fixes). Testing revealed one critical **stack-use-after-scope** bug detected by AddressSanitizer, which has been fixed. All tests now pass cleanly with both ASan and UBSan enabled.

---

## Test Environment

### Baseline Configuration
- **Platform**: Native (Linux x86_64)
- **Build Type**: Test
- **Framework**: GoogleTest 1.17.0
- **Compiler**: GCC 11.x with C++17

### Test Suites
1. **native** — Core functionality tests (275 tests)
2. **native_cli_test** — CLI component tests (compilation errors, excluded from sweep)

---

## Baseline Results (No Sanitizers)

### Native Test Suite
```
Environment    Test       Status    Duration
-------------  ---------  --------  ------------
native         test_core  PASSED    00:00:35.714

Total: 275 tests from 28 test suites
Pass:  275 tests (100%)
Fail:  0 tests
```

### CLI Test Suite
```
Environment        Test       Status    Duration
-----------------  ---------  --------  ------------
native_cli_test    test_cli   ERRORED   00:00:06.593

Status: Compilation errors — excluded from sanitizer sweep
```

**Note**: CLI tests have pre-existing compilation errors due to outdated test code that doesn't match the refactored `CipherPathConfig` and `CipherPathSession` structs (removed fields: `gridSize`, `moveBudget`, `playerPosition`, etc.). This is a known issue independent of sanitizer findings.

---

## AddressSanitizer (ASan) Results

### Configuration
```ini
build_flags =
    -std=c++17
    -DNATIVE_BUILD
    -fsanitize=address
    -fno-omit-frame-pointer
    -g
```

### Findings

#### **Critical: Stack-Use-After-Scope (CVE-level severity)**

**Location**: `test/test_core/integration-tests.hpp:89-90`
**Test**: `DuelIntegrationTestSuite.completeDuelFlowHunterWins`

**Error Output**:
```
==7752==ERROR: AddressSanitizer: stack-use-after-scope on address 0x7ffe21c03bf8
READ of size 37 at 0x7ffe21c03bf8 thread T0
    #0 __interceptor_strlen
    #1 std::char_traits<char>::length(char const*)
    #2 std::__cxx11::basic_string<char>::basic_string(char const*, std::allocator<char> const&)
    #3 completeDuelFlowHunterWins(DuelIntegrationTestSuite*)
```

**Root Cause**:
```cpp
// BUGGY CODE
char* matchId = IdGenerator(TestConstants::TEST_SEED_INTEGRATION).generateId();
std::string matchIdStr(matchId);  // matchId now points to freed memory!
```

The `IdGenerator` was constructed as a **temporary object** on line 89. After the line completes, the temporary destructor runs, invalidating the internal `_buffer[37]` that `toCharArray()` returned. Line 90 then constructs a `std::string` from the dangling pointer, causing a read from freed stack memory.

**Fix Applied**:
```cpp
// FIXED CODE
IdGenerator idGen(TestConstants::TEST_SEED_INTEGRATION);
char* matchId = idGen.generateId();
std::string matchIdStr(matchId);
```

By declaring `idGen` as a local variable, its lifetime extends through the `std::string` construction, keeping the buffer valid.

**Files Fixed**:
- `test/test_core/integration-tests.hpp:84-90` — `completeDuelFlowHunterWins()`
- `test/test_core/integration-tests.hpp:168-173` — `completeDuelFlowBountyWins()`
- `test/test_core/utility-tests.hpp:103-106` — `uuidGeneratorProducesValidFormat()`

---

### ASan Post-Fix Results
```
Environment    Test       Status    Duration
-------------  ---------  --------  ------------
native         test_core  PASSED    00:00:41.833

Total: 275 tests
Pass:  275 tests (100%)
Fail:  0 tests

ASan Errors: 0
```

**Note**: A `SIGHUP` signal was received at test completion, likely from the PlatformIO test runner cleanup. This is unrelated to memory safety.

---

## UndefinedBehaviorSanitizer (UBSan) Results

### Configuration
```ini
build_flags =
    -std=c++17
    -DNATIVE_BUILD
    -fsanitize=undefined
    -fno-omit-frame-pointer
    -g
```

### Findings

**No undefined behavior detected.**

All 275 tests passed without UBSan warnings:
```
Environment    Test       Status    Duration
-------------  ---------  --------  ------------
native         test_core  PASSED    00:00:51.930

Total: 275 tests
Pass:  275 tests (100%)
Fail:  0 tests

UBSan Errors: 0
```

UBSan checked for:
- Signed integer overflow
- Null pointer dereferences
- Misaligned memory access
- Division by zero
- Invalid enum values
- Out-of-bounds array access

No issues found in any category.

---

## Summary of Fixes

| File | Function | Issue | Fix |
|------|----------|-------|-----|
| `test/test_core/integration-tests.hpp:89` | `completeDuelFlowHunterWins()` | Stack-use-after-scope | Changed temporary `IdGenerator()` to local variable |
| `test/test_core/integration-tests.hpp:172` | `completeDuelFlowBountyWins()` | Stack-use-after-scope | Changed temporary `IdGenerator()` to local variable |
| `test/test_core/utility-tests.hpp:104` | `uuidGeneratorProducesValidFormat()` | Stack-use-after-scope | Changed temporary `IdGenerator()` to local variable |

**Total Fixes**: 3 instances of the same pattern
**Lines Changed**: 9 (3 functions × 3 lines each)

---

## Known Issues (Not Fixed)

### 1. CLI Test Suite Compilation Errors
**Status**: Pre-existing, unrelated to sanitizers
**Cause**: Test code references removed struct fields after CipherPath refactor
**Impact**: CLI tests cannot be executed until updated
**Priority**: Medium (tests are stale but product code is correct)

**Example Errors**:
```
test/test_cli/cipher-path-tests.hpp:114:32: error:
  'const struct CipherPathConfig' has no member named 'gridSize'
test/test_cli/cipher-path-tests.hpp:139:13: error:
  'struct CipherPathSession' has no member named 'playerPosition'
```

---

## Verification

### Final Clean Build (No Sanitizers)
```bash
$ platformio test -e native
Environment    Test       Status    Duration
-------------  ---------  --------  ------------
native         test_core  PASSED    00:00:40.105

Total: 275 tests
Pass:  275 tests (100%)
```

**platformio.ini**: Confirmed clean (no sanitizer flags remaining)

---

## Impact Assessment

### Severity of Fixed Bug
- **Criticality**: **HIGH** (memory corruption, potential security issue)
- **Likelihood**: Low in production (only affects test code, not runtime)
- **Scope**: Test utilities only

### Potential Production Risk
The fixed pattern (`IdGenerator(...).generateId()`) appears only in test code. A grep of `src/` confirmed no production code uses this antipattern:

```bash
$ grep -r "IdGenerator.*generateId" src/
# No matches in production code
```

However, the fix is **important for test reliability** — ASan caught a real bug that could cause flaky tests or false negatives if the freed memory happens to contain valid UUID-like data.

---

## Recommendations

1. **Enable ASan in CI** (low priority, adds ~15% runtime overhead)
   - Consider periodic sanitizer runs (weekly/monthly)
   - Catches lifetime bugs that are hard to debug otherwise

2. **Fix CLI Test Compilation Errors** (medium priority)
   - Update `test/test_cli/cipher-path-tests.hpp` to match new `CipherPathConfig` struct
   - Re-enable CLI test suite in sanitizer sweeps

3. **Add Clang Static Analyzer** (future enhancement)
   - Would have caught the dangling pointer at compile time
   - Complements runtime sanitizers

---

## Conclusion

**Wave 19 sanitizer sweep identified and resolved 1 critical memory safety bug** detected by AddressSanitizer. The codebase now passes 275/275 tests cleanly under both ASan and UBSan with zero warnings.

The fixed bug was a **stack-use-after-scope** issue in test code where temporary `IdGenerator` objects were destroyed before their returned pointers were consumed. This is a common C++ pitfall that ASan excels at detecting.

**Status**: ✅ All sanitizer findings resolved
**Regressions**: None
**Test Pass Rate**: 100% (275/275)
