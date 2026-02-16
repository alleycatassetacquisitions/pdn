# Defect Escape Tracking Process

**Purpose**: Track and reduce the rate of defects that escape to production/main branch despite CI and review processes.

**Owner**: CI Review Bot (Agent 03) + QA roles (Test Planner, Integration Tester, Regression Guard)

**Tracking Frequency**: Per wave (development cycle)

---

## Overview

A **Defect Escape** is a bug that:
1. Makes it past code review and CI checks
2. Gets merged to main
3. Is discovered post-merge (by users, in integration testing, or in production)

Tracking defect escapes helps identify gaps in testing, design review, and merge processes.

---

## Defect Escape Rate (DER) Formula

```
DER = (Defects Escaped to Main) / (Total Defects Found in Wave)
```

**Target**: DER < 10% (fewer than 1 in 10 bugs escape to main)

**Current Baseline** (from issue #280 audit):
- **20 defects escaped** across recent waves
- Analysis shows: 50% of redesigns ship without tests
- 4 QA roles defined, but only 1 (CI Review Bot) executing consistently

---

## Tracking Sheet Template

Create a tracking sheet per wave in `docs/reports/wave-NN-defect-escapes.md`:

```markdown
# Wave NN Defect Escape Report

**Wave Period**: YYYY-MM-DD to YYYY-MM-DD
**Total Defects Found**: ___
**Defects Caught Pre-Merge**: ___
**Defects Escaped to Main**: ___
**Defect Escape Rate**: ___%

---

## Escaped Defects

### Defect 1: [Short Title]

**Issue**: #___
**PR that introduced it**: #___
**Discovered by**: [Agent ID / User / Role]
**Date discovered**: YYYY-MM-DD
**Severity**: [Critical / High / Medium / Low]

**Category**: [Test Gap / Design Gap / Merge Conflict / Environment]

**Root Cause**:
[What went wrong? Why wasn't this caught?]

**Prevention**:
[What process change would have caught this?]

---

### Defect 2: [Short Title]
...

---

## Analysis

**Trends**:
- [e.g., "3/5 escapes were in minigame redesigns without integration tests"]
- [e.g., "2/5 escapes were timing-related bugs not caught by unit tests"]

**Process Improvements**:
- [e.g., "Enforce test coverage check before PR approval"]
- [e.g., "Add timing regression tests to CI"]

**Action Items**:
- [ ] #___ — [Description]
- [ ] #___ — [Description]
```

---

## Defect Categories

### 1. Test Gap

**Definition**: The bug could have been caught by a test, but no test existed.

**Examples**:
- Feature ships without unit tests
- Edge case not covered (timeout, null input, boundary value)
- Integration test missing for multi-component interaction
- Regression test missing for known bug area

**Root Causes**:
- Test Planner not invoked or skipped review
- Developer bypassed testing requirements
- Test coverage not enforced by CI

**Prevention**:
- Enforce Test Planner review before PR approval
- Add quality ratchet: new features require N% test coverage
- Block PRs without tests (CI gate)

---

### 2. Design Gap

**Definition**: The bug is due to flawed architecture, unclear requirements, or incorrect assumptions.

**Examples**:
- Race condition in state machine not considered in design
- Incorrect lifecycle assumption (onStateDismounted not called in edge case)
- Misunderstanding of driver contract (native vs. ESP32 behavior mismatch)
- Breaking change to API not documented

**Root Causes**:
- Design Architect not consulted for complex changes
- No design doc written for non-trivial features
- Assumptions not validated with code owners

**Prevention**:
- Require design doc for features touching >3 files
- Design Architect reviews architecture changes
- Contract tests enforce driver interface parity

---

### 3. Merge Conflict

**Definition**: The bug is introduced during merge/rebase due to conflicting changes.

**Examples**:
- Two PRs modify same function; merge combines them incorrectly
- State machine transition added in one PR conflicts with another
- Test file merge drops test cases from one branch

**Root Causes**:
- No shared file coordination between agents
- Merge conflicts resolved without re-running tests
- CI doesn't catch semantic merge issues (only syntax)

**Prevention**:
- Shared file registry (CLAUDE.md) — agents coordinate before editing
- Re-run full test suite after merge conflict resolution
- Post-merge validation (wave-end integration tests)

---

### 4. Environment

**Definition**: The bug only occurs in specific environments (hardware, OS, network conditions).

**Examples**:
- Works on native CLI but crashes on ESP32 hardware
- Timing bug only visible on slower hardware
- Network latency issue not simulated in tests
- Memory leak only visible after long runtime

**Root Causes**:
- Insufficient testing on target platform (ESP32)
- Native driver doesn't match hardware behavior
- Test mocks skip edge cases (buffer overflow, timeout)

**Prevention**:
- Contract tests enforce native/ESP32 parity
- Require hardware testing for driver changes
- Long-running stability tests (leave device running overnight)

---

## Post-Mortem Template

When a defect escapes, conduct a **5 Whys** post-mortem:

### Defect: [Title]

**What happened?**
[Describe the bug]

**Why did it escape?** (Ask "why" 5 times to find root cause)

1. **Why did the bug occur?**
   - [e.g., "State machine transitioned to wrong state on cable disconnect"]

2. **Why wasn't it caught by tests?**
   - [e.g., "No test covered cable disconnect during this specific state"]

3. **Why was that test missing?**
   - [e.g., "Test Planner review template didn't include cable disconnect checklist"]

4. **Why wasn't the template comprehensive?**
   - [e.g., "Cable disconnect testing not documented as requirement"]

5. **Why wasn't it a requirement?**
   - [e.g., "Design doc didn't call out cable disconnect as critical scenario"]

**Root Cause**: [Final answer from 5 Whys]

**Prevention**: [Process change to prevent recurrence]

---

## Process Integration

### When a defect is discovered post-merge:

1. **Create issue** with label `defect-escape`
2. **Assign severity**: Critical / High / Medium / Low
3. **Categorize**: Test Gap / Design Gap / Merge Conflict / Environment
4. **Conduct post-mortem** using template above
5. **Add to wave tracking sheet** (`docs/reports/wave-NN-defect-escapes.md`)
6. **Create follow-up issues** for prevention measures
7. **Update baselines** if DER exceeds target (10%)

### At end of wave:

1. **Calculate DER** for the wave
2. **Analyze trends** (which categories are most common?)
3. **Propose process improvements** (e.g., add CI gate, expand template)
4. **Update CLAUDE.md** if workflow changes are needed
5. **Present findings** to orchestrator for human review

---

## Success Metrics

### Leading Indicators (Process Health)
- **Test Planner review completion rate**: Target 100%
- **PRs with tests**: Target 100%
- **Pre-commit hook adoption**: Target 100% (no --no-verify bypasses)
- **Contract test pass rate**: Target 100%

### Lagging Indicators (Outcome)
- **Defect Escape Rate**: Target < 10%
- **Post-merge hotfixes**: Target < 2 per wave
- **Critical defects in production**: Target 0

---

## Continuous Improvement

This process is **iterative**. As defects escape, we:
1. Identify the gap (test, design, merge, environment)
2. Create a process change to close the gap
3. Document the change in CLAUDE.md or templates
4. Track whether DER improves in next wave

**Example Evolution**:
- Wave 10: DER = 30% → Add Test Planner review template
- Wave 11: DER = 20% → Add contract tests for native/ESP32 parity
- Wave 12: DER = 10% → Add defect escape tracking process (this doc)
- Wave 13: DER = 5% → Goal achieved, maintain and refine

---

**Document Owner**: Agent 03 (CI Review Bot)
**Last Updated**: 2026-02-16
**Next Review**: After Wave 19 completion
