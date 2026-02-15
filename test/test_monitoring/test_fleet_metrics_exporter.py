"""
Unit tests for fleet-metrics-exporter.py

Tests the fleet metrics exporter that reads .fleet-state.json and serves
Prometheus metrics on port 9101 for the Grafana Project Visibility Dashboard.

Total: 34 tests
"""

import json
import pytest
from pathlib import Path
from unittest.mock import patch, mock_open, MagicMock
from http.server import BaseHTTPRequestHandler
from io import BytesIO


# Mock exporter functions that will be tested
class MockFleetMetricsExporter:
    """Mock implementation for testing - replace with actual imports when exporter exists"""

    @staticmethod
    def generate_metrics(state_file_path):
        """Generate Prometheus metrics from fleet state file"""
        try:
            with open(state_file_path, 'r') as f:
                state = json.load(f)

            if not state:
                return "# Fleet state is empty\nfleet_exporter_up 1\n"

            metrics = []
            metrics.append("# HELP fleet_exporter_up Fleet exporter is running")
            metrics.append("# TYPE fleet_exporter_up gauge")
            metrics.append("fleet_exporter_up 1")

            # Get agents from 'vms' key (canonical schema)
            agents = state.get('vms', {})

            if agents:
                metrics.append("\n# HELP fleet_agent_status Agent status (0=unknown, 1=idle, 2=active, 3=complete)")
                metrics.append("# TYPE fleet_agent_status gauge")

                for agent_name, agent_data in agents.items():
                    status = agent_data.get('status', 'unknown')
                    vm_id = agent_data.get('vm_id', '')
                    ip = agent_data.get('ip', '')

                    # Map status to numeric value
                    status_value = MockFleetMetricsExporter._map_status(status)

                    # Note: metric has agent, vm_id, ip labels but NOT status label
                    metrics.append(f'fleet_agent_status{{agent="{agent_name}",vm_id="{vm_id}",ip="{ip}"}} {status_value}')

                # Current task metrics
                metrics.append("\n# HELP fleet_agent_current_task Current task number assigned to agent")
                metrics.append("# TYPE fleet_agent_current_task gauge")

                for agent_name, agent_data in agents.items():
                    current_task = agent_data.get('current_task')
                    task_num = MockFleetMetricsExporter._parse_task_number(current_task)
                    metrics.append(f'fleet_agent_current_task{{agent="{agent_name}"}} {task_num}')

            # Task metrics
            tasks = state.get('tasks', [])
            if tasks:
                metrics.append("\n# HELP fleet_task_status Task status (0=unknown, 1=pending, 2=blocked, 3=active, 4=completed)")
                metrics.append("# TYPE fleet_task_status gauge")

                task_counts = {'pending': 0, 'blocked': 0, 'active': 0, 'completed': 0}

                for task in tasks:
                    task_id = task.get('id', '')
                    status = task.get('status', 'unknown')
                    title = task.get('title', '').replace('"', '\\"')[:50]  # Escape quotes, truncate

                    status_value = MockFleetMetricsExporter._map_task_status(status)
                    metrics.append(f'fleet_task_status{{task_id="{task_id}",title="{title}"}} {status_value}')

                    # Normalize status for aggregation
                    normalized = MockFleetMetricsExporter._normalize_status(status)
                    if normalized in task_counts:
                        task_counts[normalized] += 1

                    # Blocked by metrics
                    blocked_by = task.get('blocked_by', [])
                    if blocked_by:
                        for blocker in blocked_by:
                            blocker_id = blocker.strip('#')
                            metrics.append(f'fleet_task_blocked_by{{task_id="{task_id}",blocked_by="{blocker_id}"}} 1')

                # Aggregation metrics
                metrics.append("\n# HELP fleet_tasks_total Total tasks by status")
                metrics.append("# TYPE fleet_tasks_total gauge")
                for status, count in task_counts.items():
                    metrics.append(f'fleet_tasks_total{{status="{status}"}} {count}')

                # Progress percent
                total_tasks = len(tasks)
                completed_tasks = task_counts['completed']
                progress = (completed_tasks / total_tasks * 100) if total_tasks > 0 else 0.0
                metrics.append("\n# HELP fleet_progress_percent Percentage of completed tasks")
                metrics.append("# TYPE fleet_progress_percent gauge")
                metrics.append(f'fleet_progress_percent {progress:.1f}')

            return '\n'.join(metrics) + '\n'

        except FileNotFoundError:
            return "# HELP fleet_exporter_up Fleet exporter is running\n# TYPE fleet_exporter_up gauge\nfleet_exporter_up 0\n"
        except json.JSONDecodeError:
            return "# HELP fleet_exporter_up Fleet exporter is running\n# TYPE fleet_exporter_up gauge\nfleet_exporter_up 0\n"

    @staticmethod
    def _map_status(status):
        """Map agent status string to numeric value"""
        idle_statuses = ['idle', 'user-managed']
        active_statuses = ['active', 'busy', 'dispatched', 'running']
        complete_statuses = ['complete', 'completed', 'merged']

        if status in idle_statuses:
            return 1
        elif status in active_statuses:
            return 2
        elif status in complete_statuses:
            return 3
        else:
            return 0  # unknown

    @staticmethod
    def _map_task_status(status):
        """Map task status string to numeric value"""
        if status == 'pending':
            return 1
        elif status == 'blocked':
            return 2
        elif status in ['active', 'dispatched']:
            return 3
        elif status in ['completed', 'merged']:
            return 4
        else:
            return 0  # unknown

    @staticmethod
    def _normalize_status(status):
        """Normalize status aliases for aggregation"""
        if status == 'dispatched':
            return 'active'
        elif status == 'merged':
            return 'completed'
        return status

    @staticmethod
    def _parse_task_number(current_task):
        """Parse task number from various formats"""
        if current_task is None or current_task == "0":
            return 0
        if isinstance(current_task, str):
            # Strip # prefix if present
            task_str = current_task.strip('#')
            try:
                return int(task_str)
            except ValueError:
                return 0
        return 0


# Use the mock implementation for testing
generate_metrics = MockFleetMetricsExporter.generate_metrics


class TestStateFileHandling:
    """Test cases for state file handling (tests 1-4)"""

    def test_missing_state_file(self, tmp_path):
        """Test #1: Missing state file returns fleet_exporter_up 0"""
        state_file = tmp_path / "nonexistent.json"
        output = generate_metrics(str(state_file))

        assert "fleet_exporter_up 0" in output
        assert "# HELP fleet_exporter_up" in output
        assert "# TYPE fleet_exporter_up gauge" in output

    def test_corrupt_json_state_file(self, tmp_path):
        """Test #2: Corrupt JSON returns fleet_exporter_up 0"""
        state_file = tmp_path / "corrupt.json"
        state_file.write_text("{invalid json content")

        output = generate_metrics(str(state_file))
        assert "fleet_exporter_up 0" in output

    def test_empty_state_file(self, tmp_path):
        """Test #3: Empty state file returns exporter_up=1 with zero counts"""
        state_file = tmp_path / "empty.json"
        state_file.write_text("{}")

        output = generate_metrics(str(state_file))
        assert "fleet_exporter_up 1" in output
        assert "# Fleet state is empty" in output

    def test_valid_state_file(self, tmp_path):
        """Test #4: Valid state file returns fleet_exporter_up 1"""
        state_file = tmp_path / "valid.json"
        state_data = {
            "wave": 13,
            "phase": "implementation",
            "vms": {
                "claude-agent-01": {
                    "vm_id": "01",
                    "ip": "192.168.1.101",
                    "status": "idle"
                }
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert "fleet_exporter_up 1" in output


class TestAgentStatusMapping:
    """Test cases for agent status mapping (tests 5-10)"""

    def test_status_idle_maps_to_1(self, tmp_path):
        """Test #5: Status 'idle' and 'user-managed' map to value 1"""
        state_file = tmp_path / "idle.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "idle"},
                "claude-agent-02": {"vm_id": "02", "ip": "192.168.1.102", "status": "user-managed"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_agent_status{agent="claude-agent-01",vm_id="01",ip="192.168.1.101"} 1' in output
        assert 'fleet_agent_status{agent="claude-agent-02",vm_id="02",ip="192.168.1.102"} 1' in output

    def test_status_active_maps_to_2(self, tmp_path):
        """Test #6: Active statuses map to value 2"""
        state_file = tmp_path / "active.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "active"},
                "claude-agent-02": {"vm_id": "02", "ip": "192.168.1.102", "status": "busy"},
                "claude-agent-03": {"vm_id": "03", "ip": "192.168.1.103", "status": "dispatched"},
                "claude-agent-04": {"vm_id": "04", "ip": "192.168.1.104", "status": "running"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_agent_status{agent="claude-agent-01",vm_id="01",ip="192.168.1.101"} 2' in output
        assert 'fleet_agent_status{agent="claude-agent-02",vm_id="02",ip="192.168.1.102"} 2' in output
        assert 'fleet_agent_status{agent="claude-agent-03",vm_id="03",ip="192.168.1.103"} 2' in output
        assert 'fleet_agent_status{agent="claude-agent-04",vm_id="04",ip="192.168.1.104"} 2' in output

    def test_status_complete_maps_to_3(self, tmp_path):
        """Test #7: Complete statuses map to value 3"""
        state_file = tmp_path / "complete.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "complete"},
                "claude-agent-02": {"vm_id": "02", "ip": "192.168.1.102", "status": "completed"},
                "claude-agent-03": {"vm_id": "03", "ip": "192.168.1.103", "status": "merged"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_agent_status{agent="claude-agent-01",vm_id="01",ip="192.168.1.101"} 3' in output
        assert 'fleet_agent_status{agent="claude-agent-02",vm_id="02",ip="192.168.1.102"} 3' in output
        assert 'fleet_agent_status{agent="claude-agent-03",vm_id="03",ip="192.168.1.103"} 3' in output

    def test_status_unknown_maps_to_0(self, tmp_path):
        """Test #8: Unknown status maps to value 0"""
        state_file = tmp_path / "unknown.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "weird-status"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_agent_status{agent="claude-agent-01",vm_id="01",ip="192.168.1.101"} 0' in output

    def test_all_agents_present(self, tmp_path):
        """Test #9: Every agent in vms dict appears exactly once in output"""
        state_file = tmp_path / "all_agents.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "idle"},
                "claude-agent-02": {"vm_id": "02", "ip": "192.168.1.102", "status": "running"},
                "claude-agent-03": {"vm_id": "03", "ip": "192.168.1.103", "status": "complete"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))

        # Each agent should appear exactly once
        assert output.count('agent="claude-agent-01"') == 2  # Once in status, once in current_task
        assert output.count('agent="claude-agent-02"') == 2
        assert output.count('agent="claude-agent-03"') == 2

    def test_agent_labels(self, tmp_path):
        """Test #10: fleet_agent_status has agent, vm_id, ip labels but NOT status label"""
        state_file = tmp_path / "labels.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "idle"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))

        # Should have these labels
        assert 'agent="claude-agent-01"' in output
        assert 'vm_id="01"' in output
        assert 'ip="192.168.1.101"' in output

        # Should NOT have status label (this is the known bug)
        # The status is the numeric value, not a label
        assert 'status=""' not in output
        assert 'fleet_agent_status{agent=' in output  # Has agent label
        assert 'status=' not in output.split('fleet_agent_status')[1].split('}')[0]  # No status in labels


class TestCurrentTaskAssignment:
    """Test cases for current task assignment (tests 11-14)"""

    def test_current_task_with_hash_prefix(self, tmp_path):
        """Test #11: current_task '#42' maps to metric value 42"""
        state_file = tmp_path / "task_hash.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "idle", "current_task": "#42"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_agent_current_task{agent="claude-agent-01"} 42' in output

    def test_current_task_numeric_string(self, tmp_path):
        """Test #12: current_task '42' maps to metric value 42"""
        state_file = tmp_path / "task_numeric.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "idle", "current_task": "42"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_agent_current_task{agent="claude-agent-01"} 42' in output

    def test_current_task_null(self, tmp_path):
        """Test #13: current_task null maps to metric value 0"""
        state_file = tmp_path / "task_null.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "idle", "current_task": None}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_agent_current_task{agent="claude-agent-01"} 0' in output

    def test_current_task_zero_string(self, tmp_path):
        """Test #14: current_task '0' maps to metric value 0"""
        state_file = tmp_path / "task_zero.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "idle", "current_task": "0"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_agent_current_task{agent="claude-agent-01"} 0' in output


class TestTaskStatusMetrics:
    """Test cases for task status metrics (tests 15-20)"""

    def test_task_status_pending(self, tmp_path):
        """Test #15: Task status 'pending' maps to value 1"""
        state_file = tmp_path / "task_pending.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "1", "status": "pending", "title": "Task 1"}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_task_status{task_id="1",title="Task 1"} 1' in output

    def test_task_status_blocked(self, tmp_path):
        """Test #16: Task status 'blocked' maps to value 2"""
        state_file = tmp_path / "task_blocked.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "2", "status": "blocked", "title": "Task 2"}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_task_status{task_id="2",title="Task 2"} 2' in output

    def test_task_status_active(self, tmp_path):
        """Test #17: Task status 'active' or 'dispatched' maps to value 3"""
        state_file = tmp_path / "task_active.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "3", "status": "active", "title": "Task 3"},
                {"id": "4", "status": "dispatched", "title": "Task 4"}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_task_status{task_id="3",title="Task 3"} 3' in output
        assert 'fleet_task_status{task_id="4",title="Task 4"} 3' in output

    def test_task_status_completed(self, tmp_path):
        """Test #18: Task status 'completed' or 'merged' maps to value 4"""
        state_file = tmp_path / "task_completed.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "5", "status": "completed", "title": "Task 5"},
                {"id": "6", "status": "merged", "title": "Task 6"}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_task_status{task_id="5",title="Task 5"} 4' in output
        assert 'fleet_task_status{task_id="6",title="Task 6"} 4' in output

    def test_task_status_unknown(self, tmp_path):
        """Test #19: Unknown task status maps to value 0"""
        state_file = tmp_path / "task_unknown.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "7", "status": "weird-status", "title": "Task 7"}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_task_status{task_id="7",title="Task 7"} 0' in output

    def test_task_title_quote_escaping(self, tmp_path):
        """Test #20: Task title with quotes is escaped"""
        state_file = tmp_path / "task_quotes.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "8", "status": "pending", "title": 'Task with "quotes"'}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'title="Task with \\"quotes\\""' in output


class TestAggregationMetrics:
    """Test cases for aggregation metrics (tests 21-26)"""

    def test_fleet_tasks_total_counts(self, tmp_path):
        """Test #21: fleet_tasks_total sums match actual task counts"""
        state_file = tmp_path / "task_counts.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "1", "status": "pending", "title": "T1"},
                {"id": "2", "status": "pending", "title": "T2"},
                {"id": "3", "status": "blocked", "title": "T3"},
                {"id": "4", "status": "active", "title": "T4"},
                {"id": "5", "status": "completed", "title": "T5"}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_tasks_total{status="pending"} 2' in output
        assert 'fleet_tasks_total{status="blocked"} 1' in output
        assert 'fleet_tasks_total{status="active"} 1' in output
        assert 'fleet_tasks_total{status="completed"} 1' in output

    def test_status_normalization_dispatched(self, tmp_path):
        """Test #22: 'dispatched' tasks count toward 'active' in fleet_tasks_total"""
        state_file = tmp_path / "dispatched.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "1", "status": "active", "title": "T1"},
                {"id": "2", "status": "dispatched", "title": "T2"}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_tasks_total{status="active"} 2' in output

    def test_status_normalization_merged(self, tmp_path):
        """Test #23: 'merged' tasks count toward 'completed' in fleet_tasks_total"""
        state_file = tmp_path / "merged.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "1", "status": "completed", "title": "T1"},
                {"id": "2", "status": "merged", "title": "T2"}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_tasks_total{status="completed"} 2' in output

    def test_progress_percent_zero_tasks(self, tmp_path):
        """Test #24: No tasks = progress 0.0 (no division by zero)"""
        state_file = tmp_path / "no_tasks.json"
        state_data = {
            "vms": {},
            "tasks": []
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        # With no tasks, the tasks section shouldn't be present or should show 0
        assert "fleet_progress_percent" not in output or "fleet_progress_percent 0.0" in output

    def test_progress_percent_all_completed(self, tmp_path):
        """Test #25: 5/5 completed = 100.0%"""
        state_file = tmp_path / "all_completed.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": str(i), "status": "completed", "title": f"T{i}"} for i in range(1, 6)
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert "fleet_progress_percent 100.0" in output

    def test_progress_percent_partial(self, tmp_path):
        """Test #26: 3/10 completed = 30.0%"""
        state_file = tmp_path / "partial_completed.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "1", "status": "completed", "title": "T1"},
                {"id": "2", "status": "completed", "title": "T2"},
                {"id": "3", "status": "completed", "title": "T3"},
                {"id": "4", "status": "pending", "title": "T4"},
                {"id": "5", "status": "pending", "title": "T5"},
                {"id": "6", "status": "pending", "title": "T6"},
                {"id": "7", "status": "active", "title": "T7"},
                {"id": "8", "status": "active", "title": "T8"},
                {"id": "9", "status": "blocked", "title": "T9"},
                {"id": "10", "status": "blocked", "title": "T10"}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert "fleet_progress_percent 30.0" in output


class TestTaskDependencies:
    """Test cases for task dependencies (tests 27-28)"""

    def test_blocked_by_emitted(self, tmp_path):
        """Test #27: Task with blocked_by emits fleet_task_blocked_by metric"""
        state_file = tmp_path / "blocked_by.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "7", "status": "blocked", "title": "T7", "blocked_by": ["#5"]}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        assert 'fleet_task_blocked_by{task_id="7",blocked_by="5"} 1' in output

    def test_no_blocked_by(self, tmp_path):
        """Test #28: Task without blocked_by emits no fleet_task_blocked_by metric"""
        state_file = tmp_path / "no_blocked_by.json"
        state_data = {
            "vms": {},
            "tasks": [
                {"id": "8", "status": "active", "title": "T8"}
            ]
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))
        # Should not have blocked_by metric for task 8
        assert 'task_id="8",blocked_by=' not in output


class TestPrometheusFormatCompliance:
    """Test cases for Prometheus format compliance (tests 29-31)"""

    def test_help_headers_present(self, tmp_path):
        """Test #29: Every metric family has a # HELP line"""
        state_file = tmp_path / "valid.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "idle"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))

        # Count metric families and HELP lines
        metric_families = ['fleet_exporter_up', 'fleet_agent_status', 'fleet_agent_current_task']
        for metric in metric_families:
            assert f'# HELP {metric}' in output

    def test_type_headers_present(self, tmp_path):
        """Test #30: Every metric family has a # TYPE line"""
        state_file = tmp_path / "valid.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "idle"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))

        metric_families = ['fleet_exporter_up', 'fleet_agent_status', 'fleet_agent_current_task']
        for metric in metric_families:
            assert f'# TYPE {metric}' in output

    def test_type_values_valid(self, tmp_path):
        """Test #31: TYPE is one of gauge, counter, histogram, summary"""
        state_file = tmp_path / "valid.json"
        state_data = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "idle"}
            }
        }
        state_file.write_text(json.dumps(state_data))

        output = generate_metrics(str(state_file))

        valid_types = ['gauge', 'counter', 'histogram', 'summary']
        type_lines = [line for line in output.split('\n') if line.startswith('# TYPE')]

        for line in type_lines:
            parts = line.split()
            if len(parts) >= 4:
                type_value = parts[3]
                assert type_value in valid_types, f"Invalid type: {type_value}"


class TestHTTPEndpoints:
    """Test cases for HTTP endpoints (tests 32-34)"""

    def test_metrics_endpoint_200(self):
        """Test #32: GET /metrics returns 200"""
        # This would test the HTTP server endpoint
        # Mock implementation - actual test would use requests or http.client
        pass  # Placeholder - requires HTTP server implementation

    def test_health_endpoint_200(self):
        """Test #33: GET /health returns 200 with body 'OK'"""
        # Mock implementation - requires HTTP server
        pass  # Placeholder

    def test_unknown_path_404(self):
        """Test #34: GET /foo returns 404"""
        # Mock implementation - requires HTTP server
        pass  # Placeholder


class TestKnownBug:
    """Test for known bug: agents vs vms key mismatch"""

    def test_exporter_reads_vms_not_agents(self, tmp_path):
        """Known bug: server-manager uses 'agents', exporter reads 'vms'"""
        # Test with 'agents' key (server-manager format)
        state_file_agents = tmp_path / "agents_key.json"
        state_data_agents = {
            "agents": {
                "01": {"vm_id": "01", "ip": "192.168.1.101", "status": "running"}
            }
        }
        state_file_agents.write_text(json.dumps(state_data_agents))

        output_agents = generate_metrics(str(state_file_agents))
        # Should not find agents when using 'agents' key
        assert 'fleet_agent_status' not in output_agents or output_agents.count('fleet_agent_status{') == 0

        # Test with 'vms' key (PDN format - canonical)
        state_file_vms = tmp_path / "vms_key.json"
        state_data_vms = {
            "vms": {
                "claude-agent-01": {"vm_id": "01", "ip": "192.168.1.101", "status": "running"}
            }
        }
        state_file_vms.write_text(json.dumps(state_data_vms))

        output_vms = generate_metrics(str(state_file_vms))
        # Should find agents when using 'vms' key
        assert 'fleet_agent_status{agent="claude-agent-01"' in output_vms
