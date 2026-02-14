# Portable Data Node (PDN) by Alleycat Asset Acquisitions

![CI](https://github.com/FinallyEve/pdn/actions/workflows/pr-checks.yml/badge.svg)
![Tests](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/FinallyEve/5b8fff0b3af6332b80904301c8f159cf/raw/pdn-tests.json)

**An ESP32-S3 embedded game device designed for immersive face-to-face LARP gameplay.**

The PDN (Portable Data Node) is an interactive handheld device created for live-action role-playing events like [Neotropolis](https://www.neotropolis.com). Players connect devices via serial cables or wireless (ESP-NOW) to engage in real-time minigames, unlock progression systems, and participate in quickdraw duels. The PDN combines hardware (OLED display, LEDs, haptics, buttons) with sophisticated state machine architecture to deliver rich multiplayer experiences.

Watch Elli Furedy's [Hackaday Supercon 2025 talk](https://youtu.be/ndodsA254HA) for the design philosophy behind Alleycat Asset Acquisitions.

**[Developer Wiki](https://deepwiki.com/alleycatassetacquisitions/pdn)** | **[Discord Community](https://discord.gg/6XGTCbKkUy)** | **[Hardware Designs](https://a360.co/4a2WvzV)**

---

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [Demo Scripts (No Hardware Required)](#demo-scripts-no-hardware-required)
- [CLI Simulator](#cli-simulator)
- [Installation](#installation)
- [Architecture](#architecture)
- [Development Workflow](#development-workflow)
- [Testing](#testing)
- [Contributing](#contributing)
- [License](#license)

---

## Features

### 🎮 Seven Minigames
- **Ghost Runner** - Timing and reaction game (awards Konami START button)
- **Spike Vector** - Pattern recognition (awards Konami DOWN button)
- **Firewall Decrypt** - Multi-section decryption puzzle (awards Konami LEFT button)
- **Cipher Path** - Navigation challenge (awards Konami RIGHT button)
- **Exploit Sequencer** - Sequence memorization (awards Konami B button)
- **Breach Defense** - Defense game (awards Konami A button)
- **Signal Echo** - Audio-visual pattern matching (awards Konami UP button)

Each game supports **Easy** and **Hard** difficulty modes.

### 🏆 Konami Progression System
- Beat all 7 minigames on **hard mode** to collect Konami controller buttons (↑↓←→BA START)
- Visual progress tracking via on-device UI
- Complete the set to unlock the **Konami Boon** special reward
- Re-encounter mechanics: face beaten NPCs again to choose higher difficulty

### 🎨 Color Profile System
- Unlock custom LED color themes by beating games on hard mode
- 7 unique profiles (one per minigame)
- Visual feedback during idle and gameplay states
- Persistent across sessions

### ⚔️ Multiplayer PvP
- **Quickdraw Duels**: Hunter vs Hunter wireless battles via ESP-NOW
- Reaction time measurement to the millisecond
- Win/loss tracking, streak counters, and matchmaking
- Broadcast discovery for automatic peer detection

### 🔧 Developer-Friendly CLI Simulator
- **Run without hardware** - native C++ simulator for Linux/macOS
- Simulate up to 8 devices simultaneously
- Serial cable connections, ESP-NOW peer communication, mock HTTP server
- Real-time state visualization with braille-rendered OLED display
- Interactive commands for gameplay testing

### 📊 Server Synchronization
- Real-time player stats (wins, losses, streaks, reaction times)
- Server-side progress tracking for Konami buttons and color profiles
- Conflict resolution for offline/online sync
- Graceful fallback to offline mode

### 🎭 Demo Mode
- **Five automated demo scripts** showcasing full gameplay loop
- Speed control for presentations (0.5x to 4x)
- Scriptable for conference demos and hackathons

---

## Quick Start

### Prerequisites
- **Linux or macOS** (Windows users: use WSL)
- **Python 3.7+**
- **PlatformIO Core**

### 60-Second Demo (No Hardware Required)

```bash
# Clone and build
git clone https://github.com/alleycatassetacquisitions/pdn.git
cd pdn
pip install platformio
pio run -e native_cli

# Run quick demo
./scripts/demo-quick.sh
```

This launches a **60-second automated demo** showing device pairing, minigame gameplay, and Konami button unlock.

---

## Demo Scripts (No Hardware Required)

All demos use the **CLI simulator** - no physical device needed!

| Script | Duration | What It Shows |
|--------|----------|---------------|
| `demo-quick.sh` | 60s | Device pairing, one minigame, Konami unlock |
| `demo-full-journey.sh` | 3-4 min | Complete player experience with stats and progression |
| `demo-all-games.sh` | 5-7 min | All 7 minigames with button mapping |
| `demo-konami-unlock.sh` | 8-10 min | Full hard-mode progression to unlock all 7 buttons |
| `demo-pvp-battle.sh` | 3-4 min | Wireless quickdraw duels and win tracking |

### Speed Control

```bash
# Run at 2x speed
DEMO_SPEED=2 ./scripts/demo-quick.sh

# Run at half speed (more readable)
DEMO_SPEED=0.5 ./scripts/demo-full-journey.sh
```

See **[HOWTO.md](HOWTO.md)** for complete hackathon presentation guide.

---

## CLI Simulator

The native CLI tool simulates PDN devices without hardware:

```bash
# Build simulator
pio run -e native_cli

# Launch interactive mode with 2 devices
.pio/build/native_cli/program 2
```

### Common Commands

```bash
# Device connections
cable 0 1              # Connect devices 0 and 1 via serial
cable -l               # List connections

# Gameplay
b 0                    # Primary button click on device 0
l 0                    # Primary button long-press
b2 0                   # Secondary button click

# Visualization
display on             # Enable display mirror + captions
mirror on              # Show braille-rendered OLED screens
captions on            # Show display text captions

# Information
state 0                # View device state
stats 0                # Show player statistics
progress 0             # View Konami button grid
colors 0               # Show unlocked color profiles
games                  # List all available minigames

# Device management
add hunter             # Add a new hunter device
add npc ghost-runner   # Add NPC for specific game
role all               # Show all device roles
reboot 0               # Restart device

# Advanced
peer 0 1 1             # Send ESP-NOW packet (device 0 -> device 1)
konami 0 0x7F          # Set Konami progress directly (testing)
help                   # Show all commands
help2                  # Show detailed command help
```

### Example Session

```bash
$ .pio/build/native_cli/program 2
> display on           # Enable visual feedback
> cable 0 1            # Connect Hunter to Bounty NPC
> b 0                  # Hunter clicks button to start game
> state 0              # View Hunter's current state
> progress 0           # Check Konami progress
> stats 0              # View player statistics
```

The simulator provides real-time visualization of:
- OLED display content (rendered as braille characters)
- LED strip colors and animations
- Haptic feedback intensity
- Serial/ESP-NOW communication packets
- State machine transitions
- Player statistics

---

## Installation

### Step 1: Install PlatformIO

```bash
pip install platformio
# or upgrade existing installation
platformio upgrade
```

### Step 2: Clone Repository

```bash
git clone https://github.com/alleycatassetacquisitions/pdn.git
cd pdn
```

### Step 3: Configure WiFi (For ESP32-S3 Hardware)

```bash
cp wifi_credentials.ini.example wifi_credentials.ini
```

Edit `wifi_credentials.ini` with your WiFi SSID, password, and API base URL. This file is excluded from version control.

### Step 4: Build

```bash
# CLI Simulator (Linux/macOS)
pio run -e native_cli

# ESP32-S3 Hardware
pio run -e esp32-s3_debug      # Development build (verbose logs)
pio run -e esp32-s3_release    # Production build (no logs)
```

### Step 5: Flash to Device (Hardware Only)

```bash
pio run -e esp32-s3_debug --target upload
pio device monitor                        # View serial output
```

---

## Architecture

### State Machine Pattern

All game logic uses the **State Machine** pattern with three lifecycle methods:

```cpp
class GameState : public State {
    void onStateMounted() override;    // Setup (called once on entry)
    void onStateLoop() override;       // Main logic (called every loop)
    void onStateDismounted() override; // Cleanup (called once on exit)
};
```

**State Transitions**: Conditions define when to move between states (e.g., button press, timer expiry, message received).

### Driver Abstraction

Hardware access goes through **driver interfaces**, enabling:
- **ESP32-S3 drivers** for real hardware
- **Native drivers** for CLI simulator
- Easy porting to new platforms

```
Device (abstract)
  ├── PDN (concrete implementation)
  └── Drivers (platform-specific)
      ├── ESP32-S3 (U8g2, FastLED, OneButton, WiFi, ESP-NOW)
      └── Native (mock implementations for CLI)
```

### Directory Structure

```
include/
  device/           # Hardware abstraction layer
    drivers/        # Platform-specific drivers
      esp32-s3/     # Real hardware (OLED, LEDs, buttons, WiFi)
      native/       # CLI simulator mocks
  game/             # Minigame logic (7 games + Quickdraw PvP)
    ghost-runner/
    spike-vector/
    firewall-decrypt/
    cipher-path/
    exploit-sequencer/
    breach-defense/
    signal-echo/
  state/            # State machine core
  cli/              # CLI-specific extensions
  utils/            # Timers, logging, UUID generation

src/                # Implementation files (.cpp)
test/
  test_core/        # Core unit tests (GoogleTest)
  test_cli/         # CLI simulator tests
scripts/            # Demo scripts for presentations
docs/               # Architecture, game mechanics, testing reports
```

---

## Development Workflow

### Build Environments

```bash
# CLI Simulator (fastest iteration)
pio run -e native_cli
.pio/build/native_cli/program 2

# Unit Tests (run before every commit)
pio test -e native               # Core tests (test_core/)
pio test -e native_cli_test      # CLI tests (test_cli/)

# Hardware Builds
pio run -e esp32-s3_debug        # Development (verbose logs)
pio run -e esp32-s3_release      # Production (optimized, no logs)
```

### Helper Script

Use `./dev.sh` for common tasks:

```bash
./dev.sh test         # Run all tests
./dev.sh build        # Build release firmware
./dev.sh sim 2        # Run simulator with 2 devices
./dev.sh full-check   # Complete verification before commit
./dev.sh help         # See all commands
```

### Development Flow

1. **Develop** using CLI simulator (fastest iteration, no hardware required)
2. **Write tests** in `test/test_core/` or `test/test_cli/`
3. **Run test suite** - must pass before commit
4. **Test on hardware** if driver changes were made
5. **Commit** with descriptive message (pre-commit hook runs tests)

---

## Testing

### Running Tests

```bash
# Core unit tests (GoogleTest framework)
pio test -e native

# CLI simulator tests
pio test -e native_cli_test

# Run all tests
./dev.sh test
```

**IMPORTANT**: The pre-commit hook automatically runs tests before every commit. Use `git commit --no-verify` only if the hook fails due to known issues, but always run tests manually first.

### Test Structure

```cpp
// GoogleTest + GMock
TEST_F(TestSuiteName, testDescription) {
    // Arrange
    // Act
    // Assert
}
```

Tests are located in:
- `test/test_core/` - Core functionality (state machine, drivers, game logic)
- `test/test_cli/` - CLI-specific (commands, device spawning, E2E gameplay)

**Coverage**: 126+ test cases covering state transitions, game mechanics, network protocols, difficulty tuning, and edge cases.

---

## Contributing

We welcome contributions! Whether you have feature suggestions, bug reports, or improvements, please follow these steps:

### How to Contribute

1. **Fork the repository**
2. **Create a branch** for your feature or fix:
   ```bash
   git checkout -b feature/new-feature
   ```
3. **Develop your feature** (preferably using CLI simulator for rapid iteration)
4. **Add tests** in `test/test_core/` or `test/test_cli/`
5. **Run test suite** - all tests must pass:
   ```bash
   pio test -e native && pio test -e native_cli_test
   ```
6. **Commit your changes**:
   ```bash
   git commit -m "Add new feature (#issue-number)"
   ```
7. **Push and create a pull request**:
   ```bash
   git push origin feature/new-feature
   gh pr create --title "Add new feature" --body "Fixes #issue"
   ```

### Contribution Guidelines

- **Test coverage required** - new features must include tests
- **Follow code style** - see `.clang-format` for C++ formatting rules
- **Hardware changes need device testing** - don't rely solely on CLI
- **One feature per PR** - keep changes focused and atomic
- **Descriptive commit messages** - explain what and why

See **[CLAUDE.md](CLAUDE.md)** for complete developer guide (architecture patterns, code conventions, Git workflow).

---

## Code Style

- **Classes**: `PascalCase` (e.g., `StateMachine`, `Player`)
- **Methods**: `camelCase` (e.g., `onStateMounted()`, `getUserID()`)
- **Constants**: `SCREAMING_SNAKE_CASE`
- **Indentation**: 4 spaces (no tabs)
- **Header guards**: `#pragma once`

Format files with:
```bash
clang-format -i <file>
```

---

## License

This project is licensed under the **MIT License**. See [LICENSE](LICENSE) for details.

---

## Contact & Community

- **Email**: [alleycatassetacquisitions@gmail.com](mailto:alleycatassetacquisitions@gmail.com)
- **Discord**: [https://discord.gg/6XGTCbKkUy](https://discord.gg/6XGTCbKkUy)
- **Website**: [alleycat.agency](https://alleycat.agency)
- **GitHub**: [https://github.com/alleycatassetacquisitions/pdn](https://github.com/alleycatassetacquisitions/pdn)

---

## Hardware Specifications

The PDN is built on the **ESP32-S3** platform with:

- **ESP32-S3 SoC** - Dual-core Xtensa LX7, 240MHz
- **128x64 OLED Display** - SSD1306 controller
- **19 RGB LEDs** - Addressable via FastLED (WS2812B compatible)
- **Haptic Feedback Motor** - PWM-controlled vibration
- **2 Programmable Buttons** - OneButton library for click/long-press detection
- **ESP-NOW** - Peer-to-peer wireless communication (~30m range)
- **WiFi** - HTTP API for server synchronization
- **Serial UART** - Cable connections for primary-auxiliary communication

**Fusion 360 CAD files**: [https://a360.co/4a2WvzV](https://a360.co/4a2WvzV)

**PCB designs**: KiCad 8 (to be released in separate repository)

---

## Platform Notes

### PioArduino Migration

This project uses **[pioarduino](https://github.com/pioarduino/platform-espressif32)**, a community fork of the Espressif32 platform with support for newer ESP-IDF and Arduino framework versions.

If you're experiencing build issues after pulling recent changes:

```bash
# Clean install
rm -rf ~/.platformio/platforms/espressif32*
rm -rf ~/.platformio/packages/framework-arduinoespressif32*
rm -rf .pio/
pio run -e esp32-s3_release
```

---

## Recent Development Activity

- **Demo scripts** for hackathon presentations (PR #37)
- **Comprehensive test suite** with 126+ test cases (PRs #45, #53, #66)
- **Display mirror with braille rendering** for CLI visualization (PR #67, #70)
- **Server sync** with conflict resolution (PR #50, #69)
- **Konami progression system** with 7-button meta-game (PR #14, #35, #38)
- **FDN minigames** (Ghost Runner, Spike Vector, Cipher Path, Exploit Sequencer, Breach Defense) (PR #32)
- **CLI simulator tool** for hardware-less development (PR #62)
- **PioArduino platform** migration for modern ESP-IDF (PR #65)

See commit history for detailed changelog.

---

**Last Updated**: 2026-02-14
**Maintained by**: Alleycat Asset Acquisitions
**Built for**: Neotropolis LARP and immersive gameplay events
