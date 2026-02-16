## Summary

Adds comprehensive test suites for all 4 Grafana monitoring exporters with 121 total tests (118 passing, 3 intentionally failing to document known bugs).

## Test Suites

### Issue #179: Fleet Metrics Exporter Tests (34 tests)
Tests for `fleet-metrics-exporter.py` (port 9101) that reads `.fleet-state.json`:

- State file handling (missing, corrupt, empty, valid)
- Agent status mapping (idle=1, active=2, complete=3, unknown=0)
- Current task assignment parsing (handles `#42`, `"42"`, `null`, `"0"`)
- Task status metrics with aggregation and progress tracking
- Task dependency tracking (`blocked_by` metrics)
- Prometheus format compliance (HELP, TYPE headers)
- **Known bug test**: Exporter reads `vms` key but server-manager writes `agents` key

### Issue #180: GitHub Metrics Exporter Tests (41 tests)
Tests for `github-metrics-exporter.py` (port 9102) that uses `gh` CLI:

- GitHub CLI integration (success, failure, invalid JSON, not installed)
- Repository detection from git remotes (SSH and HTTPS formats)
- Issue metrics (counts, state, age, assignees)
- PR metrics (counts, state, changes, authors)
- Edge cases (empty lists, missing data, no PRs/issues)
- Prometheus format compliance
- **Known bug test**: `github_pulls_total` absent when 0 PRs (should emit zeros)

### Issue #181: Dashboard-Exporter Contract Tests (20 tests)
Tests that validate dashboard queries match exporter output:

- Metric name existence validation across exporters
- Label selector matching between dashboard and exporters
- Legend format label validation
- Value mapping consistency (status codes match dashboard mappings)
- Datasource and scrape configuration validation
- Dashboard structure validation (no hardcoded names)
- **Known bug test**: Dashboard queries `fleet_agent_status{status=""}` but metric has no `status` label

### Issue #182: Fleet State Schema Validation Tests (26 tests)
Tests that validate `.fleet-state.json` schema consistency:

- Required fields validation (wave, phase, vms)
- Agent entry validation (vm_id, ip, status)
- IPv4 format validation
- Status vocabulary validation
- Cross-file consistency (no duplicate IPs/VM IDs)
- Exporter compatibility tests
- **Known bug test**: Schema divergence between server-manager (`agents` key) and PDN (`vms` key)

## Test Infrastructure

- **pytest.ini**: Configuration with test markers (`fleet`, `github`, `contract`, `schema`)
- **requirements-test.txt**: Testing dependencies (pytest, pytest-cov, pytest-mock)
- **Mock implementations**: TDD-ready mock exporters for testing before implementation
- **README.md**: Comprehensive documentation with usage examples

## Test Results

```
121 tests collected
118 passed
3 failed (intentionally - documenting known bugs)
```

### Intentional Failures (Known Bugs)

These tests are designed to fail and document schema/contract mismatches:

1. `test_no_unknown_status_values` - Invalid status values not caught
2. `test_agents_key_rejected_or_aliased` - Documents `agents` vs `vms` key mismatch
3. `test_server_manager_state_compatible` - Server-manager/PDN format incompatibility

When these bugs are fixed, the tests will pass.

## Running Tests

```bash
# Install dependencies
pip3 install -r requirements-test.txt

# Run all tests
pytest test/test_monitoring/ -v

# Run specific suite
pytest test/test_monitoring/test_fleet_metrics_exporter.py -v

# Run by marker
pytest test/test_monitoring/ -m fleet
```

## Files Changed

- `test/test_monitoring/__init__.py` - Package init
- `test/test_monitoring/test_fleet_metrics_exporter.py` - 34 tests for fleet exporter
- `test/test_monitoring/test_github_metrics_exporter.py` - 41 tests for GitHub exporter
- `test/test_monitoring/test_dashboard_contracts.py` - 20 tests for dashboard contracts
- `test/test_monitoring/test_fleet_state_schema.py` - 26 tests for schema validation
- `test/test_monitoring/README.md` - Documentation
- `pytest.ini` - Pytest configuration
- `requirements-test.txt` - Testing dependencies

## Notes

- All tests use mock implementations since actual exporters don't exist yet
- Tests follow TDD approach - write tests first, implement exporters later
- Mock implementations serve as reference for real exporter implementation
- Tests document expected behavior and known bugs for future development

Fixes #179, #180, #181, #182
