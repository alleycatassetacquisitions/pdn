# Grafana Monitoring Test Suites

Test suites for Grafana monitoring exporters that serve Prometheus metrics for the Project Visibility Dashboard.

## Overview

This test suite validates 4 monitoring components:

1. **Fleet Metrics Exporter** (`test_fleet_metrics_exporter.py`) - 34 tests
2. **GitHub Metrics Exporter** (`test_github_metrics_exporter.py`) - 41 tests
3. **Dashboard-Exporter Contracts** (`test_dashboard_contracts.py`) - 20 tests
4. **Fleet State Schema** (`test_fleet_state_schema.py`) - 26 tests

**Total: 121 tests**

## Running Tests

```bash
# Install dependencies
pip3 install -r requirements-test.txt

# Run all monitoring tests
pytest test/test_monitoring/ -v

# Run specific test file
pytest test/test_monitoring/test_fleet_metrics_exporter.py -v

# Run with coverage
pytest test/test_monitoring/ --cov=. --cov-report=html
```

## Test Status

**118 passing tests**, 3 intentionally failing tests that document known bugs.

### Known Bugs (Intentional Failures)

These tests are designed to fail and document schema mismatches:

1. **`test_no_unknown_status_values`** - Demonstrates that invalid status values are not caught by the exporter
2. **`test_agents_key_rejected_or_aliased`** - Documents the `agents` vs `vms` key mismatch between server-manager and exporter
3. **`test_server_manager_state_compatible`** - Shows incompatibility between server-manager format and PDN format

These failing tests serve as regression tests - when the bugs are fixed, these tests will pass.

## Test Structure

### Fleet Metrics Exporter Tests (34 tests)

Tests the exporter that reads `.fleet-state.json` and serves metrics on port 9101:

- State file handling (missing, corrupt, empty, valid)
- Agent status mapping (idle=1, active=2, complete=3)
- Current task assignment parsing
- Task status metrics
- Aggregation metrics (totals, progress percent)
- Task dependencies (blocked_by)
- Prometheus format compliance
- Known bug: exporter reads `vms` key but server-manager writes `agents` key

### GitHub Metrics Exporter Tests (41 tests)

Tests the exporter that fetches issues/PRs via `gh` CLI and serves metrics on port 9102:

- GitHub CLI integration (success, failure, invalid JSON)
- Repository detection (SSH, HTTPS formats)
- Issue metrics (counts, state, age)
- PR metrics (counts, state, changes)
- Edge cases (no issues, no PRs, empty lists)
- Prometheus format compliance
- Known bug: `github_pulls_total` absent when 0 PRs (should emit zeros)

### Dashboard Contract Tests (20 tests)

Tests the contract between Grafana dashboard queries and exporter output:

- Metric name existence in exporters
- Label selector validation
- Legend format label validation
- Value mapping consistency
- Datasource configuration
- Scrape configuration
- Dashboard structure validation
- Known bug: dashboard queries `fleet_agent_status{status=""}` but metric has no `status` label

### Fleet State Schema Tests (26 tests)

Tests fleet state schema validation and consistency:

- Required fields validation
- IP address format validation
- Status vocabulary validation
- Cross-file consistency (no duplicate IPs/VM IDs)
- Exporter compatibility
- Known bug: schema divergence between server-manager (`agents`) and PDN (`vms`)

## Mock Implementation

The tests include mock implementations of the exporters to enable TDD (Test-Driven Development). These mocks:

- Implement the same API as the real exporters
- Allow tests to run without actual exporters existing
- Serve as reference implementations when building the real exporters

When the real exporters are implemented, replace the mock imports with actual imports.

## Test Categories

Tests are marked with categories for selective running:

```bash
# Run only fleet exporter tests
pytest test/test_monitoring/ -m fleet

# Run only GitHub exporter tests
pytest test/test_monitoring/ -m github

# Run only contract tests
pytest test/test_monitoring/ -m contract

# Run only schema tests
pytest test/test_monitoring/ -m schema
```

## Related Issues

- #179 - Fleet metrics exporter unit tests
- #180 - GitHub metrics exporter unit tests
- #181 - Dashboard-exporter contract tests
- #182 - Fleet state schema validation tests

## Contributing

When adding new tests:

1. Add test function with descriptive name
2. Update test count in this README
3. Add appropriate pytest marker if needed
4. Ensure mock implementations stay in sync with real exporters
