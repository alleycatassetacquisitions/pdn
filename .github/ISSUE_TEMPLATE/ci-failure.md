---
name: CI Failure Report
about: Report a CI failure on main or a PR
labels: ci-failure
---

## Severity
<!-- P0: main broken / P1: single PR / P2: CI infra / P3: warning -->

## Failing Tests
<!-- List exact test names or "compile error in <file>" -->

## Error Output
<!-- Paste the relevant compiler error or test failure output (truncated) -->

## Root Cause
<!-- Which PR(s) or merge(s) caused this? What changed? -->

## Affected Files
<!-- List files that need to be modified to fix this -->

## Reproduction
<!-- Exact command: e.g., pio test -e native_cli_test -->

## Blast Radius
<!-- How many agents/PRs are blocked? Is main broken? -->
