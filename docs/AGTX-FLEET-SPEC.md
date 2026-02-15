# agtx Fleet Orchestration Spec — Server Manager

> Spec for adapting [agtx](https://github.com/fynnfluegge/agtx) as the PDN fleet management TUI.
> Target: Mac Mini server-manager running 12 Claude agents (01-12).

## What is agtx?

A **Rust TUI** (Ratatui + Crossterm) that provides Kanban-style task management for Claude Code:
- **Kanban columns**: Backlog → Planning → Running → Review → Done
- **Git worktree isolation**: Each task gets its own worktree + branch
- **tmux integration**: Each task runs in its own tmux window
- **Claude Code integration**: Auto-spawns `claude --dangerously-skip-permissions`, supports session resume
- **PR workflow**: AI-generated descriptions, `gh pr create` from TUI
- **Multi-project dashboard**: Sidebar for switching between repos
- **SQLite backend**: Global index + per-project databases
- **Config**: TOML format (`~/.config/agtx/config.toml` + `.agtx/config.toml`)

## Architecture Summary

```
┌─────────────────────────────────────────────┐
│                 agtx TUI                     │
│  ┌──────────┐ ┌────────┐ ┌───────┐ ┌──────┐│
│  │ Backlog  │ │Planning│ │Running│ │Review││
│  │          │ │ Claude │ │ Claude│ │  PR  ││
│  │          │ │planning│ │coding │ │review││
│  └──────────┘ └────────┘ └───────┘ └──────┘│
│        ↕ SQLite DB    ↕ tmux sessions       │
│        ↕ git worktrees  ↕ gh CLI            │
└─────────────────────────────────────────────┘
```

**Runtime deps**: Rust binary, tmux, git, gh CLI, claude CLI
**Install**: `cargo build --release` or `curl -fsSL .../install.sh | bash`

## Key Integration Points for PDN Fleet

### Current agtx (Single User)
- One user, one project at a time
- One Claude session per task
- Linear task flow (no pipeline stages)

### What We Need (12-Agent Fleet)
- 12 concurrent agents with role-based pipeline
- Centralized orchestration with fleet state visibility
- Conflict-free parallel git operations
- Grafana-style monitoring (agent health, task progress)

## Proposed Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    Mac Mini Server Manager                 │
│                                                           │
│  ┌─────────────────────────────────────────────────────┐ │
│  │              Fleet Orchestrator (agtx-fleet)         │ │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐            │ │
│  │  │ Pipeline │ │ Task     │ │ Health   │            │ │
│  │  │ Manager  │ │ Queue    │ │ Monitor  │            │ │
│  │  └────┬─────┘ └────┬─────┘ └────┬─────┘            │ │
│  │       │             │             │                  │ │
│  │  ┌────▼─────────────▼─────────────▼─────┐           │ │
│  │  │         Fleet State DB (SQLite)       │           │ │
│  │  │  agents | tasks | assignments | logs  │           │ │
│  │  └──────────────────────────────────────┘           │ │
│  └──────────────────┬──────────────────────────────────┘ │
│                      │                                    │
│  ┌──────────────────▼──────────────────────────────────┐ │
│  │              tmux Server Pool                        │ │
│  │                                                      │ │
│  │  agtx-01 ─── Agent 01 (Architect)     ← YOU         │ │
│  │  agtx-02 ─── Agent 02 (Senior Dev)                  │ │
│  │  agtx-03 ─── Agent 03 (Reviewer)                    │ │
│  │  agtx-04 ─── Agent 04 (Reviewer)                    │ │
│  │  agtx-05 ─── Agent 05 (Reviewer)                    │ │
│  │  agtx-06 ─── Agent 06 (Junior Dev)                  │ │
│  │  agtx-07 ─── Agent 07 (Junior Dev)                  │ │
│  │  agtx-08 ─── Agent 08 (Junior Dev)                  │ │
│  │  agtx-09 ─── Agent 09 (Bug Detective)               │ │
│  │  agtx-10 ─── Agent 10 (Bug Detective)               │ │
│  │  agtx-11 ─── Agent 11 (Bug Detective)               │ │
│  │  agtx-12 ─── Agent 12 (Bug Detective)               │ │
│  │                                                      │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                           │
│  ┌──────────────────────────────────────────────────────┐ │
│  │              Fleet Dashboard TUI                      │ │
│  │  ┌────────────┐ ┌────────────┐ ┌──────────────────┐ │ │
│  │  │ Agent Grid │ │ Task Board │ │ Pipeline View    │ │ │
│  │  │ 12 agents  │ │ Kanban     │ │ Bug→Senior→     │ │ │
│  │  │ with status│ │ all tasks  │ │ Review→Junior   │ │ │
│  │  └────────────┘ └────────────┘ └──────────────────┘ │ │
│  └──────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

## Implementation Plan

### Phase 1: Install agtx on Mac Mini (Day 1)
```bash
# Install Rust if not present
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Clone and build agtx
git clone https://github.com/fynnfluegge/agtx.git /opt/agtx
cd /opt/agtx && cargo build --release
cp target/release/agtx /usr/local/bin/

# Configure for PDN project
mkdir -p ~/.config/agtx
cat > ~/.config/agtx/config.toml << 'EOF'
default_agent = "claude"

[worktree]
enabled = true
auto_cleanup = true
base_branch = "main"
EOF

# Initialize PDN project
cd /path/to/pdn && agtx
```

### Phase 2: Fleet Wrapper (Week 1)
Build a thin orchestration layer around agtx:

```toml
# fleet-config.toml
[fleet]
project = "FinallyEve/pdn"
repo_path = "/home/pdn"

[[agents]]
id = "01"
role = "architect"
vm = "192.168.1.182"
ssh_user = "agent"

[[agents]]
id = "02"
role = "senior"
vm = "192.168.1.183"
ssh_user = "agent"

# ... agents 03-12 ...

[pipeline]
stages = [
    { name = "triage", roles = ["bug-detective"] },
    { name = "design", roles = ["senior", "architect"] },
    { name = "review", roles = ["reviewer"] },
    { name = "implement", roles = ["junior"] },
]
```

### Phase 3: Fleet Dashboard (Week 2-3)
Fork agtx and add:
1. **Agent status grid** — 12 cells showing each agent's current state
2. **Pipeline view** — tasks flowing through Bug Detective → Senior → Review → Junior
3. **SSH tunnel integration** — connect to agent VMs to check tmux sessions
4. **Issue sync** — pull GitHub issues as Backlog tasks

### Phase 4: Grafana Replacement (Week 3-4)
The fleet dashboard TUI replaces Grafana for real-time monitoring:
- Agent health (heartbeat from each VM)
- Task throughput (tasks completed per hour)
- Pipeline bottlenecks (where tasks pile up)
- Build status (pio build results per agent)

## Database Schema Extension

```sql
-- Extend agtx's existing tasks table
ALTER TABLE tasks ADD COLUMN assigned_agent TEXT;
ALTER TABLE tasks ADD COLUMN pipeline_stage TEXT;
ALTER TABLE tasks ADD COLUMN priority INTEGER DEFAULT 0;
ALTER TABLE tasks ADD COLUMN github_issue INTEGER;

-- New fleet tables
CREATE TABLE agents (
    id TEXT PRIMARY KEY,
    role TEXT NOT NULL,
    vm_host TEXT,
    status TEXT DEFAULT 'idle',
    last_heartbeat DATETIME,
    current_task_id TEXT
);

CREATE TABLE pipeline_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    task_id TEXT,
    from_stage TEXT,
    to_stage TEXT,
    agent_id TEXT,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

## Git Workflow for Fleet

```
main (protected)
├── task/agent-09/bug-142-analysis     ← Bug Detective finds issue
├── task/agent-02/fix-142-design       ← Senior designs fix
├── task/agent-06/fix-142-implement    ← Junior implements
└── task/agent-03/fix-142-review       ← Reviewer creates PR
```

Each agent works on isolated branches. Merge conflicts resolved by:
1. Auto-rebase on latest main before PR
2. Conflict detection in orchestrator
3. Human intervention for complex conflicts

## Immediate Next Steps

1. **Get Mac Mini back online** — it's currently unreachable at 192.168.1.91
2. **Install agtx** on Mac Mini (Phase 1 above)
3. **Create fleet-config.toml** with all 12 agent VMs
4. **Test single-project workflow** with agtx on FinallyEve/pdn
5. **Start Phase 2** fleet wrapper development

---

*Generated by Agent 01 — 2026-02-15*
