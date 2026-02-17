# CI Failure Severity Classification Guide

**Version**: 1.0
**Date**: 2026-02-17
**Status**: Active
**Related**: [CI Failure Response Audit (#322)](https://github.com/FinallyEve/pdn/issues/322)

## Overview

This guide defines a four-level severity classification system (P0-P3) for CI failures in the PDN project. Each severity level has defined SLAs, ownership rules, and response procedures. This system replaces the previous ad-hoc approach where all CI failures were treated identically.

**Purpose**: Enable rapid triage, prevent pipeline downtime, and establish clear ownership for CI failures.

## Severity Definitions

### P0: Main Branch Broken
**Label**: `P0-main-broken`
**Color**: Red (`#B60205`)

**Definition**: The `main` branch fails to compile or fails core test suites (`native` or `native_cli_test`). All agents are blocked from branching.

**Examples from Waves 17-20**:
- Wave 17 (#271, #272, #273): `MockStateMachine::loopCount` removed → compile error, `dynamic_cast` with `-fno-rtti` → ESP32 build failure, cppcheck false positives → static analysis failure
- Wave 18 (#284): 61 CLI tests failed to compile after Ghost Runner (#264) and Cipher Path (#268) API redesigns
- Wave 19 (#297): Compile errors in 3 test registration files (references to deleted functions) + 3 runtime test failures from cable disconnect tests

**Impact**: Pipeline fully blocked. Estimated 8+ hours of downtime and ~80 wasted agent-hours across Waves 17-19.

**SLA**:
- **Detection**: Automated (post-merge smoke test) or manual within 30 minutes of merge
- **Issue filed**: Within 30 minutes of detection
- **Fix merged**: Within 2 hours of issue filed

**Who triages**: Orchestrator (or automated detection system)

**Who fixes**: Original PR author(s) whose merge caused the failure. If author is unavailable (session ended), assign Agent 01 (devbox) as permanent fallback.

---

### P1: Single PR CI Failure
**Label**: `P1-pr-blocked`
**Color**: Orange (`#D93F0B`)

**Definition**: A single PR fails CI checks. One agent is blocked, but `main` is healthy.

**Examples**:
- PR fails `native` test suite due to new code breaking existing tests
- PR fails ESP32 build due to missing include or syntax error
- PR fails static analysis with new cppcheck warnings

**Impact**: One agent blocked from merging. Other agents unaffected.

**SLA**:
- **Detection**: Immediate (PR CI status)
- **Issue filed**: Within 1 hour (or self-triaged by PR author)
- **Fix merged**: By next wave (or PR author re-pushes fix immediately)

**Who triages**: PR author self-triages

**Who fixes**: PR author fixes their own PR. No orchestrator involvement needed.

---

### P2: CI Infrastructure Issue
**Label**: `P2-ci-infra`
**Color**: Yellow (`#FBCA04`)

**Definition**: CI infrastructure failure that does not block development but degrades CI reliability. Tests can still run locally.

**Examples from Waves 17-20**:
- Issue #299: `static-analysis` job red for "multiple waves" (cppcheck install fails on self-hosted runners)
- Issue #279 (partial): CI runner missing `unzip` dependency
- Flaky tests that intermittently fail

**Impact**: CI checks unreliable. May cause false positives or masking of real failures. Non-blocking for development.

**SLA**:
- **Detection**: Within 24 hours (CI health report or agent observation)
- **Issue filed**: Within 24 hours of detection
- **Fix merged**: Within 1 week (or next wave if low priority)

**Who triages**: CI Engineer (when activated) or Agent 01 (devbox)

**Who fixes**: CI Engineer (when activated) or Agent 01 (devbox)

**Note**: The CI Engineer role is defined in the workflow spec ("diagnose CI failures, produce health reports, maintain GitHub Actions workflows") but has never been activated in Waves 17-20. Until activated, Agent 01 owns all P2 issues.

---

### P3: Warning or Style Violation
**Label**: `P3-warning`
**Color**: Green (`#0E8A16`)

**Definition**: Non-functional issue such as increased compiler warnings, style violations, or non-critical static analysis warnings.

**Examples**:
- New compiler warning about unused variable
- clang-format style drift
- cppcheck warning that does not indicate a bug
- Documentation linter warnings

**Impact**: No functional impact. Code works correctly.

**SLA**:
- **Detection**: Opportunistic (during PR review or wave planning)
- **Issue filed**: Next wave planning
- **Fix merged**: Next wave or whenever convenient

**Who triages**: Any agent or orchestrator during planning

**Who fixes**: Any agent (typically batched with related work)

---

## Severity Classification Examples

### Historical Failures Classified

| Wave | Issue | Original Severity | Should Be | Rationale |
|------|-------|------------------|-----------|-----------|
| 17 | #271 | (none) | **P0** | Compile error in `MockStateMachine` → main broken |
| 17 | #272 | (none) | **P0** | ESP32 build failure (`dynamic_cast`/`-fno-rtti`) → main broken |
| 17 | #273 | (none) | **P3** | cppcheck false positives → static analysis only, not functional |
| 17 | #279 | (none) | **Mixed** | Bundled P0 (ESP32 build) + P2 (missing `unzip`) + P1 (`managedMode`) — should have been 3 separate issues |
| 18 | #284 | (none) | **P0** | 61 CLI tests failed to compile → main broken |
| 19 | (no issue) | (none) | **P0** | Compile errors + 3 test failures → main broken (no issue filed!) |
| 18-20 | #299 | (none) | **P2** | cppcheck install failure on CI runner → infrastructure issue |

**Key lesson from #279**: Do NOT bundle unrelated failures into a single issue. Three independent failures require three separate issues, even if assigned to the same agent. Bundling caused the fix PR to fail 7/9 CI checks and require a second fix PR (#281).

---

## Ownership Model (Hybrid)

| Severity | Who Detects | Who Triages | Who Fixes | Assignment Method |
|----------|------------|-------------|-----------|-------------------|
| **P0** | Automated smoke test or orchestrator | Orchestrator | Original PR author(s) OR Agent 01 (fallback) | Orchestrator assigns within 30 min |
| **P1** | PR CI status | PR author | PR author | Self-assignment (no orchestrator) |
| **P2** | CI Engineer or agents | CI Engineer OR Agent 01 | CI Engineer OR Agent 01 | Orchestrator assigns to role owner |
| **P3** | Any agent or orchestrator | Any agent | Any agent | Batched into next wave |

### P0 Assignment Protocol

1. **Identify the breaking PR(s)**: Use git bisect or merge timestamps to find which PR(s) caused the failure
2. **Assign original author(s)**: If the PR was merged by Agent 07, assign Agent 07 to fix it
3. **Fallback to Agent 01**: If the original author's session has ended or agent is unreachable, assign Agent 01 (devbox) as permanent fallback
4. **Single fix per issue**: If multiple PRs contributed to the breakage, file separate issues for each PR's contribution

### P1 Self-Fix Protocol

1. PR author receives CI failure notification from GitHub
2. PR author investigates locally: `git checkout <branch> && pio test -e native`
3. PR author fixes the issue and re-pushes
4. No issue needs to be filed unless the failure reveals a design problem requiring orchestrator input

---

## Issue Filing Requirements

All P0, P1, and P2 issues MUST use the CI Failure Report template (`.github/ISSUE_TEMPLATE/ci-failure.md`) with these required fields:

### Required Fields

| Field | Purpose | Example |
|-------|---------|---------|
| **Severity** | P0/P1/P2/P3 — sets urgency and SLA | `P0` |
| **Failing Tests** | Exact test names or "compile error in <file>" | `test_ghost_runner_routing`, `test_cipher_path_encryption` (61 tests listed) |
| **Error Output** | Raw compiler error or test failure output (snippet) | `error: 'loopCount' is not a member of 'MockStateMachine'` |
| **Root Cause** | Which PR(s) or merge(s) caused this? What changed? | "PR #264 replaced Ghost Runner API — 61 tests reference old API" |
| **Affected Files** | List files that need to be modified to fix this | `test/test_cli/ghost-runner-tests.hpp`, `test/test_cli/cipher-path-tests.hpp` (10 files listed) |
| **Reproduction** | Exact command to reproduce | `pio test -e native_cli_test` |
| **Blast Radius** | How many agents/PRs are blocked? Is main broken? | "Main broken — all 12 agents blocked from branching" |
| **Merge That Caused It** | Link to PR(s) or merge commit | "Wave 18 batch merge (#258-#276), specifically PRs #264 and #268" |

### Issue Quality Scorecard (from Audit #322)

| Issue | Wave | Severity | Failing Tests | Error Output | Root Cause | Files | Repro | Merge Link | Blast Radius | Score |
|-------|------|----------|---------------|--------------|------------|-------|-------|------------|-------------|-------|
| #271 | W17 | ❌ | ⚠️ | ✓ | ✓ | ⚠️ | ❌ | ❌ | ❌ | 5/10 |
| #272 | W17 | ❌ | ❌ | ✓ | ✓ | ✓ | ❌ | ❌ | ❌ | 5/10 |
| #273 | W17 | ❌ | ❌ | ✓ | ⚠️ | ❌ | ❌ | ❌ | ❌ | 3/10 |
| #279 | W17-18 | ❌ | ⚠️ | ❌ | ⚠️ | ❌ | ❌ | ⚠️ | ❌ | 3/10 |
| **#284** | W18 | ❌ | **✓** | **✓** | **✓** | **✓** | **✓** | **✓** | **✓** | **9/10** ⭐ |
| (none) | W19 | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **0/10** |

**Gold standard**: Issue #284 is the best example. It includes:
- All 61 failing test names
- Root cause table mapping PRs to broken tests
- 10 affected files with test counts
- Exact reproduction command
- Clear blast radius ("61 tests across 10 files")
- Links to breaking PRs (#264, #268)

**Anti-pattern**: Wave 19 had NO issue filed — just a direct fix PR (#297). This makes the failure harder to track and prevents post-incident review.

---

## Response Procedures

### P0 Response (Main Broken)

**1. Detection** (automated or manual within 30 min):
```bash
# Post-merge smoke test (run after EVERY merge to main)
git checkout main && git pull origin main
pio test -e native && pio test -e native_cli_test && pio run -e esp32-s3_release
```

**2. If smoke test fails**:
- Immediately update fleet state: `main_status: "broken"`
- Block further dispatches until fixed
- File P0 issue using CI Failure Report template within 30 min
- Add labels: `P0-main-broken`, `ci-failure`
- Assign original PR author(s) OR Agent 01 (fallback)

**3. Fixer workflow**:
```bash
# Reproduce locally
git checkout main && git pull origin main
pio test -e native_cli_test  # (or whatever command is failing)

# Identify root cause
git log --oneline -20  # Find recent merges
git show <commit>  # Review breaking changes

# Fix approach A: Forward fix
# - Modify affected files
# - Run tests: pio test -e native && pio test -e native_cli_test
# - Commit: git commit -m "fix: <description> (#issue)"
# - Push: git push origin fix/<branch>
# - Create PR: gh pr create --title "fix: <description> (#issue)" --base main

# Fix approach B: Rollback (if forward fix is complex)
# - Revert to last known good commit
# - git revert --no-commit HEAD~N..HEAD
# - git commit -m "revert: rollback to <commit> (main broken by Wave N merge)"
```

**4. Verification after fix merged**:
```bash
git checkout main && git pull origin main
pio test -e native && pio test -e native_cli_test && pio run -e esp32-s3_release
# Update fleet state: main_status: "healthy"
```

**SLA tracking**:
- Detection → issue filed: **< 30 min**
- Issue filed → fix merged: **< 2 hours**
- Total downtime target: **< 2.5 hours**

**Historical performance** (Waves 17-19):
- Wave 17: 1h 56m total (within SLA)
- Wave 18: 1h 35m total (within SLA)
- Wave 19: 5h 56m total (**SLA miss** — 5-hour detection gap)

---

### P1 Response (Single PR Failure)

**1. Detection**: Immediate (GitHub PR CI status shows red X)

**2. PR author self-triage**:
```bash
# Reproduce locally
git checkout <branch>
pio test -e native  # or whatever CI job failed
```

**3. If failure is in PR author's changes**:
- Fix and re-push (no issue needed)
- Re-run CI to verify

**4. If failure is in base branch (main)**:
- Check if main is broken: `git checkout main && pio test -e native`
- If main is broken, escalate to P0 (file issue, assign orchestrator)
- If main is healthy, rebase onto latest main and re-test

**SLA**: Fix within 1 hour or by next wave (whichever is sooner)

---

### P2 Response (CI Infrastructure)

**1. Detection**: CI health report, agent observation, or standing failures (e.g., #299)

**2. Triage by CI Engineer or Agent 01**:
- Determine if issue blocks any PR merges
- Determine if issue causes false positives/negatives
- Classify urgency (24-hour SLA vs. next-wave)

**3. Investigation**:
```bash
# For runner dependency issues
gh api repos/FinallyEve/pdn/actions/runs --jq '.workflow_runs[0].logs_url'
# Download logs, check for missing packages

# For flaky tests
# Run test suite 10 times, check for intermittent failures
for i in {1..10}; do pio test -e native_cli_test || echo "FAIL $i"; done
```

**4. Fix and verify**:
- For runner dependencies: update CI workflow YAML or runner setup
- For flaky tests: quarantine the test (mark as `@Ignore` or `DISABLED_`) and file issue for root cause fix
- Verify fix: trigger CI run manually or wait for next PR

**SLA**: Fix within 24 hours (urgent) or next wave (low priority)

---

### P3 Response (Warning/Style)

**1. Detection**: Opportunistic (during PR review, wave planning, or CI Engineer health report)

**2. Batching**: Group similar P3 issues into a single wave task (e.g., "Fix all unused variable warnings")

**3. Fix**: Any agent can claim as a low-priority task

**SLA**: Next wave or whenever convenient

---

## Prevention Gates

### Gate 1: Post-Merge Smoke Test (Mandatory)

**Run after EVERY merge to main**:
```bash
git checkout main && git pull origin main
pio test -e native && pio test -e native_cli_test && pio run -e esp32-s3_release
```

**If any command fails**:
1. Halt merge queue (do not merge next PR)
2. File P0 issue immediately
3. Update fleet state: `main_status: "broken"`
4. Assign original PR author or Agent 01

**Implementation options**:
- **Manual** (orchestrator runs after each merge): 0 infrastructure, requires discipline
- **Automated** (GitHub Actions workflow on push to main): requires CI setup, fully reliable

**Expected time**: ~5 minutes per merge (test suite runtime)

**Value**: Prevents batch merge anti-pattern that caused Waves 17, 18, and 19 breakage

---

### Gate 2: Sequential Merge Protocol (Recommended)

Do NOT batch-merge multiple PRs in rapid succession. Merge one PR at a time with Gate 1 between each.

**Wave 17-19 anti-pattern**:
- Wave 17: 12 PRs merged in 71 seconds → 4+ failures
- Wave 18: 16 PRs merged in 84 seconds → 61 tests broken
- Wave 19: 12 PRs merged in ~7 minutes → compile errors + 3 test failures

**Correct pattern for N PRs**:
```
FOR each PR in merge queue:
  1. Merge one PR
  2. Run Gate 1 (smoke test)
  3. IF Gate 1 fails:
       - Revert the merge OR leave unreverted and halt queue
       - File P0 issue for original PR author
       - STOP merging
  4. IF Gate 1 passes:
       - Merge next PR
  5. REPEAT
```

**Expected time for 10-PR wave**: 10 PRs × ~5 min = ~50 minutes (vs. 6+ hours debugging batch merge failures)

---

### Gate 3: Base Branch Freshness Check (Agent Responsibility)

**Before pushing a PR**, agent should verify base is fresh:
```bash
git fetch origin main
git merge-base --is-ancestor origin/main HEAD
# If false: git rebase origin/main && pio test -e native && pio test -e native_cli_test
```

**Purpose**: Prevents agent from pushing PR based on stale main (e.g., Wave 20 agents branched from pre-Wave-19-fix main)

---

### Gate 4: Fleet State Block (Orchestrator Responsibility)

**Before dispatching a new wave**, orchestrator must:
```bash
# Check fleet state
cat /mnt/scratch/fleet/fleet-state.json | jq '.main_ci_status'

# If status is "broken":
# - DO NOT dispatch new wave
# - Fix main first (assign P0 to available agent)
# - Dispatch wave only after main is healthy
```

**Wave 20 violation**: Dispatched while Wave 19 breakage was undetected (5-hour gap). 10 agents branched from broken main.

---

## Label Usage Guide (for Orchestrators)

### When to Apply Labels

| Label | When to Apply | Who Applies |
|-------|--------------|-------------|
| `P0-main-broken` | Main fails to compile or core tests fail | Orchestrator (or automated) |
| `P1-pr-blocked` | Single PR fails CI, agent requests help | PR author or orchestrator |
| `P2-ci-infra` | CI runner issue, flaky test, standing CI failure | CI Engineer or Agent 01 |
| `P3-warning` | Compiler warning, style issue, non-functional | Any agent or orchestrator |
| `ci-failure` | Any CI failure report (in addition to severity label) | Issue creator |
| `ready-to-merge` | PR is reviewed, approved, and ready for merge queue | Orchestrator after human review |
| `do-not-merge` | PR has blocking issue or is work-in-progress | PR author or orchestrator |

### Label Lifecycle

```
[Issue filed] → add severity label (P0/P1/P2/P3) + `ci-failure`
             ↓
[Fix PR created] → link to issue ("Fixes #N")
             ↓
[Fix PR reviewed] → add `ready-to-merge` (or `do-not-merge` if issues found)
             ↓
[Fix PR merged] → issue auto-closes (via "Fixes #N")
```

### Batch Operations

```bash
# Find all P0 issues
gh issue list --repo FinallyEve/pdn --label P0-main-broken --state open

# Find all unresolved CI failures
gh issue list --repo FinallyEve/pdn --label ci-failure --state open

# Find all PRs ready to merge
gh pr list --repo FinallyEve/pdn --label ready-to-merge --state open
```

---

## Escalation Path

### When to Escalate

| From | To | When |
|------|----|----- |
| P1 → P0 | PR failure → Main broken | PR author discovers main is broken (not their PR) |
| P2 → P0 | CI infra → Main broken | CI infrastructure failure starts blocking all merges |
| P3 → P2 | Warning → Infra | Warning count grows to point where it masks real issues |
| Any → Human | Agent → @FinallyEve | SLA miss, agent stuck, or repeated failures |

### Escalation to Human

**When to escalate to @FinallyEve**:
- P0 SLA miss (> 2.5 hours total downtime)
- Agent assigned to fix cannot reproduce the failure
- Same failure recurs multiple waves (systemic issue)
- Fix PR fails CI due to unrelated issues (cascade failure)
- Rollback is required and agent is uncertain of blast radius

**How to escalate**:
1. Comment on the P0 issue: `@FinallyEve SLA miss — X hours downtime, need guidance`
2. Update fleet state: `escalation: "human-review-required"`
3. Continue with other tasks (do not block on escalation)

---

## Role Definitions

### CI Engineer (Dormant Role — Needs Activation)

**Defined in workflow spec**: "Diagnose CI failures, produce health reports, maintain GitHub Actions workflows, quarantine flaky tests."

**Expected artifacts**:
- Per-wave CI health report (pass/fail counts, new failures, regressions)
- Flaky test quarantine list (with repro steps)
- Runner dependency manifest (what packages are installed on CI runners)

**Status in Waves 17-20**: Role never activated. Zero health reports produced. Issue #299 (standing cppcheck failure) unowned.

**Recommendation**: Activate in Wave 21 or assign Agent 01 as permanent CI Engineer.

---

### Rollback Engineer (Dormant Role — Needs Activation)

**Defined in workflow spec**: "Per-commit revert guide, blast radius mapping, canary signals."

**Expected artifacts**:
- Rollback plan with per-commit revert commands
- Blast radius assessment (which PRs/features depend on each commit)
- Canary test list (critical tests to run after rollback)

**Status in Waves 17-20**: Role never activated. Zero rollback plans produced. All fixes are forward-only.

**When rollback is preferred over forward fix**:
- Forward fix is complex and will take > 2 hours
- Breakage affects multiple subsystems (e.g., 61 tests in Wave 18)
- Root cause is unclear and investigation is required

**Rollback procedure** (manual, no tooling exists):
```bash
# Find last known good commit
git log --oneline main | head -20

# Revert to last known good (if last N commits are bad)
git revert --no-commit HEAD~N..HEAD
git commit -m "revert: rollback to <commit> (main broken by Wave N merge)"

# Verify
pio test -e native && pio test -e native_cli_test

# Push
git push origin main

# Update fleet state
main_status: "healthy"
last_green_commit: "<commit>"
```

**Recommendation**: Document rollback procedure in CLAUDE.md and train agents on when to use it.

---

## Post-Incident Review (Required for P0)

After every P0 fix is merged, the orchestrator MUST append to the wave manifest:

```json
{
  "incident": {
    "severity": "P0",
    "detected_at": "2026-02-16T10:52:25Z",
    "issue_filed_at": "2026-02-16T11:52:53Z",
    "fix_merged_at": "2026-02-16T12:47:42Z",
    "detection_latency_min": 60,
    "fix_latency_min": 55,
    "total_downtime_min": 116,
    "root_cause": "Batch merge without integration testing — 12 PRs merged in 71s",
    "breaking_prs": ["#251", "#249"],
    "tests_disabled": 7,
    "prevention": "Sequential merge protocol with post-merge smoke test",
    "lessons_learned": "Gate 1 (smoke test) would have caught this in 5 min instead of 60 min"
  }
}
```

**Purpose**: Creates the learning loop that currently does not exist. Enables trend analysis (detection latency, fix latency, recurrence).

**Post-incident review checklist**:
- [ ] Root cause identified and documented
- [ ] Prevention measure added (Gate 1/2/3/4)
- [ ] SLA met or escalation occurred
- [ ] Fix verified (main is green)
- [ ] Tests disabled (if any) tracked for re-enabling
- [ ] Incident added to wave manifest
- [ ] Related issues cross-referenced

---

## SLA Tracking Dashboard (Future)

**Proposed metrics** (to be tracked manually or automated):

| Metric | Target | Wave 17 | Wave 18 | Wave 19 | Wave 20 |
|--------|--------|---------|---------|---------|---------|
| Detection latency (P0) | < 30 min | 60 min | 60 min | **5 hours** | TBD |
| Issue filing latency (P0) | < 30 min | ~0 min | ~0 min | ~37 min | TBD |
| Fix latency (P0) | < 2 hours | 55 min | 34 min | 50 min | TBD |
| Total downtime (P0) | < 2.5 hours | 1h 56m | 1h 35m | **5h 56m** | TBD |
| Tests disabled per wave | < 5 | 7 | 61 | 3 | TBD |
| Batch merges per wave | 0 (sequential) | **12 in 71s** | **16 in 84s** | **12 in 7m** | TBD |
| P0 recurrences | 0 | — | 1 (repeat) | 2 (repeat) | TBD |

**Red flags**:
- Detection latency increasing (60 min → 5 hours in Wave 19)
- Same root cause repeating (batch merge anti-pattern in Waves 17, 18, 19)
- Tests disabled accumulating (7 + 61 + 3 = 71 tests disabled, unclear re-enable plan)

---

## Related Documents

- [CI Failure Response Audit (#322)](https://github.com/FinallyEve/pdn/issues/322) — Full analysis of Waves 17-20 failures
- [Orchestrator Process Audit (#282)](https://github.com/FinallyEve/pdn/issues/282) — Identified batch merge anti-pattern
- [Testing Agent Pipeline Audit (#280)](https://github.com/FinallyEve/pdn/issues/280) — CI Engineer role definition
- [Workflow Spec](~/Documents/code/alleycat/plans/pdn/workflow.md) — Full agent pipeline phases
- [CLAUDE.md](CLAUDE.md) — Git workflow and commit conventions

---

## Changelog

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-02-17 | Initial version based on Audit #322. Added P0-P3 severity system, ownership model, response procedures, prevention gates, label guide. |

---

*This guide is a living document. Update it when SLAs change, roles are activated, or new failure modes are discovered.*
