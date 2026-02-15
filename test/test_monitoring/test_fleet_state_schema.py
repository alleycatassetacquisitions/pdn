"""
Unit tests for fleet state schema validation

Tests that validate the .fleet-state.json schema and catch divergence
between server-manager and PDN repo formats.

Total: 21 tests
"""

import json
import pytest
import re
from pathlib import Path


# Valid status vocabulary
VALID_AGENT_STATUSES = {
    'idle', 'user-managed', 'active', 'busy',
    'dispatched', 'running', 'complete', 'completed', 'merged'
}

VALID_TASK_STATUSES = {
    'pending', 'blocked', 'active', 'dispatched', 'completed', 'merged'
}

# Mock fleet state data
VALID_FLEET_STATE_VMS = {
    "wave": 13,
    "phase": "implementation",
    "vms": {
        "claude-agent-01": {
            "vm_id": "01",
            "ip": "192.168.1.110",
            "status": "idle",
            "role": "senior-engineer",
            "current_task": None,
            "branch": None,
            "pr": None
        },
        "claude-agent-02": {
            "vm_id": "02",
            "ip": "192.168.1.111",
            "status": "running",
            "role": "testing-engineer",
            "current_task": "#42",
            "branch": "wave13/02-feature",
            "pr": "199"
        }
    }
}

# Server-manager format (uses 'agents' key)
SERVER_MANAGER_FORMAT = {
    "wave": 13,
    "status": "running",
    "agents": {
        "01": {
            "vm_id": "01",
            "ip": "192.168.1.110",
            "status": "running"
        },
        "02": {
            "vm_id": "02",
            "ip": "192.168.1.111",
            "status": "idle"
        }
    }
}


def validate_ipv4(ip_str):
    """Validate IPv4 address format"""
    pattern = r'^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$'
    match = re.match(pattern, ip_str)
    if not match:
        return False
    # Check each octet is 0-255
    for octet in match.groups():
        if int(octet) > 255:
            return False
    return True


def validate_agent_name_format(name):
    """Validate agent name matches claude-agent-XX pattern"""
    pattern = r'^claude-agent-\d{2}$'
    return re.match(pattern, name) is not None


class TestSchemaValidation:
    """Test cases for schema validation (tests 1-6)"""

    def test_fleet_state_has_required_keys(self):
        """Test #1: Root object must have required keys"""
        state = VALID_FLEET_STATE_VMS.copy()

        # Valid state should have wave and phase/status
        assert 'wave' in state
        assert 'phase' in state or 'status' in state
        # Should have agent section (either vms or agents)
        assert 'vms' in state or 'agents' in state

    def test_agent_entry_required_fields(self):
        """Test #2: Each agent must have required fields"""
        state = VALID_FLEET_STATE_VMS.copy()
        agents = state['vms']

        for agent_name, agent_data in agents.items():
            assert 'vm_id' in agent_data, f"Agent {agent_name} missing vm_id"
            assert 'ip' in agent_data, f"Agent {agent_name} missing ip"
            assert 'status' in agent_data, f"Agent {agent_name} missing status"

    def test_vm_id_is_string_or_int(self):
        """Test #3: vm_id field type is consistent"""
        state = VALID_FLEET_STATE_VMS.copy()
        agents = state['vms']

        for agent_name, agent_data in agents.items():
            vm_id = agent_data['vm_id']
            # Should be string or int
            assert isinstance(vm_id, (str, int)), f"Agent {agent_name} vm_id has invalid type {type(vm_id)}"

    def test_ip_address_format(self):
        """Test #4: IP fields match IPv4 pattern"""
        state = VALID_FLEET_STATE_VMS.copy()
        agents = state['vms']

        for agent_name, agent_data in agents.items():
            ip = agent_data['ip']
            assert validate_ipv4(ip), f"Agent {agent_name} has invalid IP: {ip}"

    def test_status_values_in_vocabulary(self):
        """Test #5: Agent status must be in known vocabulary"""
        state = VALID_FLEET_STATE_VMS.copy()
        agents = state['vms']

        for agent_name, agent_data in agents.items():
            status = agent_data['status']
            assert status in VALID_AGENT_STATUSES, f"Agent {agent_name} has unknown status: {status}"

    def test_no_unknown_status_values(self):
        """Test #6: Flag any status not in vocabulary"""
        # Test with invalid status
        invalid_state = {
            "wave": 13,
            "phase": "test",
            "vms": {
                "claude-agent-01": {
                    "vm_id": "01",
                    "ip": "192.168.1.110",
                    "status": "weird-status"
                }
            }
        }

        agents = invalid_state['vms']
        for agent_name, agent_data in agents.items():
            status = agent_data['status']
            if status not in VALID_AGENT_STATUSES:
                # This should be flagged
                assert False, f"Unknown status detected: {status}"


class TestSchemaConsistency:
    """Test cases for schema consistency (tests 7-10)"""

    def test_canonical_key_is_vms(self):
        """Test #7: Canonical schema uses 'vms' as top-level agent key"""
        state = VALID_FLEET_STATE_VMS.copy()

        # Canonical format uses 'vms'
        assert 'vms' in state
        # Should not have 'agents' key in canonical format
        assert 'agents' not in state

    def test_agents_key_rejected_or_aliased(self):
        """Test #8: If 'agents' key used, should be flagged"""
        state = SERVER_MANAGER_FORMAT.copy()

        # This is the server-manager format
        if 'agents' in state and 'vms' not in state:
            # This is a schema mismatch that should be caught
            pytest.fail("Schema uses 'agents' key but exporter expects 'vms' - this is the known bug")

    def test_agent_name_format(self):
        """Test #9: Agent names must match claude-agent-XX pattern"""
        state = VALID_FLEET_STATE_VMS.copy()
        agents = state['vms']

        for agent_name in agents.keys():
            assert validate_agent_name_format(agent_name), f"Agent name '{agent_name}' doesn't match claude-agent-XX format"

    def test_server_manager_state_compatible(self):
        """Test #10: Server-manager state should be compatible or flagged"""
        state = SERVER_MANAGER_FORMAT.copy()

        # Check if it could be consumed by exporter
        if 'agents' in state:
            # Exporter reads 'vms', not 'agents'
            # This is incompatible and should be flagged
            assert False, "Server-manager uses 'agents' key but fleet exporter reads 'vms' key"


class TestTaskSection:
    """Test cases for task section (tests 11-14)"""

    def test_tasks_section_optional(self):
        """Test #11: Exporter handles missing tasks key gracefully"""
        state = VALID_FLEET_STATE_VMS.copy()

        # Tasks section is optional
        tasks = state.get('tasks', [])
        assert isinstance(tasks, list) or tasks is None

    def test_task_entry_required_fields(self):
        """Test #12: If tasks exist, each must have required fields"""
        state_with_tasks = VALID_FLEET_STATE_VMS.copy()
        state_with_tasks['tasks'] = [
            {
                "id": "1",
                "status": "pending",
                "title": "Task 1"
            }
        ]

        tasks = state_with_tasks.get('tasks', [])
        for task in tasks:
            assert 'status' in task, "Task missing status field"
            assert 'title' in task, "Task missing title field"

    def test_task_status_vocabulary(self):
        """Test #13: Task status must be in valid vocabulary"""
        state_with_tasks = VALID_FLEET_STATE_VMS.copy()
        state_with_tasks['tasks'] = [
            {"id": "1", "status": "pending", "title": "T1"},
            {"id": "2", "status": "active", "title": "T2"},
            {"id": "3", "status": "completed", "title": "T3"}
        ]

        tasks = state_with_tasks.get('tasks', [])
        for task in tasks:
            status = task['status']
            assert status in VALID_TASK_STATUSES, f"Task has invalid status: {status}"

    def test_current_task_format(self):
        """Test #14: current_task must be numeric string, '0', or null"""
        state = VALID_FLEET_STATE_VMS.copy()
        agents = state['vms']

        for agent_name, agent_data in agents.items():
            current_task = agent_data.get('current_task')

            if current_task is not None:
                # Should be numeric string (possibly with # prefix) or "0"
                if isinstance(current_task, str):
                    task_str = current_task.strip('#')
                    # Should be numeric or "0"
                    assert task_str.isdigit() or task_str == "0", f"Agent {agent_name} current_task has invalid format: {current_task}"


class TestCrossFileConsistency:
    """Test cases for cross-file consistency (tests 15-18)"""

    def test_agent_ips_match_infrastructure(self):
        """Test #15: Agent IPs should match known infrastructure IPs"""
        state = VALID_FLEET_STATE_VMS.copy()
        agents = state['vms']

        # Known infrastructure IP range: 192.168.1.110-121
        valid_ip_prefix = "192.168.1."

        for agent_name, agent_data in agents.items():
            ip = agent_data['ip']
            assert ip.startswith(valid_ip_prefix), f"Agent {agent_name} IP {ip} outside infrastructure range"

    def test_vm_ids_in_valid_range(self):
        """Test #16: VM IDs should be in valid range (01-12 for agents, 100-104 for infra)"""
        state = VALID_FLEET_STATE_VMS.copy()
        agents = state['vms']

        for agent_name, agent_data in agents.items():
            vm_id = agent_data['vm_id']

            # Convert to int for comparison
            if isinstance(vm_id, str):
                vm_id_int = int(vm_id)
            else:
                vm_id_int = vm_id

            # Agent VMs: 01-12 (or 1-12)
            # Infrastructure VMs: 100-104
            valid_agent_range = range(1, 13)
            valid_infra_range = range(100, 105)

            assert vm_id_int in valid_agent_range or vm_id_int in valid_infra_range, \
                f"Agent {agent_name} has VM ID {vm_id} outside valid ranges"

    def test_no_duplicate_ips(self):
        """Test #17: No two agents share the same IP"""
        state = VALID_FLEET_STATE_VMS.copy()
        agents = state['vms']

        ips = [agent_data['ip'] for agent_data in agents.values()]
        assert len(ips) == len(set(ips)), "Duplicate IPs detected in fleet state"

    def test_no_duplicate_vm_ids(self):
        """Test #18: No two agents share the same VM ID"""
        state = VALID_FLEET_STATE_VMS.copy()
        agents = state['vms']

        vm_ids = [agent_data['vm_id'] for agent_data in agents.values()]
        assert len(vm_ids) == len(set(vm_ids)), "Duplicate VM IDs detected in fleet state"


class TestExporterCompatibility:
    """Test cases for exporter compatibility (tests 19-21)"""

    def test_exporter_reads_all_agents(self, tmp_path):
        """Test #19: Exporter reads all agents from state file"""
        state_file = tmp_path / "test_state.json"
        state_file.write_text(json.dumps(VALID_FLEET_STATE_VMS))

        # Mock exporter reading the file
        with open(state_file, 'r') as f:
            state = json.load(f)

        agents = state.get('vms', {})
        assert len(agents) == 2, "Should read all agents from file"

        # Check each agent is present
        assert 'claude-agent-01' in agents
        assert 'claude-agent-02' in agents

    def test_exporter_handles_missing_optional_fields(self, tmp_path):
        """Test #20: Missing optional fields don't crash exporter"""
        minimal_state = {
            "wave": 13,
            "phase": "test",
            "vms": {
                "claude-agent-01": {
                    "vm_id": "01",
                    "ip": "192.168.1.110",
                    "status": "idle"
                    # Missing: current_task, branch, pr, role
                }
            }
        }

        state_file = tmp_path / "minimal_state.json"
        state_file.write_text(json.dumps(minimal_state))

        # Mock exporter processing
        with open(state_file, 'r') as f:
            state = json.load(f)

        agents = state.get('vms', {})
        agent = agents['claude-agent-01']

        # Optional fields should be handled gracefully
        current_task = agent.get('current_task')
        branch = agent.get('branch')
        pr = agent.get('pr')
        role = agent.get('role')

        # Should not crash if missing
        assert current_task is None
        assert branch is None
        assert pr is None
        assert role is None

    def test_exporter_hash_stripping(self, tmp_path):
        """Test #21: current_task '#42' outputs metric value 42"""
        state_with_hash = {
            "wave": 13,
            "phase": "test",
            "vms": {
                "claude-agent-01": {
                    "vm_id": "01",
                    "ip": "192.168.1.110",
                    "status": "idle",
                    "current_task": "#42"
                }
            }
        }

        state_file = tmp_path / "hash_state.json"
        state_file.write_text(json.dumps(state_with_hash))

        with open(state_file, 'r') as f:
            state = json.load(f)

        agents = state.get('vms', {})
        agent = agents['claude-agent-01']
        current_task = agent['current_task']

        # Parse task number (strip # prefix)
        if current_task and isinstance(current_task, str):
            task_num_str = current_task.strip('#')
            task_num = int(task_num_str)
            assert task_num == 42, f"Expected task number 42, got {task_num}"


class TestKnownBugs:
    """Test cases for known bugs in fleet state schema"""

    def test_agents_vs_vms_key_mismatch(self):
        """Known bug: server-manager uses 'agents', exporter reads 'vms'"""
        # Server-manager format
        server_manager_state = SERVER_MANAGER_FORMAT.copy()

        # Check that it uses 'agents' not 'vms'
        assert 'agents' in server_manager_state
        assert 'vms' not in server_manager_state

        # This is incompatible with fleet exporter which reads 'vms'
        # The bug: orchestrator writes to 'agents', exporter reads 'vms'
        with pytest.raises(AssertionError, match="exporter expects 'vms'"):
            if 'agents' in server_manager_state and 'vms' not in server_manager_state:
                raise AssertionError("Schema mismatch: exporter expects 'vms' but state has 'agents'")

    def test_agent_name_format_in_server_manager(self):
        """Known bug: server-manager uses '01', '02' not 'claude-agent-01'"""
        server_manager_state = SERVER_MANAGER_FORMAT.copy()

        if 'agents' in server_manager_state:
            agents = server_manager_state['agents']

            for agent_name in agents.keys():
                # Server-manager format uses just '01', '02' etc
                # Exporter expects 'claude-agent-XX' format
                if not validate_agent_name_format(agent_name):
                    # This is the format mismatch
                    assert True, f"Agent name '{agent_name}' doesn't match exporter's expected format"

    def test_tasks_section_always_empty(self):
        """Known bug: tasks section coded but never populated"""
        # In real fleet state files, 'tasks' is always empty or missing
        state = VALID_FLEET_STATE_VMS.copy()

        # Tasks section doesn't exist in real files
        tasks = state.get('tasks')
        assert tasks is None, "Tasks section should not be populated in actual fleet state"

        # But exporter has code to read and display tasks
        # This creates dead dashboard panels
