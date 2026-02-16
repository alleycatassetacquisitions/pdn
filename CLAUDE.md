# PDN (Portable Data Node) - Development Guide for Claude

## Project Overview

The Alleycat Asset Acquisitions Portable Data Node is an **ESP32-S3 based embedded device** for immersive LARP gameplay. It features OLED display, LEDs, haptics, buttons, and wireless communication (ESP-NOW, WiFi, Serial).

**Current Game**: Neotropolis Quickdraw - a real-time face-to-face dueling system

## Architecture

### Core Patterns

**1. Driver Pattern (Hardware Abstraction)**
```
Device (abstract) → PDN (concrete) → Drivers (platform-specific)
├── ESP32-S3 drivers (real hardware)
└── Native drivers (CLI simulator)
```

All hardware access goes through driver interfaces. This enables:
- CLI simulator for hardware-less development
- Unit testing without physical devices
- Easy porting to new platforms

**2. State Machine Pattern**
```
StateMachine → States → Lifecycle
```

Game logic lives in states with three lifecycle methods:
- `onStateMounted()` - Setup (called once when entering state)
- `onStateLoop()` - Main logic (called repeatedly)
- `onStateDismounted()` - Cleanup (called once when leaving state)

States register valid message types for communication and define transition conditions.

### Directory Structure

```
include/
  device/          # Hardware abstraction layer
    drivers/       # Platform-specific implementations
      esp32-s3/    # Real hardware drivers
      native/      # CLI simulator drivers
  game/            # Game logic (Quickdraw)
  state/           # State machine core
  cli/             # CLI tool specifics
  utils/           # Utilities (timers, logger, etc.)
src/               # Implementation files (.cpp)
test/
  test_core/       # Core unit tests (GoogleTest)
  test_cli/        # CLI simulator tests
```

## Development Workflow

### Build Environments

```bash
# CLI Simulator (fastest, no hardware needed)
pio run -e native_cli
.pio/build/native_cli/program 2  # Spawn 2 devices

# Unit Tests (ALWAYS run before committing)
pio test -e native               # Core tests (test_core/)
pio test -e native_cli_test      # CLI tests (test_cli/)

# Hardware Builds
pio run -e esp32-s3_debug        # Development (verbose logs)
pio run -e esp32-s3_release      # Production (no logs)

# Flash to device
pio run -e esp32-s3_debug --target upload
pio device monitor               # View serial output
```

**IMPORTANT — Test environment names are confusing:**
| Environment | Purpose | Command |
|---|---|---|
| `native` | Core unit tests (`test_core/`) | `pio test -e native` |
| `native_cli_test` | CLI unit tests (`test_cli/`) | `pio test -e native_cli_test` |
| `native_cli` | Simulator **build** (NOT tests) | `pio run -e native_cli` |

Do NOT run `pio test -e native_cli` — it will fail because `native_cli` is a build-only environment.

### Aliases & Dev Script

```bash
akwsp          # cd to project directory
pdn            # Development helper script (./dev.sh)
pdntest        # Run all tests
pdnbuild       # Build release firmware
pdndebug       # Build debug firmware
pdncli         # Run CLI simulator
pdnflash       # Flash to device
pdnmonitor     # Serial monitor
```

```bash
./dev.sh test           # Run all tests
./dev.sh build          # Build release firmware
./dev.sh sim 2          # Run simulator with 2 devices
./dev.sh full-check     # Complete verification before commit
./dev.sh help           # See all commands
```

## Code Style

### Naming Conventions
- **Classes**: `PascalCase` (e.g., `Player`, `StateMachine`)
- **Methods**: `camelCase` (e.g., `onStateMounted()`, `getUserID()`)
- **Private members**: `camelCase`, no prefix (e.g., `id`, `winStreak`)
- **Enums**: `PascalCase` with `SCREAMING_CASE` values
- **Constants**: `SCREAMING_SNAKE_CASE`
- **Files**: `kebab-case` (e.g., `ghost-runner-tests.hpp`)

### Formatting
- **Indentation**: 4 spaces (no tabs)
- **Header guards**: `#pragma once` (not `#ifndef`)
- **Braces**: Opening brace on same line for functions
- **Initialization lists**: One per line, colon on constructor line

```cpp
Player::Player(const std::string& id, Allegiance allegiance, bool isHunter) :
  id(id),
  allegiance(allegiance),
  hunter(isHunter)
{
}
```

### Comments
```cpp
// Single-line for brief explanations

/*
 * Multi-line block comments for detailed docs
 * Explain lifecycle, purpose, architecture
 */
```

## Testing Requirements

**CRITICAL**: All PRs must pass CI tests

1. **New features MUST have tests** — core tests in `test/test_core/`, CLI tests in `test/test_cli/`
2. **Run tests before every commit**:
   ```bash
   pdntest  # or: pio test -e native && pio test -e native_cli_test
   ```
3. **Hardware changes require device testing** (not just CLI)

For test file conventions, registration file table, and code examples, see **`test/CLAUDE.md`**.

## Git Workflow

### Branch Naming
```
feature/description   # New features
fix/bug-description   # Bug fixes
vm-{NN}/{type}/{issue}/{name}  # Multi-VM agent branches
```

### Commit Messages
```
Brief description (#PR_NUMBER)
```

**Rules:**
- **No co-authored-by lines** in commit messages
- Each commit should encompass **one feature + its tests** — don't separate implementation from tests
- Use `git commit --no-verify` (pre-commit hook is broken — see Automation section)

### PR Process
1. Fork → Create branch → Develop
2. **Add tests** for new functionality
3. **Run test suite** - must pass
4. **Build for ESP32** - must succeed
5. Create draft PR with clear description, link related issues (`Fixes #N`)
6. When ready for human eyes: `gh pr ready` + `gh pr edit --add-assignee FinallyEve` + add `review-requested` label
7. Wait for CI checks to pass

**GitHub API rate limits:** Space out API calls. If you get a 429, wait 30+ seconds. Don't fire 5+ calls in rapid succession.

## Security Practices

- Never commit credentials to git
- Use `wifi_credentials.ini` (in `.gitignore`), template is `wifi_credentials.ini.example`
- Never expose WiFi credentials, API keys, or tokens

**Sensitive Files** (already in `.gitignore`): `wifi_credentials.ini`, `.env`, `*.log`, `compile_commands.json`, `.pio/`, `.vscode/`, `.idea/`, `.cursor/`

## Key Dependencies

**ESP32-S3**: U8g2 (OLED), FastLED (LEDs), OneButton (buttons), ArduinoJson, WiFi
**Testing**: GoogleTest + GMock

## CLI Simulator

```bash
pdncli 2  # Spawn 2 devices (hunter + bounty)

# In simulator:
cable 0 1              # Connect devices via serial
press 0 primary        # Simulate button press
state                  # View all device states
help                   # All commands
```

Features: multi-device simulation, serial cable connections, ESP-NOW peer communication, mock HTTP server, real-time state visualization.

## Common Tasks

### Adding a New State

1. Create state class in `include/game/quickdraw-states/`
2. Implement lifecycle methods: `onStateMounted()`, `onStateLoop()`, `onStateDismounted()`
3. Define transition conditions, register message types
4. Add to state machine initialization
5. **Write tests** in `test/test_core/`

### Adding a New Driver

1. Define interface in `include/device/drivers/`
2. Implement for ESP32-S3 in `drivers/esp32-s3/`
3. Implement for native in `drivers/native/`
4. Register in PDN device class
5. **Test on both CLI and hardware**

**Native drivers must match hardware behavior exactly.** The native driver is a test double for the real ESP32 driver — if it takes shortcuts, tests will pass but the CLI simulator will behave differently from hardware.

### Debugging

- **CLI Simulator**: Fastest iteration, full state visibility, no hardware required
- **Serial Monitor**: `pdnmonitor` (or `pio device monitor`)
- **Debug Build**: `pdndebug` (verbose logging enabled)

## Automation & Tooling

**Pre-commit Hook** — Runs tests before every commit. **Known issue**: Currently broken due to environment misconfiguration. Use `--no-verify` to bypass, but always run `pio test -e native_cli_test` manually before committing.

**Code Formatting** — `.clang-format` (4 spaces, K&R style), `.editorconfig`. Format: `clang-format -i <file>`

## Plan Documents

When creating a PR that implements a planned feature, write a plan document to `~/Documents/code/alleycat/plans/pdn/<branch-name>.md`. The plan should be a **reproducible implementation guide** — someone should be able to re-create the feature from the plan alone. Include: Problem, Solution, Files Changed, Commits, Tests (full source), Verification, Design Decisions, Lessons Learned.

## Agent Pipeline

**Full workflow spec**: `~/Documents/code/alleycat/plans/pdn/workflow.md` — all phases are mandatory, follow in order.

Key rules:
- **Never merge to main.** Only @FinallyEve merges after human review.
- **One GitHub issue per feature.** Paste plan markdown inline in the issue body.
- **PR review flow:** `gh pr ready` + `gh pr edit --add-assignee FinallyEve` + add `review-requested` label.
- **Session checkpoints:** Write state to `~/Documents/code/alleycat/plans/pdn/.checkpoint` after each phase gate.

### Multi-VM Conventions

- VM identity: `claude-agent-XX`, VM number in `$VM_ID`
- Branch naming: `vm-{NN}/{type}/{issue}/{name}`
- Can: create branches, push to origin (FinallyEve/pdn), run tests
- Cannot: merge to main, push to upstream, edit shared files unless assigned

**Shared Files (Do Not Touch Unless Assigned):** `include/cli/cli-device.hpp`, `include/device/device-types.hpp`, `src/game/quickdraw-states/fdn-detected-state.cpp`, `src/game/quickdraw.cpp`, `include/game/color-profiles.hpp`

### Context Resilience

On session start: check `~/pdn/.agent-checkpoint.json`, then re-read your plan. If checkpoint exists, you are **resuming mid-task** — do NOT restart from scratch.

Checkpoint after every major step: `~/agent-checkpoint.sh "Completed: <what>. Next: <what>"`. Checkpoint after: reading plan, each file modified, before/after tests, after commit, after push, after PR creation.

**Recovery protocol:** STOP → read checkpoint → read plan → check `git log`/`git status` → resume from where you left off. Do NOT re-create files, re-commit work, or start over.

**Process hygiene:** Kill test processes after completion: `pkill -f 'pio\|program' 2>/dev/null || true`

## Tips for Claude

- **Always read existing states** before creating new ones - follow established patterns
- **Prefer editing over creating** - build on existing code
- **Run tests frequently** - catch issues early
- **Use CLI simulator** for rapid iteration
- **Check recent PRs** for current development patterns
- **Hardware changes need device testing** - don't rely solely on CLI

## Community

- **Discord**: https://discord.gg/6XGTCbKkUy
- **GitHub**: https://github.com/alleycatassetacquisitions/pdn
- **Email**: alleycatassetacquisitions@gmail.com

---

*Last Updated: 2026-02-16*
