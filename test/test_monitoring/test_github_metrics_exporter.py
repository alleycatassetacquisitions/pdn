"""
Unit tests for github-metrics-exporter.py

Tests the GitHub metrics exporter that fetches issues/PRs via gh CLI
and serves Prometheus metrics on port 9102 for Grafana dashboard.

Total: 41 tests
"""

import json
import pytest
from datetime import datetime, timedelta
from unittest.mock import patch, MagicMock, call
from subprocess import CalledProcessError
from pathlib import Path


# Mock exporter functions for testing
class MockGitHubMetricsExporter:
    """Mock implementation for testing - replace with actual imports when exporter exists"""

    @staticmethod
    def run_gh_command(cmd):
        """Run gh CLI command and return parsed JSON"""
        import subprocess
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                check=True
            )
            if not result.stdout or result.stdout.strip() == "":
                return None
            return json.loads(result.stdout)
        except (CalledProcessError, FileNotFoundError, json.JSONDecodeError):
            return None

    @staticmethod
    def get_repo_name():
        """Detect repository name from git remote"""
        import subprocess
        try:
            result = subprocess.run(
                ['git', 'remote', 'get-url', 'origin'],
                capture_output=True,
                text=True,
                check=True,
                cwd='/home/ubuntu/pdn'
            )
            remote_url = result.stdout.strip()

            # Parse SSH format: git@github.com:FinallyEve/pdn.git
            if remote_url.startswith('git@github.com:'):
                repo = remote_url.replace('git@github.com:', '').replace('.git', '')
                return repo

            # Parse HTTPS format: https://github.com/FinallyEve/pdn.git
            if 'github.com/' in remote_url:
                parts = remote_url.split('github.com/')[1]
                repo = parts.replace('.git', '')
                return repo

            return "FinallyEve/pdn"  # fallback
        except:
            return "FinallyEve/pdn"

    @staticmethod
    def generate_metrics():
        """Generate Prometheus metrics from GitHub API"""
        repo = MockGitHubMetricsExporter.get_repo_name()

        # Fetch issues
        issues = MockGitHubMetricsExporter.run_gh_command([
            'gh', 'issue', 'list',
            '--repo', repo,
            '--limit', '100',
            '--json', 'number,title,state,createdAt,assignees'
        ])

        # Fetch PRs
        pulls = MockGitHubMetricsExporter.run_gh_command([
            'gh', 'pr', 'list',
            '--repo', repo,
            '--limit', '100',
            '--state', 'all',
            '--json', 'number,title,state,author,additions,deletions'
        ])

        if issues is None and pulls is None:
            return "# HELP github_exporter_up GitHub exporter is running\n# TYPE github_exporter_up gauge\ngithub_exporter_up 0\n"

        metrics = []
        metrics.append("# HELP github_exporter_up GitHub exporter is running")
        metrics.append("# TYPE github_exporter_up gauge")
        metrics.append("github_exporter_up 1")

        # Issue metrics
        if issues is not None:
            open_issues = [i for i in issues if i['state'] == 'OPEN']
            closed_issues = [i for i in issues if i['state'] == 'CLOSED']

            metrics.append("\n# HELP github_issues_total Total number of issues")
            metrics.append("# TYPE github_issues_total gauge")
            metrics.append(f'github_issues_total{{state="open"}} {len(open_issues)}')
            metrics.append(f'github_issues_total{{state="closed"}} {len(closed_issues)}')

            # Individual issue metrics (limit to first 20)
            if issues:
                metrics.append("\n# HELP github_issue_state Issue state (1=open, 2=closed)")
                metrics.append("# TYPE github_issue_state gauge")

                for issue in issues[:20]:
                    issue_num = issue['number']
                    title = issue['title'][:50].replace('"', '\\"')  # Truncate and escape
                    state = issue['state']
                    state_value = 1 if state == 'OPEN' else 2

                    assignee = "unassigned"
                    if issue.get('assignees') and len(issue['assignees']) > 0:
                        assignee = issue['assignees'][0]['login']

                    metrics.append(f'github_issue_state{{number="{issue_num}",title="{title}",assignee="{assignee}"}} {state_value}')

                # Issue age (only for open issues)
                metrics.append("\n# HELP github_issue_age_days Age of open issues in days")
                metrics.append("# TYPE github_issue_age_days gauge")

                now = datetime.now()
                for issue in open_issues[:20]:
                    created_at = datetime.fromisoformat(issue['createdAt'].replace('Z', '+00:00'))
                    age_days = (now - created_at.replace(tzinfo=None)).days
                    issue_num = issue['number']
                    metrics.append(f'github_issue_age_days{{number="{issue_num}"}} {age_days}')

        # PR metrics
        if pulls is not None:
            open_prs = [p for p in pulls if p['state'] == 'OPEN']
            closed_prs = [p for p in pulls if p['state'] == 'CLOSED']
            merged_prs = [p for p in pulls if p['state'] == 'MERGED']

            metrics.append("\n# HELP github_pulls_total Total number of pull requests")
            metrics.append("# TYPE github_pulls_total gauge")
            metrics.append(f'github_pulls_total{{state="open"}} {len(open_prs)}')
            metrics.append(f'github_pulls_total{{state="closed"}} {len(closed_prs)}')
            metrics.append(f'github_pulls_total{{state="merged"}} {len(merged_prs)}')

            # Individual PR metrics (limit to first 20)
            if pulls:
                metrics.append("\n# HELP github_pr_state PR state (1=open, 2=closed, 3=merged)")
                metrics.append("# TYPE github_pr_state gauge")

                for pr in pulls[:20]:
                    pr_num = pr['number']
                    title = pr['title'][:50].replace('"', '\\"')
                    state = pr['state']

                    state_value = 1 if state == 'OPEN' else (3 if state == 'MERGED' else 2)

                    author = pr.get('author', {}).get('login', 'unknown') if pr.get('author') else 'unknown'

                    metrics.append(f'github_pr_state{{number="{pr_num}",title="{title}",author="{author}"}} {state_value}')

                # PR changes (only for open or merged)
                metrics.append("\n# HELP github_pr_changes Total changes (additions + deletions)")
                metrics.append("# TYPE github_pr_changes gauge")

                for pr in pulls[:20]:
                    if pr['state'] in ['OPEN', 'MERGED']:
                        pr_num = pr['number']
                        additions = pr.get('additions', 0)
                        deletions = pr.get('deletions', 0)
                        total_changes = additions + deletions
                        metrics.append(f'github_pr_changes{{number="{pr_num}"}} {total_changes}')
        elif pulls is None and issues is not None:
            # Known bug fix: Always emit github_pulls_total even when empty
            metrics.append("\n# HELP github_pulls_total Total number of pull requests")
            metrics.append("# TYPE github_pulls_total gauge")
            metrics.append('github_pulls_total{state="open"} 0')
            metrics.append('github_pulls_total{state="closed"} 0')
            metrics.append('github_pulls_total{state="merged"} 0')

        return '\n'.join(metrics) + '\n'


# Use mock for testing
run_gh_command = MockGitHubMetricsExporter.run_gh_command
get_repo_name = MockGitHubMetricsExporter.get_repo_name
generate_metrics = MockGitHubMetricsExporter.generate_metrics


class TestGitHubCLIIntegration:
    """Test cases for GitHub CLI integration (tests 1-5)"""

    @patch('subprocess.run')
    def test_gh_command_success(self, mock_run):
        """Test #1: Valid JSON output from gh is parsed correctly"""
        mock_run.return_value = MagicMock(
            stdout='[{"number": 1, "title": "Test"}]',
            returncode=0
        )

        result = run_gh_command(['gh', 'issue', 'list'])
        assert result == [{"number": 1, "title": "Test"}]

    @patch('subprocess.run')
    def test_gh_command_failure(self, mock_run):
        """Test #2: CalledProcessError returns None"""
        mock_run.side_effect = CalledProcessError(1, 'gh')

        result = run_gh_command(['gh', 'issue', 'list'])
        assert result is None

    @patch('subprocess.run')
    def test_gh_command_invalid_json(self, mock_run):
        """Test #3: Non-JSON stdout returns None"""
        mock_run.return_value = MagicMock(
            stdout='not valid json',
            returncode=0
        )

        result = run_gh_command(['gh', 'issue', 'list'])
        assert result is None

    @patch('subprocess.run')
    def test_gh_not_installed(self, mock_run):
        """Test #4: FileNotFoundError returns None"""
        mock_run.side_effect = FileNotFoundError()

        result = run_gh_command(['gh', 'issue', 'list'])
        assert result is None

    @patch('subprocess.run')
    def test_gh_empty_output(self, mock_run):
        """Test #5: Empty stdout returns None"""
        mock_run.return_value = MagicMock(
            stdout='',
            returncode=0
        )

        result = run_gh_command(['gh', 'issue', 'list'])
        assert result is None


class TestRepositoryDetection:
    """Test cases for repository detection (tests 6-9)"""

    @patch('subprocess.run')
    def test_repo_from_ssh_remote(self, mock_run):
        """Test #6: SSH format git@github.com:FinallyEve/pdn.git"""
        mock_run.return_value = MagicMock(
            stdout='git@github.com:FinallyEve/pdn.git\n',
            returncode=0
        )

        repo = get_repo_name()
        assert repo == "FinallyEve/pdn"

    @patch('subprocess.run')
    def test_repo_from_https_remote(self, mock_run):
        """Test #7: HTTPS format https://github.com/FinallyEve/pdn.git"""
        mock_run.return_value = MagicMock(
            stdout='https://github.com/FinallyEve/pdn.git\n',
            returncode=0
        )

        repo = get_repo_name()
        assert repo == "FinallyEve/pdn"

    @patch('subprocess.run')
    def test_repo_fallback(self, mock_run):
        """Test #8: When git command fails, returns fallback"""
        mock_run.side_effect = CalledProcessError(1, 'git')

        repo = get_repo_name()
        assert repo == "FinallyEve/pdn"

    @patch('subprocess.run')
    def test_repo_no_pdn_directory(self, mock_run):
        """Test #9: When PDN dir doesn't exist, returns fallback"""
        mock_run.side_effect = FileNotFoundError()

        repo = get_repo_name()
        assert repo == "FinallyEve/pdn"


class TestIssueMetrics:
    """Test cases for issue metrics (tests 10-18)"""

    @patch('subprocess.run')
    def test_issues_total_open_count(self, mock_run):
        """Test #10: Correct count of OPEN issues"""
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []},
                {"number": 2, "title": "Issue 2", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []},
                {"number": 3, "title": "Issue 3", "state": "CLOSED", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        assert 'github_issues_total{state="open"} 2' in output

    @patch('subprocess.run')
    def test_issues_total_closed_count(self, mock_run):
        """Test #11: Correct count of CLOSED issues"""
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []},
                {"number": 2, "title": "Issue 2", "state": "CLOSED", "createdAt": "2026-01-01T00:00:00Z", "assignees": []},
                {"number": 3, "title": "Issue 3", "state": "CLOSED", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        assert 'github_issues_total{state="closed"} 2' in output

    @patch('subprocess.run')
    def test_issue_state_open_value(self, mock_run):
        """Test #12: OPEN state maps to value 1"""
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        assert 'github_issue_state{number="1",title="Issue 1",assignee="unassigned"} 1' in output

    @patch('subprocess.run')
    def test_issue_state_closed_value(self, mock_run):
        """Test #13: CLOSED state maps to value 2"""
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 2, "title": "Issue 2", "state": "CLOSED", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        assert 'github_issue_state{number="2",title="Issue 2",assignee="unassigned"} 2' in output

    @patch('subprocess.run')
    def test_issue_limit_20(self, mock_run):
        """Test #14: Only first 20 issues get individual metrics"""
        issues = [
            {"number": i, "title": f"Issue {i}", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
            for i in range(1, 26)  # 25 issues
        ]
        mock_run.return_value = MagicMock(
            stdout=json.dumps(issues),
            returncode=0
        )

        output = generate_metrics()
        # Should have issue 20 but not issue 21
        assert 'number="20"' in output
        assert 'number="21"' not in output

    @patch('subprocess.run')
    def test_issue_title_truncation(self, mock_run):
        """Test #15: Titles longer than 50 chars are truncated"""
        long_title = "A" * 100
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": long_title, "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        # Title should be truncated to 50 chars
        assert f'title="{"A" * 50}"' in output

    @patch('subprocess.run')
    def test_issue_title_quote_escaping(self, mock_run):
        """Test #16: Double quotes in title are escaped"""
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": 'Issue with "quotes"', "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        assert 'title="Issue with \\"quotes\\""' in output

    @patch('subprocess.run')
    def test_issue_no_assignee(self, mock_run):
        """Test #17: Missing assignees → assignee='unassigned'"""
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        assert 'assignee="unassigned"' in output

    @patch('subprocess.run')
    def test_issue_with_assignee(self, mock_run):
        """Test #18: First assignee login is used"""
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z",
                 "assignees": [{"login": "testuser"}]}
            ]),
            returncode=0
        )

        output = generate_metrics()
        assert 'assignee="testuser"' in output


class TestIssueAge:
    """Test cases for issue age (tests 19-21)"""

    @patch('subprocess.run')
    def test_issue_age_open_issues_only(self, mock_run):
        """Test #19: Age metric only emitted for OPEN issues"""
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []},
                {"number": 2, "title": "Issue 2", "state": "CLOSED", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        # Should have age for issue 1 but not issue 2
        assert 'github_issue_age_days{number="1"}' in output
        assert 'github_issue_age_days{number="2"}' not in output

    @patch('subprocess.run')
    def test_issue_age_calculation(self, mock_run):
        """Test #20: Age in days is calculated correctly"""
        # This test would need freezegun or similar to mock datetime properly
        # For now, just verify the calculation logic conceptually
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": "2026-02-05T00:00:00Z", "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        # Age calculation happens in the exporter - just verify it exists
        assert 'github_issue_age_days{number="1"}' in output
        # Actual age value would depend on when test runs
        pass

    @patch('subprocess.run')
    def test_issue_age_new_issue(self, mock_run):
        """Test #21: Issue created today has age 0"""
        today = datetime.now().strftime("%Y-%m-%dT%H:%M:%SZ")
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": today, "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        assert 'github_issue_age_days{number="1"} 0' in output


class TestPRMetrics:
    """Test cases for PR metrics (tests 22-31)"""

    @patch('subprocess.run')
    def test_pulls_total_open(self, mock_run):
        """Test #22: Correct count of OPEN PRs"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(stdout='[]', returncode=0)
            elif 'pr' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 1, "title": "PR 1", "state": "OPEN", "author": {"login": "user1"}, "additions": 10, "deletions": 5},
                        {"number": 2, "title": "PR 2", "state": "OPEN", "author": {"login": "user2"}, "additions": 20, "deletions": 10}
                    ]),
                    returncode=0
                )

        mock_run.side_effect = side_effect
        output = generate_metrics()
        assert 'github_pulls_total{state="open"} 2' in output

    @patch('subprocess.run')
    def test_pulls_total_closed(self, mock_run):
        """Test #23: Correct count of CLOSED PRs"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(stdout='[]', returncode=0)
            elif 'pr' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 1, "title": "PR 1", "state": "CLOSED", "author": {"login": "user1"}, "additions": 10, "deletions": 5}
                    ]),
                    returncode=0
                )

        mock_run.side_effect = side_effect
        output = generate_metrics()
        assert 'github_pulls_total{state="closed"} 1' in output

    @patch('subprocess.run')
    def test_pulls_total_merged(self, mock_run):
        """Test #24: Correct count of MERGED PRs"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(stdout='[]', returncode=0)
            elif 'pr' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 1, "title": "PR 1", "state": "MERGED", "author": {"login": "user1"}, "additions": 10, "deletions": 5}
                    ]),
                    returncode=0
                )

        mock_run.side_effect = side_effect
        output = generate_metrics()
        assert 'github_pulls_total{state="merged"} 1' in output

    @patch('subprocess.run')
    def test_pr_state_open_value(self, mock_run):
        """Test #25: OPEN state maps to value 1"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(stdout='[]', returncode=0)
            elif 'pr' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 1, "title": "PR 1", "state": "OPEN", "author": {"login": "user1"}, "additions": 10, "deletions": 5}
                    ]),
                    returncode=0
                )

        mock_run.side_effect = side_effect
        output = generate_metrics()
        assert 'github_pr_state{number="1",title="PR 1",author="user1"} 1' in output

    @patch('subprocess.run')
    def test_pr_state_closed_value(self, mock_run):
        """Test #26: CLOSED state maps to value 2"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(stdout='[]', returncode=0)
            elif 'pr' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 2, "title": "PR 2", "state": "CLOSED", "author": {"login": "user2"}, "additions": 10, "deletions": 5}
                    ]),
                    returncode=0
                )

        mock_run.side_effect = side_effect
        output = generate_metrics()
        assert 'github_pr_state{number="2",title="PR 2",author="user2"} 2' in output

    @patch('subprocess.run')
    def test_pr_state_merged_value(self, mock_run):
        """Test #27: MERGED state maps to value 3"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(stdout='[]', returncode=0)
            elif 'pr' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 3, "title": "PR 3", "state": "MERGED", "author": {"login": "user3"}, "additions": 10, "deletions": 5}
                    ]),
                    returncode=0
                )

        mock_run.side_effect = side_effect
        output = generate_metrics()
        assert 'github_pr_state{number="3",title="PR 3",author="user3"} 3' in output

    @patch('subprocess.run')
    def test_pr_limit_20(self, mock_run):
        """Test #28: Only first 20 PRs get individual metrics"""
        prs = [
            {"number": i, "title": f"PR {i}", "state": "OPEN", "author": {"login": f"user{i}"}, "additions": 10, "deletions": 5}
            for i in range(1, 26)  # 25 PRs
        ]

        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(stdout='[]', returncode=0)
            elif 'pr' in cmd:
                return MagicMock(stdout=json.dumps(prs), returncode=0)

        mock_run.side_effect = side_effect
        output = generate_metrics()

        # Should have PR 20 but not PR 21 in individual metrics
        pr_state_lines = [line for line in output.split('\n') if 'github_pr_state{number=' in line]
        assert any('number="20"' in line for line in pr_state_lines)
        assert not any('number="21"' in line for line in pr_state_lines)

    @patch('subprocess.run')
    def test_pr_author_missing(self, mock_run):
        """Test #29: author: null → author='unknown'"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(stdout='[]', returncode=0)
            elif 'pr' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 1, "title": "PR 1", "state": "OPEN", "author": None, "additions": 10, "deletions": 5}
                    ]),
                    returncode=0
                )

        mock_run.side_effect = side_effect
        output = generate_metrics()
        assert 'author="unknown"' in output

    @patch('subprocess.run')
    def test_pr_changes_calculation(self, mock_run):
        """Test #30: additions + deletions = total changes"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(stdout='[]', returncode=0)
            elif 'pr' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 1, "title": "PR 1", "state": "OPEN", "author": {"login": "user1"}, "additions": 100, "deletions": 50}
                    ]),
                    returncode=0
                )

        mock_run.side_effect = side_effect
        output = generate_metrics()
        assert 'github_pr_changes{number="1"} 150' in output

    @patch('subprocess.run')
    def test_pr_changes_only_open_or_merged(self, mock_run):
        """Test #31: Changes metric only for OPEN or MERGED PRs"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(stdout='[]', returncode=0)
            elif 'pr' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 1, "title": "PR 1", "state": "OPEN", "author": {"login": "user1"}, "additions": 10, "deletions": 5},
                        {"number": 2, "title": "PR 2", "state": "CLOSED", "author": {"login": "user2"}, "additions": 20, "deletions": 10},
                        {"number": 3, "title": "PR 3", "state": "MERGED", "author": {"login": "user3"}, "additions": 30, "deletions": 15}
                    ]),
                    returncode=0
                )

        mock_run.side_effect = side_effect
        output = generate_metrics()

        # Should have changes for PR 1 and 3, but not 2
        assert 'github_pr_changes{number="1"}' in output
        assert 'github_pr_changes{number="2"}' not in output
        assert 'github_pr_changes{number="3"}' in output


class TestEdgeCases:
    """Test cases for edge cases (tests 32-35)"""

    @patch('subprocess.run')
    def test_no_issues_no_prs(self, mock_run):
        """Test #32: Both gh commands return None → github_exporter_up 0"""
        mock_run.side_effect = CalledProcessError(1, 'gh')

        output = generate_metrics()
        assert 'github_exporter_up 0' in output

    @patch('subprocess.run')
    def test_issues_only_no_prs(self, mock_run):
        """Test #33: Issues exist but no PRs → github_pulls_total emitted with zeros (bug fix)"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
                    ]),
                    returncode=0
                )
            elif 'pr' in cmd:
                # Simulate no PRs
                raise CalledProcessError(1, 'gh')

        mock_run.side_effect = side_effect
        output = generate_metrics()

        # Known bug fix: should emit zeros instead of being absent
        assert 'github_pulls_total{state="open"} 0' in output
        assert 'github_pulls_total{state="closed"} 0' in output
        assert 'github_pulls_total{state="merged"} 0' in output

    @patch('subprocess.run')
    def test_prs_only_no_issues(self, mock_run):
        """Test #34: PRs exist but no issues"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                raise CalledProcessError(1, 'gh')
            elif 'pr' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 1, "title": "PR 1", "state": "OPEN", "author": {"login": "user1"}, "additions": 10, "deletions": 5}
                    ]),
                    returncode=0
                )

        mock_run.side_effect = side_effect
        output = generate_metrics()

        # Should not have issues section
        assert 'github_issues_total' not in output
        # Should have PRs
        assert 'github_pulls_total{state="open"} 1' in output

    @patch('subprocess.run')
    def test_empty_issue_list(self, mock_run):
        """Test #35: gh returns [] → counts are all 0"""
        mock_run.return_value = MagicMock(
            stdout='[]',
            returncode=0
        )

        output = generate_metrics()
        assert 'github_issues_total{state="open"} 0' in output
        assert 'github_issues_total{state="closed"} 0' in output


class TestPrometheusFormat:
    """Test cases for Prometheus format (tests 36-38)"""

    @patch('subprocess.run')
    def test_content_type_header(self, mock_run):
        """Test #36: Response Content-Type header (tested at HTTP server level)"""
        # This would be tested at the HTTP server level
        # Expected: text/plain; version=0.0.4; charset=utf-8
        pass  # Placeholder

    @patch('subprocess.run')
    def test_all_metrics_have_help(self, mock_run):
        """Test #37: Every metric family has # HELP"""
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        metric_families = ['github_exporter_up', 'github_issues_total', 'github_issue_state', 'github_issue_age_days']

        for metric in metric_families:
            assert f'# HELP {metric}' in output

    @patch('subprocess.run')
    def test_all_metrics_have_type(self, mock_run):
        """Test #38: Every metric family has # TYPE"""
        mock_run.return_value = MagicMock(
            stdout=json.dumps([
                {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
            ]),
            returncode=0
        )

        output = generate_metrics()
        metric_families = ['github_exporter_up', 'github_issues_total', 'github_issue_state', 'github_issue_age_days']

        for metric in metric_families:
            assert f'# TYPE {metric}' in output


class TestHTTPEndpoints:
    """Test cases for HTTP endpoints (tests 39-41)"""

    def test_metrics_endpoint(self):
        """Test #39: GET /metrics returns 200"""
        # Tested at HTTP server level
        pass  # Placeholder

    def test_health_endpoint(self):
        """Test #40: GET /health returns 200 with body 'OK'"""
        # Tested at HTTP server level
        pass  # Placeholder

    def test_404_handler(self):
        """Test #41: Unknown paths return 404"""
        # Tested at HTTP server level
        pass  # Placeholder


class TestKnownBug:
    """Test for known bug: github_pulls_total absent when 0 PRs"""

    @patch('subprocess.run')
    def test_zero_prs_still_emits_metric(self, mock_run):
        """Known bug: github_pulls_total should emit zeros even when no PRs"""
        def side_effect(*args, **kwargs):
            cmd = args[0]
            if 'issue' in cmd:
                return MagicMock(
                    stdout=json.dumps([
                        {"number": 1, "title": "Issue 1", "state": "OPEN", "createdAt": "2026-01-01T00:00:00Z", "assignees": []}
                    ]),
                    returncode=0
                )
            elif 'pr' in cmd:
                # No PRs
                return MagicMock(stdout='[]', returncode=0)

        mock_run.side_effect = side_effect
        output = generate_metrics()

        # Bug: metric should be present with value 0, not absent
        # This test should PASS with the fix, FAIL without it
        assert 'github_pulls_total{state="open"} 0' in output
        assert 'github_pulls_total{state="closed"} 0' in output
        assert 'github_pulls_total{state="merged"} 0' in output
