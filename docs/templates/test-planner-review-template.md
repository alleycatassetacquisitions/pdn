# Test Planner Review Template

**Feature/Issue**: #___
**PR**: #___
**Reviewer**: [Agent ID or Human Name]
**Date**: YYYY-MM-DD

---

## 1. Behavior Enumeration

All behaviors must be explicitly tested using Given/When/Then format.

### Checklist

- [ ] **Happy path** tested for all primary user flows
- [ ] **State transitions** verified for all affected states
- [ ] **Message handling** tested for all new message types
- [ ] **Edge cases** identified and tested (boundary values, empty inputs, max values)

### Required Behaviors (Given/When/Then)

List each behavior as:

```
- [ ] Given [initial state], When [action], Then [expected outcome]
```

**Example:**
```
- [ ] Given device in IDLE state, When cable connected, Then transitions to CABLE_DETECTED
- [ ] Given match in progress, When button pressed before timeout, Then records draw time
- [ ] Given empty player roster, When attempting to start match, Then shows error state
```

---

## 2. Error Path Coverage

All error conditions must be tested, not just happy paths.

### Required Error Scenarios

- [ ] **Invalid inputs**: Malformed data, out-of-range values, null/empty strings
- [ ] **State violations**: Invalid state transitions, operations in wrong state
- [ ] **Resource exhaustion**: Buffer overflow, memory allocation failures (where applicable)
- [ ] **Timeout conditions**: Network timeouts, user inaction timeouts
- [ ] **Concurrent operations**: Race conditions, re-entrant calls (if applicable)

### Error Path Test Cases

```
- [ ] Given [precondition], When [error condition], Then [graceful handling]
```

**Example:**
```
- [ ] Given device awaiting response, When timeout expires, Then returns to IDLE
- [ ] Given invalid JSON message, When parsed, Then logs error and ignores message
- [ ] Given full player roster, When adding new player, Then rejects with error
```

---

## 3. Regression Guard Requirements

Features fixing bugs or redesigning existing systems must include regression tests.

### Checklist

- [ ] **Root cause** of original bug documented in test comments
- [ ] **Reproduction case** from bug report included as test case
- [ ] **Related edge cases** that could reintroduce the bug are tested
- [ ] **Test names** clearly indicate what regression they prevent

### Regression Test Cases

```
- [ ] Test: [descriptive name referencing original bug]
  - Issue: #___
  - Scenario: [what broke before]
  - Verification: [what must stay fixed]
```

**Example:**
```
- [ ] Test: ghostRunnerDoesNotCrashOnEmptyPuzzleArray
  - Issue: #225
  - Scenario: Empty puzzle array caused null pointer dereference
  - Verification: Gracefully handles empty array, shows error state
```

---

## 4. Cable Disconnect Scenario Check

**Required for**: All FDN minigames and any feature involving serial cable communication.

### Checklist

- [ ] **During gameplay**: Cable disconnect triggers appropriate cleanup
- [ ] **During state transition**: Mid-transition disconnect handled gracefully
- [ ] **During message send**: Incomplete send operations don't leave stale state
- [ ] **Reconnection**: Device can recover and resume or restart cleanly

### Test Cases

```
- [ ] Given [game state], When cable disconnected, Then [cleanup + state transition]
- [ ] Given cable disconnected, When reconnected, Then [recovery behavior]
```

**Example:**
```
- [ ] Given Ghost Runner puzzle active, When cable disconnected, Then clears puzzle state and returns to IDLE
- [ ] Given Exploit Sequencer round in progress, When cable disconnected, Then cancels round and shows disconnect message
```

---

## 5. Managed Mode Scenario Check

**Required for**: Features that behave differently when controlled by an external FDN server vs. standalone operation.

### Checklist

- [ ] **Standalone mode**: Feature works without FDN server connection
- [ ] **Managed mode**: Feature respects FDN server instructions
- [ ] **Mode transitions**: Switching between managed/standalone doesn't corrupt state
- [ ] **Permission checks**: Managed-only features blocked in standalone mode

### Test Cases

```
- [ ] Given [standalone mode], When [feature invoked], Then [expected behavior]
- [ ] Given [managed mode], When [feature invoked], Then [respects server control]
- [ ] Given [mode transition], When [feature active], Then [handles transition cleanly]
```

**Example:**
```
- [ ] Given standalone mode, When player initiates quickdraw, Then match starts locally
- [ ] Given managed mode, When FDN server sends match command, Then match starts with server config
- [ ] Given managed mode active, When FDN disconnects mid-match, Then gracefully transitions to standalone
```

---

## 6. Test Implementation Quality

### Code Review Checklist

- [ ] **Test names** are descriptive and indicate what's being tested
- [ ] **Arrange/Act/Assert** pattern followed in test bodies
- [ ] **No sleeps or arbitrary waits** (use deterministic mocks/timers)
- [ ] **No skipToState() abuse** (tests should drive state via real transitions where possible)
- [ ] **Assertions are specific** (avoid ASSERT_TRUE for complex conditions, use ASSERT_EQ with expected values)
- [ ] **Test isolation** (tests don't depend on execution order, clean up state)

### Anti-Patterns to Reject

- [ ] **Shallow assertions**: `ASSERT_TRUE(state != nullptr)` without checking behavior
- [ ] **Hardcoded session values**: Manually setting game state instead of driving via API
- [ ] **Disabled tests**: GTEST_SKIP without corresponding fix issue/PR
- [ ] **Overly broad mocks**: Mocking entire systems instead of just external dependencies

---

## 7. Coverage Analysis

### Metrics

- **Files changed**: ___
- **Test files added/modified**: ___
- **Test cases added**: ___
- **Lines of production code**: ___
- **Lines of test code**: ___
- **Test-to-code ratio**: ___ (aim for 1:1 or higher for complex logic)

### Coverage Gaps

List any code paths that are NOT tested and explain why:

```
- [ ] [File:function] - [reason not tested, e.g., "hardware-only", "legacy code"]
```

---

## 8. Approval Criteria

**This PR may be approved if:**

- [ ] All sections above are completed
- [ ] All required test cases are implemented and passing
- [ ] No regressions introduced (existing tests still pass)
- [ ] Test code follows project conventions (CLAUDE.md)
- [ ] CI pipeline passes (build + test)

**Conditional approval** (requires follow-up work):

- [ ] Tests pass but coverage gaps exist → Create follow-up issue: #___
- [ ] Minor test quality issues → Document in PR comments, fix in next iteration

**Rejection criteria:**

- [ ] No tests included for new functionality
- [ ] Tests don't cover error paths
- [ ] Regression guards missing for bug fixes
- [ ] Tests use anti-patterns (skipToState abuse, GTEST_SKIP, etc.)

---

## Notes & Recommendations

[Free-form section for reviewer commentary]

**Strengths:**

**Concerns:**

**Follow-up issues:**

---

**Test Planner Sign-off**: ____________
**Date**: ____________
