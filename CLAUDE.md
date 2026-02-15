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

### Aliases Available

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

### Development Helper Script

Use `./dev.sh` or `pdn` alias for common tasks:

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

### Test File Convention (CLI tests)

CLI tests use a **split pattern** — test logic lives in `.hpp` headers, TEST_F registrations live in per-domain `.cpp` files:

- **Test functions** go in header files: `test/test_cli/ghost-runner-tests.hpp`, `signal-echo-tests.hpp`, etc.
- **TEST_F registrations** go in per-domain `.cpp` files in `test/test_cli/`

Each domain has its own registration file:

| Registration File | Domain |
|---|---|
| `cli-tests-main.cpp` | `main()` entry point (gtest/gmock) |
| `cli-core-tests.cpp` | Broker, HTTP, Serial, Peer, Button, Light, Display, Command, Role, Reboot |
| `fdn-protocol-tests.cpp` | FDN Protocol, FDN Game, CLI FDN Device, Device Extensions |
| `signal-echo-tests.cpp` | Signal Echo gameplay + difficulty |
| `fdn-integration-tests.cpp` | Integration, Complete, Progress, Challenge, Switching, Color Profile |
| `firewall-decrypt-tests.cpp` | Firewall Decrypt (all sections) |
| `progression-e2e-tests.cpp` | E2E, Re-encounter, Color Context, Bounty Role |
| `ghost-runner-reg-tests.cpp` | Ghost Runner minigame |
| `spike-vector-reg-tests.cpp` | Spike Vector minigame |
| `cipher-path-reg-tests.cpp` | Cipher Path minigame |
| `exploit-seq-reg-tests.cpp` | Exploit Sequencer minigame |
| `breach-defense-reg-tests.cpp` | Breach Defense minigame |

When adding a new test, add entries to **both** the `.hpp` header and the correct `.cpp` registration file:

```cpp
// 1. In ghost-runner-tests.hpp — the test function:
void myNewTestFunction(GhostRunnerTestSuite* suite) {
    // test logic here
}

// 2. In ghost-runner-reg-tests.cpp — the TEST_F registration:
TEST_F(GhostRunnerTestSuite, MyNewTest) {
    myNewTestFunction(this);
}
```

**Important:** Add TEST_F macros to YOUR game's registration file only. Do not edit other games' files.

### Test Structure
```cpp
// GoogleTest framework
TEST_F(TestSuiteName, testDescription) {
    // Arrange
    // Act
    // Assert
}
```

## Git Workflow

### Branch Naming
```
feature/description   # New features
fix/bug-description   # Bug fixes
```

### Commit Messages
```
Brief description (#PR_NUMBER)

# Examples from recent PRs:
PDN CLI Tool (#62)
Driver Update Bug Fixes (#61)
Feature/unicast quickdraw packets (#55)
```

**Rules:**
- **No co-authored-by lines** in commit messages
- Each commit should encompass **one feature + its tests** — don't separate implementation from tests
- Use `git commit --no-verify` (see pre-commit hook known issue above)

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

**WiFi Credentials**
- Never commit credentials to git
- Use `wifi_credentials.ini` (in `.gitignore`)
- Template is `wifi_credentials.ini.example`
- PlatformIO injects at build time

**Sensitive Files** (already in `.gitignore`)
```
wifi_credentials.ini
.env
*.log
compile_commands.json
.pio/
.vscode/
.idea/
.cursor/
```

## Key Dependencies

**ESP32-S3 Libraries**:
- U8g2 (OLED display)
- FastLED (LED strips)
- OneButton (button handling)
- ArduinoJson (JSON serialization)
- WiFi (networking)

**Testing**:
- GoogleTest + GMock

## CLI Simulator

The native CLI tool simulates PDN devices without hardware:

```bash
pdncli 2  # Spawn 2 devices (hunter + bounty)

# In simulator:
cable 0 1              # Connect devices via serial
press 0 primary        # Simulate button press
state                  # View all device states
help                   # All commands
```

**Features**:
- Multi-device simulation
- Serial cable connections
- ESP-NOW peer communication
- Mock HTTP server
- Real-time state visualization

## Common Tasks

### Adding a New State

1. Create state class in `include/game/quickdraw-states/`
2. Implement lifecycle methods:
   - `onStateMounted()` - Setup
   - `onStateLoop()` - Logic
   - `onStateDismounted()` - Cleanup
3. Define transition conditions
4. Register message types
5. Add to state machine initialization
6. **Write tests** in `test/test_core/`

### Adding a New Driver

1. Define interface in `include/device/drivers/`
2. Implement for ESP32-S3 in `drivers/esp32-s3/`
3. Implement for native in `drivers/native/`
4. Register in PDN device class
5. **Test on both CLI and hardware**

**Native drivers must match hardware behavior exactly.** The native driver is a test double for the real ESP32 driver — if it takes shortcuts or only implements the common-case path, tests will pass but the CLI simulator will behave differently from hardware. For example, U8g2's `drawXBMP()` writes both ON and OFF pixels; the native equivalent must do the same.

### Debugging

**CLI Simulator**:
- Fastest iteration
- Full visibility into state
- No hardware required

**Serial Monitor**:
```bash
pdnmonitor  # or: pio device monitor
```

**Debug Build**:
```bash
pdndebug  # Verbose logging enabled
```

## Community

- **Discord**: https://discord.gg/6XGTCbKkUy
- **GitHub**: https://github.com/alleycatassetacquisitions/pdn
- **Email**: alleycatassetacquisitions@gmail.com

## Automation & Tooling

### Pre-commit Hook
Automatically runs before every commit:
- Runs all unit tests (native + CLI)
- Blocks commits if tests fail
- Prevents accidentally committing `wifi_credentials.ini`

**Known issue**: The pre-commit hook currently fails due to environment misconfiguration — the `native` env hits a pre-existing SIGABRT and the hook tries `native_cli` (build env) instead of `native_cli_test` (test env). Use `--no-verify` to bypass, but always run `pio test -e native_cli_test` manually before committing.

### Code Formatting
- `.clang-format` - C++ formatting rules (4 spaces, K&R style)
- `.editorconfig` - Universal editor settings
- Format file: `clang-format -i <file>`

### Development Script
`./dev.sh` provides quick access to common tasks:
```bash
./dev.sh test         # All tests
./dev.sh build        # Release build
./dev.sh sim 3        # Simulator with 3 devices
./dev.sh full-check   # Complete verification
```

## Plan Documents

When creating a PR that implements a planned feature, write a plan document to:

```
~/Documents/code/alleycat/plans/pdn/<branch-name>.md
```

The plan should be a **reproducible implementation guide** — someone should be able to re-create the feature from the plan alone and get a nearly identical result. Include:
- **Problem** — what's broken or missing, with root cause analysis and before/after code snippets
- **Solution** — step-by-step changes with exact code snippets showing the fix
- **Files Changed** — table mapping each file to its change and which commit it belongs to
- **Commits** — the commit breakdown with files, tests, and splitting strategy for shared files
- **Tests** — full test function source code, not just names
- **Verification** — exact commands to run and what to check manually
- **Design Decisions** — why we chose this approach over alternatives
- **Lessons Learned** — what we discovered during implementation that wasn't obvious from the start

Update the plan to reflect what actually happened during implementation (not just the original proposal).

## Agent Workflow

When implementing features through the agent pipeline, follow the workflow defined at:

```
~/Documents/code/alleycat/plans/pdn/workflow.md
```

**All phases are mandatory.** The orchestrator must follow the checklist at the top of the workflow doc in order. No skipping phases, no combining phases, no rationalizing that a phase is "already done." Each phase exists to produce a specific artifact that the next phase depends on.

Key rules:
- **Never merge to main.** Only @FinallyEve merges after human review.
- **One GitHub issue per feature.** Paste the plan markdown inline in the issue body (use `<details>` for long plans). Never reference local file paths in issues. To embed a plan file, read it into a shell variable first (`PLAN=$(cat plan.md)`) and interpolate with `"${PLAN}"` — never use `$(cat ...)` inside single-quoted heredocs, which prevents expansion.
- **Plan docs are not code.** They live locally and in issue descriptions — never commit them to git.
- **The plan doc is the spec.** Each role adds their section as they complete their phase.
- **Agents stay in their lane.** Junior devs don't make design decisions. Testing agents don't read implementation files. Senior devs don't merge.
- **PR review flow:** Mark PR ready for review (`gh pr ready`), assign @FinallyEve (`gh pr edit --add-assignee`), add `review-requested` label. Do NOT use `--add-reviewer` (author can't review own PR).
- **Session checkpoints:** After completing a phase gate, suggest a checkpoint: **"Checkpoint: [phase name]. Good point to reset."** Write state to `~/Documents/code/alleycat/plans/pdn/.checkpoint` (JSON with feature, phase, issue/PR numbers, branch, plan path). Fresh sessions resume by reading the checkpoint + plan doc. The plan doc is the brain; the session is the scratchpad.

## Multi-VM Agent Conventions

### Your Identity
You are running on VM `claude-agent-XX`. Your VM number is in `$VM_ID`.

### Branch Naming
```
vm-{NN}/{type}/{issue}/{name}
```
Examples: `vm-03/feature/26/reencounter-prompt`, `vm-07/fix/42/timing`

### What You Can Do
- Create branches from main, push to origin (FinallyEve/pdn)
- Run tests: `pio test -e native && pio test -e native_cli_test`
- Build simulator: `pio run -e native_cli`

### What You Cannot Do
- Merge to main (orchestrator/human only)
- Create GitHub issues or PRs (orchestrator handles this)
- Push to upstream (alleycatassetacquisitions/pdn)
- Edit shared files unless explicitly assigned

### Shared Files (Do Not Touch Unless Assigned)
- `include/cli/cli-device.hpp`
- `include/device/device-types.hpp`
- `src/game/quickdraw-states/fdn-detected-state.cpp`
- `src/game/quickdraw.cpp`
- `include/game/color-profiles.hpp`

### Test Registration
Each game has its own test `.cpp` file. Add TEST_F macros to YOUR game's
file only, not anyone else's.

## Context Resilience — MANDATORY

You WILL hit context limits. When Claude Code compacts your context, you lose detailed memory of earlier work. This section prevents you from going off the rails after compaction.

### On Every Session Start
```bash
# FIRST thing you do — check for existing checkpoint
if [ -f ~/pdn/.agent-checkpoint.json ]; then
    cat ~/pdn/.agent-checkpoint.json
fi
# THEN re-read your plan
cat ~/plan.md
```

If `.agent-checkpoint.json` exists, you are **resuming mid-task**. Read the checkpoint's `progress` field and `step_log` array to understand where you left off. Do NOT restart from scratch.

### Checkpoint After Every Major Step

Call this after completing each significant action:

```bash
~/agent-checkpoint.sh "Completed: <what you just did>. Next: <what's next>"
```

**When to checkpoint** (non-negotiable):
- After reading and understanding your plan
- After each file you create or modify
- Before running the test suite
- After tests pass or fail (include pass/fail in message)
- After committing code
- After pushing to remote
- After creating a PR

**Example checkpoint flow:**
```bash
~/agent-checkpoint.sh "Read plan. Task: add FDN intro tests. Files to create: 2 test headers, 1 reg file"
~/agent-checkpoint.sh --phase "implementing"
# ... write code ...
~/agent-checkpoint.sh "Created test/test_cli/fdn-intro-tests.hpp with 5 test functions"
~/agent-checkpoint.sh "Created test/test_cli/fdn-intro-reg-tests.cpp with TEST_F registrations"
# ... before testing ...
~/agent-checkpoint.sh "Implementation complete. Running tests next"
~/agent-checkpoint.sh --phase "testing"
# ... run tests ...
~/agent-checkpoint.sh "Tests: 265/265 passed. Ready to commit"
~/agent-checkpoint.sh --phase "committing"
# ... commit, push, PR ...
~/agent-checkpoint.sh --done "PR created: #215"
```

### Recovery Protocol

If you find yourself confused about what you're working on, or if you just recovered from context compaction:

1. **STOP.** Do not continue blindly.
2. Read `~/pdn/.agent-checkpoint.json` — it has your last known state
3. Read `~/plan.md` — it has your full task specification
4. Check `git log --oneline -5` and `git status` — see what's already done
5. Resume from where the checkpoint says you left off

### What NOT To Do After Compaction
- Do NOT start the task over from scratch
- Do NOT re-read files you've already modified (check git diff instead)
- Do NOT create duplicate test files or implementations
- Do NOT commit work that's already been committed (check git log)

### Process Hygiene
- Kill test processes after they complete: `pkill -f 'pio\|program' 2>/dev/null || true`
- If tests hang for >5 minutes, kill them and note the failure in your checkpoint
- Do not leave background processes running when your task is done

## Tips for Claude

- **Pre-commit hook active** - Tests run automatically before commits
- **Always read existing states** before creating new ones - follow established patterns
- **Prefer editing over creating** - build on existing code
- **Run tests frequently** - catch issues early (`./dev.sh test`)
- **Use CLI simulator** for rapid iteration (`./dev.sh sim 2`)
- **Check recent PRs** for current development patterns
- **Hardware changes need device testing** - don't rely solely on CLI
- **Security**: Never expose WiFi credentials, API keys, or tokens
- **Comments**: Add them for complex state machine logic and lifecycle explanations
- **Code formatting**: Use `.clang-format` for consistent style

## Recent Changes

- FDN minigame gameplay systems: Ghost Runner, Spike Vector, Cipher Path, Exploit Sequencer, Breach Defense (PR #32)
- Image captions and opaque display rendering fix (PR #70, replaces #68)
- Display mirror with braille rendering, captions, and display commands (PR #67)
- AI developer wiki (PR #66)
- PioArduino and modern ESP-IDF libraries (PR #65)
- WiFi credentials moved to `.ini` file (PR #64)
- Documentation cleanup (PR #63)
- CLI simulator tool added (PR #62)

---

*Last Updated: 2026-02-13*
