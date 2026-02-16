# PR Review Checklist

**Purpose**: This checklist ensures every PR meets quality standards before merge. Use this during Phase 5 (SeniorDev review) and Phase 6 (human review).

---

## Pre-Review: PR Metadata

Before diving into code, verify the PR is properly structured:

- [ ] **PR title** follows convention: `type: brief description (#issue)`
  - Examples: `feat: spike vector visual redesign (#223)`, `fix: cable disconnect on mode switch (#207)`
- [ ] **PR body** includes:
  - [ ] Summary of what changed (2-3 sentences minimum)
  - [ ] Key features or changes (bullet points)
  - [ ] Test results (pass/fail counts, not just "all pass")
  - [ ] Related issue reference (`Closes #N` or `Fixes #N`)
- [ ] **PR references the GitHub issue** — click through to verify the issue describes the feature/bug
- [ ] **Branch name** follows convention (if not, note it but don't block):
  - Agent branches: `claudecode/dev/{type}/{issue}/{name}` or `vm-{NN}/{type}/{issue}/{name}`
  - Human branches: `{type}/{name}` or `{name}`
- [ ] **PR is not a draft** (if it's still draft, stop — not ready for review)
- [ ] **Base branch is correct** — PRs to fork target `FinallyEve/pdn:main`, PRs to upstream target `alleycatassetacquisitions/pdn:main`

---

## 1. Implementation Matches Spec

**Goal**: Verify the code changes match what the spec (plan document) said to do.

### Checklist

- [ ] **Read the plan document** (if one exists — check issue body `<details>` block or NFS path)
- [ ] **Every file in the Implementation Spec appears in the PR diff** — no missing files
- [ ] **No unexpected files modified** — if a file changed that wasn't in the spec, ask why
- [ ] **Code snippets in spec match actual code** — spot-check 3-5 snippets from the spec against the diff
- [ ] **Struct/enum definitions match spec** — field names, types, comments as specified
- [ ] **Algorithm logic matches spec** — if the spec described a multi-step algorithm, verify each step is present

### Red Flags

- **Spec said "add field X to struct Y," but the diff doesn't show it** → implementation incomplete
- **Diff modifies a shared file (e.g., `cli-device.hpp`) that wasn't in the spec** → scope creep or missing spec section
- **Code uses a different algorithm than the spec described** → Junior made unauthorized design decision

### What to Do If Mismatch Found

- **Minor deviation** (variable name change, helper function added for clarity) → note it, but allow if it improves clarity
- **Major deviation** (different data structure, different state machine flow) → request changes, ask Junior to align with spec or escalate to Senior Dev for spec update

---

## 2. Testing Spec Tests Are Implemented

**Goal**: Verify every test in the Testing Spec was actually written.

### Checklist

- [ ] **Count tests in the Testing Spec** (list of test names)
- [ ] **Count TEST_F registrations in the PR diff** (should match or exceed spec count)
- [ ] **Spot-check 3-5 tests** — read the test function in the diff, compare against the Testing Spec's setup/action/assert description
- [ ] **Verify test names match spec** — if spec says "testGapPositionsBounded," the PR should have a test with that name (or very similar)
- [ ] **Tests are in correct files** — test functions in `.hpp` header, TEST_F registrations in per-game `.cpp` file

### Red Flags

- **Testing Spec lists 12 tests, PR only adds 8 TEST_F macros** → tests were dropped or deferred
- **Test function exists but is disabled** (`TEST_F` commented out, or uses `DISABLED_` prefix) → test debt
- **Test name in PR doesn't match any test in spec** → Junior added a test not in the spec (may be good, but verify it's relevant)
- **Tests are in the wrong registration file** (e.g., Spike Vector tests added to `ghost-runner-reg-tests.cpp`) → violates file ownership convention

### What to Do If Tests Missing

- **Some tests missing** → request changes, ask Junior to implement the missing tests or explain why they're deferred
- **All tests missing** → block merge, require full test implementation before approval
- **Tests disabled** → block merge, require tests to be fixed and enabled (see Section 4)

---

## 3. Bug Fix PRs Include Regression Tests

**Goal**: Ensure bugs can't return undetected.

### Checklist

- [ ] **Is this a bug fix?** (check PR title/body, or issue type)
- [ ] **PR diff includes new tests** — not just code changes
- [ ] **Test reproduces the bug** — if you revert the code fix, the test should fail
- [ ] **Test name describes the bug** — e.g., `testCableDisconnectDoesNotResetPlayer`, `testInvertedDrawDoesNotHideCaptions`

### Red Flags

- **Bug fix with zero new tests** → no regression prevention, bug can return silently
- **Test added but doesn't fail when fix is reverted** → test doesn't actually exercise the bug condition

### What to Do If Regression Test Missing

- **Request changes** — ask Junior or Testing Agent to add a regression test
- **If bug is trivial** (typo fix, comment correction) → may not need a test, but explain reasoning in PR comment

---

## 4. No Disabled Tests

**Goal**: Prevent test debt from accumulating.

### Checklist

- [ ] **Search PR diff for `DISABLED_`** — grep for disabled test macros
- [ ] **Search for commented-out TEST_F** — manual scan of test registration files
- [ ] **Read PR body** — does it mention "temporarily disabled tests"?

### Red Flags

- **Any disabled tests in the diff** → test debt being merged
- **PR body says "tests temporarily disabled"** → red flag, should be fixed before merge

### What to Do If Disabled Tests Found

- **Block merge** — tests must be fixed and enabled before approval
- **If test is legitimately blocked** (requires a feature not yet implemented) → move test to a follow-up issue, don't merge disabled code
- **If test is flaky** (passes sometimes, fails sometimes) → investigate root cause, fix the flakiness, then enable

### Exception

- **Pre-existing disabled tests** (existed before this PR) → not this PR's responsibility to fix, but file a follow-up issue to re-enable them

---

## 5. Multi-Commit Structure

**Goal**: Verify commits are logically separated for clean history and revertability.

### Checklist

- [ ] **PR has 2+ commits** (unless it's a trivial one-liner)
- [ ] **Commit 1: Struct/config changes** — data structures, state machine wiring, no-op stubs
- [ ] **Commit 2: Implementation** — full logic, rendering, algorithms
- [ ] **Commit 3: Tests** — test functions and registrations
- [ ] **Each commit message** follows convention: `type: brief description (#issue)`
- [ ] **No "WIP" or "fix typo" commits** — history should be clean

### Red Flags

- **Single commit with everything** → hard to review, hard to revert partially
- **Commits out of order** (tests before implementation) → suggests commits were squashed incorrectly
- **Commit messages are vague** ("updates", "changes", "more work") → poor commit hygiene

### What to Do If Structure Is Wrong

- **Ask Junior to rebase and split commits** — provide guidance on logical boundaries
- **If history is already pushed and messy** → may be acceptable if the code itself is correct, but note it for future PRs

### Exception

- **Trivial PRs** (1-5 line fixes, documentation updates) → single commit is fine

---

## 6. PR Body Describes Changes and Rationale

**Goal**: Ensure reviewers understand what changed and why.

### Checklist

- [ ] **Summary section** — 2-3 sentences minimum, not just "implements #N"
- [ ] **Key features or changes** — bullet points listing the main changes
- [ ] **Testing section** — describes what tests were added, pass/fail counts
- [ ] **Rationale or context** — if this is a bug fix, what caused the bug? If this is a feature, why this approach?
- [ ] **Related issues** — links to the GitHub issue with `Closes #N` or `Fixes #N`

### Red Flags

- **PR body is one sentence** ("Implements #N") → provides no context for review
- **No test information** — doesn't say whether tests pass or how many were added
- **No rationale** — doesn't explain why the change was made

### What to Do If PR Body Is Inadequate

- **Request changes** — ask for a more detailed PR body before approval
- **If code is excellent but body is weak** → approve with a note to improve PR descriptions in the future

---

## 7. All Tests Pass

**Goal**: Verify CI is green and local tests succeed.

### Checklist

- [ ] **CI checks are green** — view PR checks with `gh pr checks <N> -R FinallyEve/pdn`
- [ ] **Local test run confirms** — run `pio test -e native_cli_test` locally to verify
- [ ] **Test count matches or exceeds baseline** — if main has 175 tests, this PR should have 175+
- [ ] **No test flakiness** — run tests 2-3 times to check for non-deterministic failures

### Red Flags

- **CI is red** → tests are failing, PR is not ready
- **CI is green but local tests fail** → environment mismatch, investigate
- **Test count decreased** → tests were deleted, investigate why
- **Tests pass once but fail on re-run** → flaky test, must be fixed

### What to Do If Tests Fail

- **Block merge** — tests must pass before approval
- **If failure is unrelated to this PR** (pre-existing flaky test) → may be acceptable, but file a follow-up issue to fix the flaky test

---

## 8. No Scope Creep

**Goal**: Ensure the PR only includes changes relevant to the issue.

### Checklist

- [ ] **Every file change is mentioned in the PR body or spec** — no surprise edits
- [ ] **No refactoring unrelated to the feature** — stick to the scope
- [ ] **No "while I was here" changes** — formatting, typo fixes in unrelated files, etc.

### Red Flags

- **PR adds a new feature not in the issue** → scope creep
- **PR refactors code unrelated to the feature** → mixing concerns
- **PR fixes a bug in a different part of the codebase** → should be a separate PR

### What to Do If Scope Creep Found

- **Request changes** — ask Junior to split unrelated changes into a separate PR
- **If the "extra" change is trivial** (typo fix) → may be acceptable, but note it

---

## 9. Code Style and Conventions

**Goal**: Verify code follows project conventions (see `CLAUDE.md`).

### Checklist

- [ ] **Naming conventions** — `PascalCase` for classes, `camelCase` for methods/variables
- [ ] **Indentation** — 4 spaces, no tabs
- [ ] **Header guards** — `#pragma once` (not `#ifndef`)
- [ ] **Comments** — single-line `//` for brief, multi-line `/* */` for detailed
- [ ] **Initialization lists** — one per line, colon on constructor line
- [ ] **No emojis** (unless explicitly requested by user)

### Red Flags

- **Tabs instead of spaces** → formatting issue
- **`#ifndef` header guards** → should use `#pragma once`
- **Inconsistent brace style** → opening brace should be on same line as function signature

### What to Do If Style Violations Found

- **Minor violations** (inconsistent spacing) → note it, but don't block merge
- **Major violations** (wrong naming convention) → request changes

---

## 10. Security and Safety

**Goal**: Prevent security vulnerabilities and unsafe patterns.

### Checklist

- [ ] **No hardcoded credentials** — WiFi passwords, API keys, tokens
- [ ] **No SQL injection risks** (if applicable)
- [ ] **No command injection risks** (if PR adds shell commands)
- [ ] **No buffer overflows** — check `strncpy`, `snprintf` bounds
- [ ] **No unvalidated user input** — check input validation at system boundaries

### Red Flags

- **Hardcoded WiFi password in code** → security violation (should use `wifi_credentials.ini`)
- **Unvalidated input from serial/HTTP** → potential injection attack
- **Buffer copy without size check** → overflow risk

### What to Do If Security Issue Found

- **Block merge immediately** — security issues are P0
- **Request fix** — explain the vulnerability and the correct approach

---

## 11. Shared File Edits

**Goal**: Ensure shared files are edited carefully and don't break other features.

### Shared Files (see `CLAUDE.md`)

- `include/cli/cli-device.hpp`
- `include/device/device-types.hpp`
- `src/game/quickdraw-states/fdn-detected-state.cpp`
- `src/game/quickdraw.cpp`
- `include/game/color-profiles.hpp`

### Checklist

- [ ] **Does the PR modify any shared files?** — check diff
- [ ] **Was the shared file edit in the Implementation Spec?** — verify it was planned
- [ ] **Are the changes additive?** (new enum value, new case in switch) → safe
- [ ] **Are the changes destructive?** (deleting enum value, changing existing behavior) → risky

### Red Flags

- **Shared file edited without spec approval** → unauthorized change
- **Enum value deleted** → breaks other code that references it
- **Switch case reordered** → may change behavior if order-dependent

### What to Do If Shared File Edit Is Risky

- **Request clarification** — ask Junior why this file was edited
- **If change is safe** (additive) → approve
- **If change is destructive** → escalate to Senior Dev or orchestrator

---

## 12. Documentation and Comments

**Goal**: Ensure complex logic is well-documented.

### Checklist

- [ ] **Complex algorithms have comments** — explain the "why," not just the "what"
- [ ] **Lifecycle methods have comments** — for `onStateMounted()`, `onStateLoop()`, `onStateDismounted()`, explain what happens in each phase
- [ ] **Magic numbers are explained** — e.g., `200` (flash interval in ms) should have a comment
- [ ] **Public APIs have comments** — if a method is called from multiple places, document its contract

### Red Flags

- **Long, complex function with zero comments** → hard to review, hard to maintain
- **Magic number with no explanation** — reviewer doesn't know why `6` was chosen

### What to Do If Documentation Is Lacking

- **Request changes** — ask for comments on complex sections
- **If code is self-explanatory** → may not need comments, use judgment

---

## 13. Revertability

**Goal**: Ensure the PR can be reverted cleanly if something goes wrong.

### Checklist

- [ ] **Commits are logically separated** — can revert Commit 2 (implementation) without breaking Commit 1 (structs)?
- [ ] **No one-way migrations** — data format changes, protocol version bumps
- [ ] **No destructive changes to shared files** — deleting enum values, changing existing behavior

### Red Flags

- **Single commit with everything** → can't partially revert
- **Data format change** — if reverted, existing data becomes invalid
- **Shared enum value changed** — revert breaks other code

### What to Do If PR Is Not Revertable

- **Request commit restructuring** — split into independently revertable pieces
- **If one-way migration is necessary** → ensure it's documented and approved by orchestrator

---

## 14. Integration Impact

**Goal**: Assess whether this PR will conflict with other open PRs.

### Checklist

- [ ] **List all open PRs** — `gh pr list -R FinallyEve/pdn`
- [ ] **Check for shared file edits** — do any other PRs modify the same files?
- [ ] **Check for state machine changes** — does this PR add a new state that might conflict with other features?

### Red Flags

- **Two PRs editing the same file** → merge conflict likely
- **Two PRs adding enum values to the same enum** → merge conflict possible

### What to Do If Conflict Likely

- **Consult Integration Engineer** — determine merge order
- **Rebase one PR onto the other** (if they're sequential)
- **Merge higher-priority PR first** — lower-priority PR rebases afterward

---

## Final Checklist Before Approval

- [ ] **All sections above are checked** — no skipped items
- [ ] **No red flags unresolved** — all issues addressed or explicitly accepted
- [ ] **CI is green** — tests pass
- [ ] **PR body is clear** — reviewer understands what changed and why
- [ ] **Implementation matches spec** — no unauthorized design decisions
- [ ] **Tests are complete** — all Testing Spec tests implemented
- [ ] **No disabled tests** — no test debt being merged
- [ ] **Commit structure is clean** — logically separated, revertable
- [ ] **No security issues** — no hardcoded credentials, no injection risks

---

## Approval Actions

Once all checks pass:

### For SeniorDev (Phase 5 Review)

```bash
gh pr ready <N> -R FinallyEve/pdn
gh pr edit <N> --add-assignee FinallyEve -R FinallyEve/pdn
gh issue edit <N> --add-label review-requested -R FinallyEve/pdn
```

**Do NOT use `--add-reviewer`** (author can't be reviewer on their own PR).

### For Human (Phase 6 Review)

```bash
gh pr review <N> -R FinallyEve/pdn --approve --body "Approved. <brief comment>"
gh pr merge <N> -R FinallyEve/pdn --squash  # or --merge, depending on preference
```

---

## Common Review Patterns

### Pattern 1: "Looks good but tests are disabled"
**Response**: Request changes — "Tests must be enabled before merge. If a test is blocked, move it to a follow-up issue instead of disabling it."

### Pattern 2: "Code works but doesn't match spec"
**Response**: Request changes — "Implementation deviates from spec. Either update the code to match the spec, or escalate to Senior Dev to revise the spec."

### Pattern 3: "Great code, terrible PR body"
**Response**: Approve with comment — "Approved, but please improve PR descriptions in the future. Include summary, test results, and rationale."

### Pattern 4: "Feature works but scope crept"
**Response**: Request changes — "This PR includes changes unrelated to the issue. Please split unrelated changes into a separate PR."

### Pattern 5: "Tests pass but one is flaky"
**Response**: Request changes — "Test X fails intermittently. Investigate and fix the non-determinism before merge."

---

## Metrics to Track

- **PRs approved on first review** vs **PRs requiring changes** — measures spec quality
- **Disabled tests merged** (should be zero) — measures test debt accumulation
- **Bugs found in review that should have had tests** — measures Testing Spec completeness
- **Scope creep PRs** (multiple unrelated changes) — measures Junior adherence to spec

---

**End of PR Review Checklist**

---

## Usage Notes

- **Use this checklist for every PR** — don't skip items
- **If a section doesn't apply** (e.g., "bug fix regression test" for a feature PR), mark it N/A
- **Copy this checklist into PR review comments** if you need to explain requested changes
- **Update this checklist** as new patterns emerge (e.g., post-PR #80, we added presentation layer checks)

---

**Version**: 1.0 (2026-02-16)
**Last Updated**: Issue #277 spec quality audit
