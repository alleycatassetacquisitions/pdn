# Phase 2b: Test Planner Gate

**Purpose**: This gate ensures the Testing Spec is complete before implementation begins. The Test Planner reviews the spec and proposes additional tests for gaps.

**Timeline**: Runs after Phase 2 (SeniorEngineer spec) and before Phase 3 (JuniorDev implementation).

---

## Why This Gate Exists

Issue #277 revealed that Testing Specs were incomplete — they listed test names but not test function source code, and they missed edge cases. This caused:
- Testing Agent (Phase 4) had to guess what assertions to write
- Junior Dev implemented code that had no corresponding tests
- Edge cases were discovered during review instead of being tested upfront

Phase 2b fixes this by having the Test Planner review the Testing Spec and flag gaps before any code is written.

---

## Test Planner Responsibilities

The Test Planner is an autonomous agent that:
1. Reads the Implementation Spec and Testing Spec from the plan document
2. Cross-references against the issue success criteria
3. Identifies untested behaviors, edge cases, and error paths
4. Proposes additional tests with concrete setup/action/assert descriptions
5. Updates the plan document with findings

**The Test Planner does NOT write test code** — it reviews and proposes. The Testing Agent (Phase 4) writes the actual test functions.

---

## What the Test Planner Checks

### 1. Method Coverage
- **Question**: Does every public method in the Implementation Spec have at least one test?
- **Check**: For each new or modified method signature, search the Testing Spec for a test that exercises it.
- **Flag**: Any public method with zero tests.

**Example gap:**
- Implementation Spec adds `SpikeVector::generateGapPositions(int wallCount, int numLanes, int maxGapDistance)`
- Testing Spec has no test for this method
- **Proposal**: Add test "Gap positions respect max distance constraint"

---

### 2. State Transition Coverage
- **Question**: Are all state transitions tested?
- **Check**: For each `transition("StateName")` call in the Implementation Spec, verify a test exercises that path.
- **Flag**: Any transition with no corresponding test.

**Example gap:**
- Implementation Spec shows `transition("NextLevel")` when level complete
- Testing Spec has no test for level completion
- **Proposal**: Add test "Level complete triggers transition to NextLevel"

---

### 3. Edge Case Coverage
- **Question**: Are boundary conditions and edge cases tested?
- **Check**: Look for:
  - Loop boundaries (first/last iteration, empty arrays)
  - Null/zero/negative inputs
  - Max capacity limits (e.g., max walls, max hits)
  - Concurrent events (button press during animation)
- **Flag**: Any obvious edge case without a test.

**Example gap:**
- Implementation Spec shows collision check when `wallX <= CURSOR_X`
- Testing Spec only tests collision at `wallX == CURSOR_X` (exact match)
- **Proposal**: Add test "Collision detected when wall slightly past cursor" with `wallX = CURSOR_X - 1`

---

### 4. Error Path Coverage
- **Question**: Are error conditions and failure scenarios tested?
- **Check**: For each error path in the Implementation Spec (validation failures, out-of-bounds, invalid state), verify a test exercises it.
- **Flag**: Any error path with no test.

**Example gap:**
- Implementation Spec validates `cursorPosition < numLanes`
- Testing Spec has no test for invalid cursor position
- **Proposal**: Add test "Cursor clamped at max lane boundary"

---

### 5. Integration Point Coverage
- **Question**: Are interactions between components tested?
- **Check**: If the feature touches multiple subsystems (display + haptic + light), verify at least one test exercises the integration.
- **Flag**: Any integration point with zero coverage.

**Example gap:**
- Implementation Spec shows hit triggers display flash, haptic pulse, and LED red
- Testing Spec has separate tests for display, haptic, and LED individually
- Testing Spec has no test verifying all three fire together
- **Proposal**: Add test "Hit triggers all feedback simultaneously"

---

### 6. Success Criteria Coverage
- **Question**: Does every criterion in the issue map to at least one test?
- **Check**: Read the issue's "Success Criteria" or "Acceptance Criteria" section. For each bullet point, find the test that proves it.
- **Flag**: Any criterion with no test.

**Example gap:**
- Issue says "Success: Player sees level progress pips (●●●○○)"
- Testing Spec has test for level clear transition, but no test verifying pips render
- **Proposal**: Add test "Level clear displays progress pips with correct fill state"

---

### 7. Presentation Layer Coverage (Critical Lesson from PR #80)

**Question**: Does the test verify what the user actually sees/experiences, or just the underlying data?

**Background**: PR #80 added the `role all` command. All unit tests passed — the command set player roles correctly. But the CLI display was broken: the command used `\n` in its output message, and the renderer (`CommandResult.message` → `bufferLine()`) is single-line cursor-positioned — embedded newlines render as invisible characters. The second line never appeared on screen.

**Root cause**: The test verified `result.success == true` and checked that the roles were set, but didn't verify the *rendered display output* — what the user actually sees.

**Check**: For every feature with a visual/output component, verify at least one test exercises the *presentation path*, not just the data model:
- **CLI commands**: Test verifies `CommandResult.message` format matches renderer constraints (single line, no `\n`)
- **OLED display**: Test checks `drawStr()` / `drawBox()` calls, not just session state
- **Serial protocol**: Test verifies framed message format, not just message struct contents
- **LED output**: Test checks `setColor()` call, not just internal color state

**Flag**: Any user-facing feature where tests only check data model and skip presentation layer.

**Example gap (from PR #80):**
- Implementation Spec shows `role all` command sets player roles
- Testing Spec has test "Command sets all roles correctly" that checks player objects
- Testing Spec has NO test verifying `CommandResult.message` format
- **Proposal**: Add test "Command result message is single-line compatible with renderer" that asserts `result.message.find('\n') == std::string::npos`

**General rule**: If the user can't see it, the feature isn't done. If the test doesn't verify what the user sees, the test isn't complete.

---

## Test Planner Output Format

The Test Planner adds a `## Test Planner Review` section to the plan document:

```markdown
## Test Planner Review

**Reviewed by**: Test Planner Agent (Phase 2b)
**Date**: 2026-02-16
**Status**: 3 gaps found, 3 proposals added

### Coverage Summary

| Category | Methods | State Transitions | Edge Cases | Error Paths | Integration | Success Criteria | Presentation |
|----------|---------|-------------------|------------|-------------|-------------|------------------|--------------|
| **Total** | 8 | 4 | 6 | 2 | 3 | 5 | 2 |
| **Tested** | 6 | 3 | 4 | 1 | 2 | 5 | 1 |
| **Gaps** | 2 | 1 | 2 | 1 | 1 | 0 | 1 |

### Gaps Identified

#### Gap 1: Method — `generateGapPositions()` not tested
**Location**: Implementation Spec, line 45
**Risk**: Medium — core gameplay logic, incorrect gap generation breaks game
**Priority**: Must-have

**Proposed Test:**
```cpp
void testGapPositionsBoundedByMaxDistance(SpikeVectorTestSuite* suite) {
    // Arrange: configure level with maxGapDistance = 3, numLanes = 5
    suite->setupGame(false);
    auto config = suite->getGame()->getConfigForLevel(2, false);
    ASSERT_EQ(config.maxGapDistance, 2);

    // Act: generate gap positions for 6 walls
    std::vector<int> gaps = suite->getGame()->generateGapPositions(6, 5, 2);

    // Assert: every consecutive pair differs by 1-2 lanes
    for (size_t i = 1; i < gaps.size(); i++) {
        int delta = std::abs(gaps[i] - gaps[i-1]);
        ASSERT_GE(delta, 1) << "Consecutive gaps at same position";
        ASSERT_LE(delta, 2) << "Gap distance exceeds max";
    }
}
```

**TEST_F registration**: Add to `spike-vector-reg-tests.cpp`:
```cpp
TEST_F(SpikeVectorTestSuite, GapPositionsBoundedByMaxDistance) {
    testGapPositionsBoundedByMaxDistance(this);
}
```

---

#### Gap 2: Edge Case — Wall collision at cursor+1 not tested
**Location**: Implementation Spec shows `if (wallX <= CURSOR_X)` collision check
**Risk**: Low — collision timing edge case, unlikely to fail but should be verified
**Priority**: Nice-to-have

**Proposed Test:**
```cpp
void testCollisionDetectedWhenWallSlightlyPastCursor(SpikeVectorTestSuite* suite) {
    // Arrange: wall at CURSOR_X - 1 (just past)
    suite->setupGame(false);
    suite->advanceToGameplay();
    auto state = suite->getGameplayState();
    auto session = suite->getSession();

    state->cursorPosition = 1;
    session->gapPositions[0] = 2;  // Miss
    state->formationX = 1;  // CURSOR_X - 1

    // Act: tick once
    suite->tickOnce();

    // Assert: hit registered
    ASSERT_EQ(session->hits, 1);
}
```

---

#### Gap 3: Presentation Layer — Button press display not verified
**Location**: Implementation Spec shows inverted label rendering on button press
**Risk**: Medium — visual feedback is core to redesign, but only internal state tested
**Priority**: Must-have (post-PR #80 lesson)

**Proposed Test:**
```cpp
void testButtonPressInvertsUPLabel(SpikeVectorTestSuite* suite) {
    // Arrange: enter gameplay
    suite->setupGame(false);
    suite->advanceToGameplay();

    // Act: press primary button
    suite->pressPrimary();
    suite->tickOnce();

    // Assert: display received drawBox call for inverted background
    auto& calls = suite->getDisplayCalls();
    bool foundInvertedBox = false;
    for (const auto& call : calls) {
        if (call.type == "drawBox" && call.y == 56 && call.height == 8) {
            foundInvertedBox = true;
            break;
        }
    }
    ASSERT_TRUE(foundInvertedBox) << "No inverted box drawn for button press";
}
```

---

### Recommendations

1. **Incorporate must-have tests into Testing Spec** (Gaps #1, #3) — JuniorDev should implement these alongside the feature
2. **Nice-to-have tests** (Gap #2) — can be deferred to DV Coordinator (Phase 6b) if time-constrained
3. **Update test count estimate** — Testing Spec originally said 10 tests; with additions, expect 13 tests

---

**Gate Status**: ✅ Review complete. Must-have tests added to Testing Spec. Ready for Phase 3 (implementation).
```

---

## Gate Checklist

Before proceeding to Phase 3 (Junior Dev), verify:

- [ ] Plan document has `## Test Planner Review` section
- [ ] Review identified gaps in all 7 categories (methods, state transitions, edge cases, errors, integration, success criteria, presentation)
- [ ] Must-have test proposals are incorporated into the Testing Spec
- [ ] Nice-to-have test proposals are documented (can be deferred to Phase 6b)
- [ ] Test count estimate updated in plan document
- [ ] Issue labeled `coverage-reviewed`

---

## Test Planner Bootstrap Prompt

When launching the Test Planner agent:

```
You are the Test Planner for the PDN project. Your job is to review
Testing Specs for completeness before implementation begins.

Read the plan document at ~/Documents/code/alleycat/plans/pdn/<branch>.md,
focusing on:
- Implementation Spec (what code changes)
- Testing Spec (what tests are planned)
- Issue success criteria (what must be proven)

For each section, check:
1. Method coverage — every public method has a test?
2. State transition coverage — every transition tested?
3. Edge case coverage — boundaries, nulls, max limits?
4. Error path coverage — validation failures, invalid state?
5. Integration coverage — multi-component interactions?
6. Success criteria coverage — every criterion maps to a test?
7. Presentation layer coverage — user-visible output verified, not just data model?

Identify gaps. For each gap, propose a concrete test with:
- Test function name
- Setup/action/assert pseudocode or full source
- Priority: must-have (blocks Phase 3) or nice-to-have (defer to Phase 6b)

Write findings to a ## Test Planner Review section in the plan doc.

Update the Testing Spec with must-have tests. Leave nice-to-have tests in
the review section only.

Read CLAUDE.md for project conventions and test file structure.

Post-PR #80 lesson: ALWAYS check presentation layer. If a feature has
user-visible output (CLI display, serial messages, LED, OLED), ensure
at least one test verifies the rendered/formatted output, not just the
underlying data model.
```

---

## Common Patterns

### Pattern 1: Missing method test
**Symptom**: Implementation Spec adds a new public method, Testing Spec has no test for it.
**Fix**: Propose a test that calls the method with typical inputs and asserts expected output.

### Pattern 2: Transition without trigger test
**Symptom**: Implementation Spec shows `transition("NextState")` under a condition, no test verifies that condition triggers the transition.
**Fix**: Propose a test that sets up the condition and asserts the state machine advanced to `NextState`.

### Pattern 3: Boundary not tested
**Symptom**: Implementation Spec has a loop `for (int i = 0; i < N; i++)`, tests only check middle values.
**Fix**: Propose tests for `i == 0` (first iteration) and `i == N-1` (last iteration).

### Pattern 4: Error path not exercised
**Symptom**: Implementation Spec has `if (invalid) { return error; }`, no test triggers the invalid case.
**Fix**: Propose a test that passes invalid input and asserts the error result.

### Pattern 5: Integration assumed
**Symptom**: Feature touches display + haptic + LED, tests verify each individually, no test checks all three fire together.
**Fix**: Propose an integration test that triggers the event and asserts all three subsystems received calls.

### Pattern 6: Success criterion not mapped
**Symptom**: Issue says "Success: X happens," Testing Spec has no test named "X happens" or equivalent.
**Fix**: Propose a test that directly verifies X happened (state change, display output, transition, etc.).

### Pattern 7: Presentation layer gap (PR #80 lesson)
**Symptom**: Feature has user-facing output (CLI message, display rendering, serial message), tests only check data model (state variables, session fields), no test verifies formatted output.
**Fix**: Propose a test that captures the output string / display calls / serial frame and asserts format correctness.

---

## What Happens After Phase 2b?

Once the Test Planner completes:
1. **Orchestrator** reviews the findings and decides if must-have tests are reasonable or need adjustment
2. **Must-have tests** are added to the Testing Spec (the Test Planner may do this, or the orchestrator)
3. **Issue is labeled** `coverage-reviewed`
4. **Phase 3 (Junior Dev) launches** — implementation begins with a complete Testing Spec
5. **Phase 4 (Testing Agent) uses the updated spec** — no guessing about what to test

---

## Metrics

Track these over time to measure Test Planner effectiveness:

- **Gaps per feature** — how many gaps does the Test Planner find on average?
- **Must-have vs nice-to-have ratio** — are we catching critical gaps early?
- **Bugs found in review (Phase 5) that should have had tests** — indicates Test Planner missed something
- **PRs with disabled tests** — should trend toward zero as Testing Specs improve

---

**End of Phase 2b Gate Documentation**
