# PDN Hackathon Demo Guide

> **Quick Start**: Run `./scripts/demo-quick.sh` for a 60-second overview!

## Overview

This guide shows you how to demonstrate the PDN (Portable Data Node) at hackathons, conferences, and demonstrations. The PDN is an ESP32-S3 based game device featuring 7 minigames, Konami code progression, and wireless PvP battles.

All demos use the **CLI simulator** - no hardware required!

## Prerequisites

### System Requirements
- Linux or macOS
- Python 3.7+
- PlatformIO Core
- Bash shell
- ~500MB disk space for build artifacts

### Installation

```bash
# Clone the repository
git clone https://github.com/alleycatassetacquisitions/pdn.git
cd pdn

# Install PlatformIO (if not already installed)
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o get-platformio.py
python3 get-platformio.py

# Build the CLI simulator
pio run -e native_cli

# Verify build
.pio/build/native_cli/program --help
```

**Build time**: First build takes ~3-5 minutes. Subsequent builds are faster.

## Available Demo Scripts

All scripts are in the `scripts/` directory and support speed control via `DEMO_SPEED` environment variable.

| Script | Duration | Purpose | What to Show |
|--------|----------|---------|--------------|
| `demo-quick.sh` | 60s | Quick overview | Device pairing, minigame, Konami unlock |
| `demo-full-journey.sh` | 3-4 min | Complete player experience | Full gameplay loop, stats, progression |
| `demo-all-games.sh` | 5-7 min | All 7 minigames | Game variety, Konami button mapping |
| `demo-konami-unlock.sh` | 8-10 min | Full progression system | Hard mode, all 7 buttons, boon unlock |
| `demo-pvp-battle.sh` | 3-4 min | Multiplayer PvP | ESP-NOW, wireless duels, win tracking |

## Running Demos

### Basic Usage

```bash
# Make scripts executable (first time only)
chmod +x scripts/*.sh

# Run any demo
./scripts/demo-quick.sh
./scripts/demo-full-journey.sh
./scripts/demo-all-games.sh
./scripts/demo-konami-unlock.sh
./scripts/demo-pvp-battle.sh
```

### Speed Control

Control demo speed with `DEMO_SPEED` (default: 1.0):

```bash
# Run at double speed
DEMO_SPEED=2 ./scripts/demo-quick.sh

# Run at half speed (slower, more readable)
DEMO_SPEED=0.5 ./scripts/demo-full-journey.sh

# Run at 3x speed (very fast)
DEMO_SPEED=3 ./scripts/demo-all-games.sh
```

### Custom CLI Binary Path

If your CLI simulator is in a different location:

```bash
CLI_BIN=/path/to/program ./scripts/demo-quick.sh
```

## Demo Scenarios

### Scenario 1: First-Time Audience (60 seconds)
**Use**: `demo-quick.sh`

**What to Say**:
- "This is PDN - a handheld game device for LARP events"
- "Players connect via serial cable to discover minigames"
- "Beating games unlocks Konami code buttons"
- "Collect all 7 buttons to unlock the Konami Challenge"

**Key Moments**:
- [10s] Show device connection - "This simulates plugging in a cable"
- [20s] Show gameplay - "Each game has unique mechanics"
- [40s] Show Konami progress - "Visual feedback for progression"

### Scenario 2: Technical Deep-Dive (3-4 minutes)
**Use**: `demo-full-journey.sh`

**What to Say**:
- "The device uses state machines for game logic"
- "Serial communication uses custom protocol with P-A and P-P modes"
- "Player stats are tracked locally and synced to server"
- "Hard mode unlocks color profiles for LED customization"

**Key Moments**:
- [30s] Explain device roles (Hunter vs Bounty vs NPC)
- [60s] Show game state transitions
- [120s] Explain re-encounter mechanics
- [180s] Show difficulty selection and color profile unlock

### Scenario 3: Game Design Showcase (5-7 minutes)
**Use**: `demo-all-games.sh`

**What to Say**:
- "7 minigames, each with unique mechanics"
- "Each game awards a specific Konami button"
- "Ghost Runner: timing game, Spike Vector: pattern recognition"
- "Games scale from easy to hard difficulty"

**Key Moments**:
- Show each game's distinct personality
- Explain Konami button mapping
- Demonstrate progression through all games

### Scenario 4: Progression Systems (8-10 minutes)
**Use**: `demo-konami-unlock.sh`

**What to Say**:
- "Full progression: beat all games on hard mode"
- "Konami buttons: ↑↓←→BA START"
- "Hard mode required for button unlock"
- "Boon unlocks when all 7 buttons collected"

**Key Moments**:
- [60s] Show first easy mode win
- [120s] Demonstrate re-encounter and difficulty prompt
- [300s] Show hard mode gameplay
- [480s] Show complete Konami grid with all buttons

### Scenario 5: Multiplayer PvP (3-4 minutes)
**Use**: `demo-pvp-battle.sh`

**What to Say**:
- "Hunters battle in wireless quickdraw duels"
- "ESP-NOW enables peer-to-peer communication"
- "Reaction times measured to the millisecond"
- "Win streaks and matchmaking tracked"

**Key Moments**:
- [30s] Show ESP-NOW discovery
- [90s] First duel and reaction time measurement
- [180s] Win streak mechanics
- [240s] Final stats comparison

## Interactive Mode

For hands-on demos where audience members can try it:

```bash
# Start interactive CLI simulator
pio run -e native_cli
.pio/build/native_cli/program 2

# In simulator prompt:
help            # Show all commands
help2           # Show detailed command help
display on      # Enable visual display
cable 0 1       # Connect devices
b 0             # Press button on device 0
state           # Show device states
progress 0      # Show Konami progress
stats 0         # Show player stats
quit            # Exit
```

### Common Interactive Commands

```bash
# Device Management
list                    # List all devices
add hunter              # Add hunter device
add npc ghost-runner    # Add NPC for specific game
role all                # Show all device roles

# Connections
cable 0 1               # Connect devices 0 and 1
cable -l                # List connections
cable -d 0 1            # Disconnect devices

# Gameplay
b 0                     # Primary button click on device 0
l 0                     # Primary button long-press on device 0
b2 0                    # Secondary button click
l2 0                    # Secondary button long-press

# Visualization
display on              # Enable display mirror + captions
mirror on               # Enable display mirror only
captions on             # Enable captions only

# Information
state 0                 # Show device state
stats 0                 # Show player statistics
progress 0              # Show Konami progress grid
colors 0                # Show color profile status
games                   # List all available games

# Advanced
peer 0 1 1              # Send ESP-NOW packet
reboot 0                # Restart device
konami 0 0x7F           # Set Konami progress (testing)
```

## Troubleshooting

### Build Fails

```bash
# Clean and rebuild
pio run -e native_cli --target clean
pio run -e native_cli

# Check PlatformIO installation
pio --version

# Update PlatformIO
pio upgrade
```

### Demo Script Hangs

```bash
# Kill hung processes
pkill -9 program
rm -f /tmp/fifo*

# Re-run demo
./scripts/demo-quick.sh
```

### Display Not Showing

```bash
# In CLI, ensure display is enabled:
display on

# Or manually:
mirror on
captions on
```

### Simulator Crashes

```bash
# Check for existing instances
ps aux | grep program

# Kill all instances
pkill program

# Restart
./scripts/demo-quick.sh
```

### "Command not found" Errors

```bash
# Ensure scripts are executable
chmod +x scripts/*.sh

# Run with bash explicitly
bash scripts/demo-quick.sh
```

## Key Talking Points

### Architecture
- **State Machine Design**: All game logic uses state pattern with mount/loop/dismount lifecycle
- **Driver Abstraction**: Hardware access through driver interfaces (ESP32-S3 vs Native)
- **CLI Simulator**: Native C++ simulator matches hardware behavior exactly
- **Multi-device**: Simulate up to 8 devices simultaneously

### Hardware (when showing physical device)
- ESP32-S3 SoC (dual-core, 240MHz)
- 128x64 OLED display
- RGB LED strip (addressable)
- Haptic feedback motor
- Two programmable buttons
- ESP-NOW (peer-to-peer wireless, ~30m range)
- Serial UART (cable connections)

### Game Mechanics
- **7 Minigames**: Ghost Runner, Spike Vector, Firewall Decrypt, Cipher Path, Exploit Sequencer, Breach Defense, Signal Echo
- **Difficulty Scaling**: Easy and Hard modes
- **Konami Progression**: Each game awards a specific button when beaten on hard mode
- **Color Profiles**: Unlock custom LED themes by beating games
- **Re-encounters**: Reconnecting to beaten NPCs prompts for difficulty selection

### Networking
- **Serial (Cable)**: Primary-to-Auxiliary (hunter-to-NPC) and Primary-to-Primary (hunter-to-hunter)
- **ESP-NOW**: Broadcast discovery, unicast game packets
- **HTTP**: Server sync for player stats (mock in CLI)

### Player Progression
- Match statistics (wins, losses, streak)
- Reaction time tracking (average and last)
- Konami button collection (7 total)
- Color profile unlocks (7 themed profiles)
- Server sync for persistence

## Presentation Tips

### For Technical Audiences
- Emphasize architecture patterns (state machines, driver abstraction)
- Show CLI source code for interesting states
- Discuss testing strategy (GoogleTest, native doubles)
- Explain build system (PlatformIO, multi-environment)

### For Non-Technical Audiences
- Focus on gameplay and player experience
- Show visual display output
- Emphasize LARP immersion and storytelling
- Demonstrate physical hardware if available

### For Game Designers
- Discuss minigame mechanics and difficulty curves
- Explain progression systems and unlocks
- Show Konami code easter egg design
- Talk about PvP balance and reaction time fairness

### For Embedded Engineers
- Dive into ESP-NOW implementation
- Discuss memory constraints and optimization
- Show native driver implementation
- Explain OLED rendering and double-buffering

## Demo Variants

### Speed Run (30 seconds)
```bash
DEMO_SPEED=4 ./scripts/demo-quick.sh
```
Focus: Device pairing, one game, Konami unlock

### Deep Dive (15 minutes)
```bash
# Run all demos in sequence
for script in scripts/demo-*.sh; do
    echo "=== Running $script ==="
    DEMO_SPEED=0.8 "$script"
    sleep 2
done
```
Focus: Complete system tour

### Live Coding (20+ minutes)
```bash
# Interactive CLI session
.pio/build/native_cli/program 3
```
Focus: Let audience members input commands, explore freely

## Recording Demos

### Terminal Recording with `script`

```bash
# Record demo session
script -c "./scripts/demo-quick.sh" demo-quick.log

# Replay recording
cat demo-quick.log
```

### Video Recording with `asciinema`

```bash
# Install asciinema
pip install asciinema

# Record demo
asciinema rec demo-quick.cast -c "./scripts/demo-quick.sh"

# Play back
asciinema play demo-quick.cast

# Upload to asciinema.org (optional)
asciinema upload demo-quick.cast
```

### Terminal Recording with `vhs`

If `vhs` is available (requires Go):

```bash
# Install vhs
go install github.com/charmbracelet/vhs@latest

# Create tape file
cat > demo.tape <<EOF
Output demo.gif
Set Width 1200
Set Height 800
Type "./scripts/demo-quick.sh"
Enter
Sleep 65s
EOF

# Generate GIF
vhs demo.tape
```

## FAQ

**Q: Do I need real hardware to run demos?**
A: No! All demos use the CLI simulator which runs on Linux/macOS.

**Q: How long does the first build take?**
A: 3-5 minutes for initial build. Incremental builds are much faster.

**Q: Can I customize the demos?**
A: Yes! All scripts are in `scripts/` and use standard bash. Edit freely.

**Q: What if the simulator crashes?**
A: Kill hung processes (`pkill program`) and re-run. Check open fifos (`rm -f /tmp/fifo*`).

**Q: How do I speed up/slow down demos?**
A: Use `DEMO_SPEED` environment variable. Higher = faster, lower = slower.

**Q: Can audience members try it?**
A: Yes! Run `.pio/build/native_cli/program 2` for interactive mode. See "Interactive Mode" section.

**Q: What's the difference between hunter and bounty?**
A: Hunters have offensive stats/colors, bounties have defensive. Hunters initiate challenges.

**Q: What's an NPC device?**
A: Simulated opponent that runs a single minigame. Used for solo play testing.

**Q: How does the Konami code work?**
A: Beat all 7 minigames on hard mode to collect buttons ↑↓←→BA START. Unlock the Konami Challenge.

**Q: Can I see the hardware?**
A: Hardware designs are in the repo. See `docs/` for schematics and PCB layouts (if available).

## Next Steps

- **Development**: See `CLAUDE.md` for full developer guide
- **Contributing**: Check GitHub issues and PR guidelines
- **Community**: Join Discord at https://discord.gg/6XGTCbKkUy
- **Hardware**: Contact alleycatassetacquisitions@gmail.com

## Resources

- **GitHub**: https://github.com/alleycatassetacquisitions/pdn
- **CLI Commands**: Run `help` and `help2` in simulator
- **State Machine Docs**: See `include/state/state.hpp`
- **Game States**: See `include/game/*/`-states.hpp` files

---

**Last Updated**: 2026-02-14
**Demo Script Version**: 1.0
**CLI Simulator Build**: native_cli environment
