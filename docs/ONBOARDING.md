# PDN Hackathon Onboarding Guide

Welcome to the PDN (Portable Data Node) hackathon! This guide will get you contributing code in under 10 minutes.

## What is PDN?

PDN is an **ESP32-S3 embedded game device** for immersive LARP (Live Action Role Playing) experiences. It features an OLED display, RGB LEDs, haptic feedback, buttons, and wireless communication. The device runs 7 minigames where players connect via serial cables or wireless ESP-NOW to compete in real-time challenges.

## Architecture at a Glance

### State Machine Pattern
All game logic uses state machines with a 3-phase lifecycle:

```
          onStateMounted()
                 ↓
    ┌──────> onStateLoop() ────────┐
    │            ↓                  │
    └──── (check transitions) ──────┘
                 ↓
          onStateDismounted()
                 ↓
            (next state)
```

- **onStateMounted()** - Setup, called once when entering state
- **onStateLoop()** - Main logic, called repeatedly every frame
- **onStateDismounted()** - Cleanup, called once when leaving state

### Driver Pattern (Hardware Abstraction)
All hardware access goes through driver interfaces:

```
Device (abstract)
    ↓
PDN (concrete)
    ↓
Drivers (platform-specific)
    ├── ESP32-S3 drivers (real hardware)
    └── Native drivers (CLI simulator)
```

This enables:
- **CLI simulator** for development without hardware
- **Unit tests** that run in CI
- **Easy porting** to new platforms

## Quick Setup

### 1. Clone the Repository

```bash
git clone https://github.com/AlleycatAssetAcquisitions/pdn.git
cd pdn
```

### 2. Install PlatformIO

```bash
# Install via pip
pip install platformio

# Or upgrade if already installed
platformio upgrade
```

### 3. Build and Test

```bash
# Build the CLI simulator (no hardware needed!)
pio run -e native_cli

# Run all tests (CRITICAL - always run before commits)
pio test -e native           # Core unit tests
pio test -e native_cli_test  # CLI integration tests
```

**Build Environments:**

| Environment | Purpose | Command |
|------------|---------|---------|
| `native_cli` | CLI simulator **build** | `pio run -e native_cli` |
| `native` | Core unit tests | `pio test -e native` |
| `native_cli_test` | CLI integration tests | `pio test -e native_cli_test` |
| `esp32-s3_debug` | Hardware dev build | `pio run -e esp32-s3_debug` |
| `esp32-s3_release` | Hardware production | `pio run -e esp32-s3_release` |

**IMPORTANT:** Do NOT run `pio test -e native_cli` - it will fail because `native_cli` is a build environment, not a test environment.

## Run the Simulator

The CLI simulator lets you test without hardware. You can spawn multiple devices and simulate serial cable connections between them.

```bash
# Start simulator with 2 devices
.pio/build/native_cli/program 2
```

### Example Session

```
$ .pio/build/native_cli/program 2
PDN CLI Simulator
Type 'help' for commands, 'quit' to exit

> list
Device 0: Hunter (idle)
Device 1: Bounty (idle)

> display on
Display mirror and captions enabled

> cable 0 1
Connected device 0 to device 1 via serial cable

> state
Device 0: FdnDetectedState
Device 1: FdnListeningState

> b 0
[Device 0] Primary button pressed
(Watch the state transitions happen!)

> progress 0
Konami Progress Grid:
  ↑ ↓ ← →
  B A START
  Status: 2/7 buttons unlocked

> quit
```

## Run Demo Scripts

The quickest way to see PDN in action is to run the demo scripts. They automate the CLI simulator to showcase gameplay.

### Available Demos

| Script | Duration | What It Shows |
|--------|----------|---------------|
| `demo-quick.sh` | 60s | Device pairing, one minigame, Konami unlock |
| `demo-full-journey.sh` | 3-4 min | Complete player experience, stats, progression |
| `demo-all-games.sh` | 5-7 min | All 7 minigames, button mapping |
| `demo-konami-unlock.sh` | 8-10 min | Full progression, all 7 buttons, boon unlock |
| `demo-pvp-battle.sh` | 3-4 min | Multiplayer ESP-NOW duels, win tracking |

### Run a Demo

```bash
# Make executable (first time only)
chmod +x scripts/*.sh

# Run quick demo (60 seconds)
./scripts/demo-quick.sh

# Run at double speed
DEMO_SPEED=2 ./scripts/demo-quick.sh

# Run at half speed (slower, more readable)
DEMO_SPEED=0.5 ./scripts/demo-full-journey.sh
```

## CLI Commands Reference

Once in the CLI simulator, these commands are available:

### Device Management
| Command | Description |
|---------|-------------|
| `list` | List all devices and their states |
| `add hunter` | Add a hunter device |
| `add bounty` | Add a bounty device |
| `add npc <game>` | Add NPC for specific game (e.g., `ghost-runner`) |
| `role all` | Show all device roles |
| `reboot <id>` | Restart device |

### Connections
| Command | Description |
|---------|-------------|
| `cable <id1> <id2>` | Connect two devices via serial cable |
| `cable -l` | List active cable connections |
| `cable -d <id1> <id2>` | Disconnect cable between devices |
| `peer <src> <dst> <type>` | Send ESP-NOW packet (type: 0-5) |

### Button Input
| Command | Description |
|---------|-------------|
| `b <id>` | Primary button click on device |
| `l <id>` | Primary button long-press on device |
| `b2 <id>` | Secondary button click on device |
| `l2 <id>` | Secondary button long-press on device |
| `press <id> <button>` | Press button by name (primary/secondary) |

### Visualization
| Command | Description |
|---------|-------------|
| `display on` | Enable display mirror + captions |
| `display off` | Disable display mirror + captions |
| `mirror on` | Enable display mirror only (braille rendering) |
| `mirror off` | Disable display mirror |
| `captions on` | Enable captions only |
| `captions off` | Disable captions |

### Information
| Command | Description |
|---------|-------------|
| `state [id]` | Show device state (all if no id) |
| `stats <id>` | Show player statistics |
| `progress <id>` | Show Konami progress grid |
| `colors <id>` | Show color profile status |
| `games` | List all available games |
| `help` | Show command list |
| `help2` | Show detailed command help |

### Advanced
| Command | Description |
|---------|-------------|
| `konami <id> <bitmask>` | Set Konami progress (testing) |
| `difficulty <id> <level>` | Set difficulty (0=easy, 1=hard) |
| `quit` | Exit simulator |

## Contributing

### Branch Naming

```
vm-{NN}/{type}/{issue}/{name}
```

Examples:
- `vm-03/feature/26/reencounter-prompt`
- `vm-07/fix/42/timing-bug`

### Workflow

1. **Create your branch from main**
   ```bash
   git checkout main
   git pull origin main
   git checkout -b vm-XX/feature/NN/description
   ```

2. **Make your changes**
   - Edit code in `include/` and `src/`
   - Add tests in `test/test_core/` or `test/test_cli/`

3. **Test before committing**
   ```bash
   # Run all tests
   pio test -e native
   pio test -e native_cli_test

   # Build CLI simulator
   pio run -e native_cli

   # Test manually in CLI
   .pio/build/native_cli/program 2
   ```

4. **Commit and push**
   ```bash
   git add <files>
   git commit --no-verify -m "Brief description (#issue)"
   git push origin vm-XX/feature/NN/description
   ```

5. **Create PR**
   ```bash
   gh pr create --repo FinallyEve/pdn \
     --title "Brief description (#issue)" \
     --body "Fixes #issue. Detailed description." \
     --base main
   ```

### Commit Message Format

```
Brief description (#PR_NUMBER)

Examples:
PDN CLI Tool (#62)
Driver Update Bug Fixes (#61)
Feature/unicast quickdraw packets (#55)
```

**Rules:**
- **No co-authored-by lines** in commit messages
- Each commit should include **implementation + tests together**
- Use `git commit --no-verify` (pre-commit hook has known issues)

### Test Requirements

**CRITICAL:** All PRs must pass CI tests.

1. **New features MUST have tests**
   - Core tests in `test/test_core/`
   - CLI tests in `test/test_cli/`

2. **Run tests before every commit**
   ```bash
   pio test -e native && pio test -e native_cli_test
   ```

3. **Hardware changes require device testing** (not just CLI)

## Shared Files (Coordination Required)

These files are shared across multiple features. If you need to modify them, coordinate with other contributors:

- `include/cli/cli-device.hpp`
- `include/device/device-types.hpp`
- `src/game/quickdraw-states/fdn-detected-state.cpp`
- `src/game/quickdraw.cpp`
- `include/game/color-profiles.hpp`

## Directory Structure

```
pdn/
├── include/
│   ├── device/              # Hardware abstraction layer
│   │   ├── drivers/
│   │   │   ├── esp32-s3/    # Real hardware drivers
│   │   │   └── native/      # CLI simulator drivers
│   │   └── pdn.hpp
│   ├── game/                # Game logic
│   │   ├── quickdraw.hpp
│   │   ├── fdn-game.hpp
│   │   ├── signal-echo/
│   │   ├── ghost-runner/
│   │   └── ...              # Other minigames
│   ├── state/               # State machine core
│   │   ├── state-machine.hpp
│   │   └── state.hpp
│   ├── cli/                 # CLI simulator
│   └── utils/               # Utilities (timers, logger)
├── src/                     # Implementation files
│   ├── main.cpp             # ESP32 entry point
│   ├── cli-main.cpp         # CLI entry point
│   ├── device/
│   ├── game/
│   └── state/
├── test/
│   ├── test_core/           # Core unit tests (GoogleTest)
│   └── test_cli/            # CLI integration tests
├── scripts/                 # Demo scripts
├── docs/                    # Documentation
└── platformio.ini           # Build configuration
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
- **Comments**: Use `//` for single-line, `/* */` for multi-line blocks

Example:
```cpp
class MyState : public State {
public:
    MyState(StateMachine* parent) : State(STATE_ID) {
        // Constructor body
    }

    void onStateMounted(Device* PDN) override {
        // Setup code
    }

    void onStateLoop(Device* PDN) override {
        // Main logic
    }

private:
    int score = 0;
    bool gameComplete = false;
};
```

## Getting Help

- **Discord**: https://discord.gg/6XGTCbKkUy
- **GitHub Issues**: https://github.com/alleycatassetacquisitions/pdn/issues
- **Email**: alleycatassetacquisitions@gmail.com
- **In CLI**: Type `help` or `help2` for command reference

## Useful Aliases (Optional)

Add these to your `~/.bashrc` or `~/.zshrc`:

```bash
alias akwsp='cd ~/pdn'                          # Jump to project
alias pdntest='pio test -e native && pio test -e native_cli_test'
alias pdnbuild='pio run -e esp32-s3_release'
alias pdndebug='pio run -e esp32-s3_debug'
alias pdncli='pio run -e native_cli && .pio/build/native_cli/program 2'
alias pdnflash='pio run -e esp32-s3_debug --target upload'
alias pdnmonitor='pio device monitor'
```

## Next Steps

1. **Run a demo** to see PDN in action: `./scripts/demo-quick.sh`
2. **Read the docs** for deeper understanding:
   - [ARCHITECTURE.md](ARCHITECTURE.md) - System architecture
   - [GAME-MECHANICS.md](GAME-MECHANICS.md) - Gameplay rules
   - [API.md](API.md) - Complete API reference
   - [CLAUDE.md](../CLAUDE.md) - Full developer guide
3. **Pick a GitHub issue** and start hacking!
4. **Join Discord** to chat with other contributors

## Troubleshooting

### Build Fails

```bash
# Clean and rebuild
pio run -e native_cli --target clean
pio run -e native_cli

# Update PlatformIO
pio upgrade
```

### Tests Fail

```bash
# Run tests with verbose output
pio test -e native -v
pio test -e native_cli_test -v
```

### Simulator Won't Start

```bash
# Kill any hung processes
pkill -9 program
rm -f /tmp/fifo*

# Rebuild and run
pio run -e native_cli
.pio/build/native_cli/program 2
```

### "pio: command not found"

```bash
# Add PlatformIO to PATH
export PATH=$PATH:~/.platformio/penv/bin

# Or use full path
~/.platformio/penv/bin/platformio run -e native_cli
```

---

**Ready to contribute?** Pick an issue, create a branch, write code + tests, and submit a PR. Welcome to the PDN community!

**Last Updated:** 2026-02-14
