# PDN Hackathon Demo Presenter Guide

**Last Updated**: February 14, 2026
**Event**: Hackathon Demo Day - February 15, 2026
**Audience**: Non-technical presenters and stakeholders

---

## 📋 Table of Contents

1. [Setup](#setup)
2. [Demo 1: FDN Quickstart](#demo-1-fdn-quickstart)
3. [Demo 2: Full Progression](#demo-2-full-progression)
4. [Demo 3: Quickdraw Duel](#demo-3-quickdraw-duel)
5. [Demo 4: All Games Showcase](#demo-4-all-games-showcase)
6. [Recovery & Troubleshooting](#recovery--troubleshooting)
7. [Quick Reference Card](#quick-reference-card)

---

## Setup

### Prerequisites

Before the demo, ensure you have:

1. **Operating System**: Linux or macOS (Windows users must use WSL)
2. **Python 3.7+**: Check with `python3 --version`
3. **PlatformIO Core**: Install with `pip install platformio`
4. **Terminal Requirements**:
   - Minimum size: 80x24 characters
   - Color support: Required
   - Recommended font size: 14pt or larger for audience visibility
   - Dark background recommended

### Recommended Terminal Settings

For best visibility during the demo:

- **Terminal Emulator**: GNOME Terminal, iTerm2, or Terminal.app
- **Font**: Monospace (e.g., "DejaVu Sans Mono" or "Menlo")
- **Font Size**: 16-18pt (large enough for audience to see from distance)
- **Color Scheme**: Dark background with bright text
- **Window Size**: Full screen or at least 100x40 characters

### Building the CLI Simulator

**Before the demo**, build the simulator binary:

```bash
# Navigate to project directory
cd /home/ubuntu/pdn

# Build the CLI simulator (takes 1-2 minutes)
pio run -e native_cli
```

**Expected output**: Should end with:
```
Building in release mode
Linking .pio/build/native_cli/program
Building .pio/build/native_cli/idedata.json
DONE
```

### Verify the Build

Quick smoke test to ensure everything works:

```bash
# Test that the binary exists and runs
.pio/build/native_cli/program --help
```

**Expected output**: Should show help text with:
```
PDN CLI Simulator
Usage: program [count] [options]
  -n, --count <N>        - Create N devices (1-8)
  --npc <game>           - Spawn NPC device
  ...
```

If this works, you're ready for the demo! 🎉

---

## Demo 1: FDN Quickstart

**Duration**: ~30 seconds
**Purpose**: Quick introduction to the FDN encounter system
**What it shows**: Player meets NPC, plays one minigame (Signal Echo), wins, unlocks first Konami button

### Talking Points

Start with: *"This demonstrates how a player connects to an NPC station in the LARP arena. The PDN uses serial cables to initiate encounters, similar to how Game Boy Link Cables worked. Watch as the player connects, plays Signal Echo - a memory pattern game - and unlocks the first piece of the Konami code puzzle."*

### Commands to Type

```bash
# Run the quickstart demo
.pio/build/native_cli/program --npc signal-echo --auto-cable --script demos/fdn-quickstart.demo
```

### Expected Output

The demo will automatically:

1. **Initial Setup (3 seconds)**: Two devices boot up
   - Device 0: Player device (starts in Idle state)
   - Device 1: NPC device running Signal Echo

2. **State Display**: Shows both devices in their initial states
   ```
   Device PDN-0: Idle
   Device NPC-SE: NpcIdle
   ```

3. **FDN Detection (5 seconds)**: NPC broadcasts its presence
   - Player receives FDN signal
   - Player enters "FdnDetected" state
   - Handshake completes
   - Player transitions to "FdnPrompt" state

4. **Game Start (2.5 seconds)**: Signal Echo intro plays
   - Game displays title screen
   - Instructions appear on simulated OLED

5. **Gameplay (2 seconds)**:
   - Sequence shown (4 lights: UP, DOWN, UP, DOWN)
   - Player automatically inputs correct sequence
   - Each button press simulated with `b 0` or `b2 0` commands

6. **Win State (6 seconds)**:
   - "YOU WIN!" displayed
   - Konami button unlocked (UP button / 0x01)
   - Returns to FdnComplete state

7. **Final Status**:
   ```
   Device PDN-0: Idle
   Konami: 0x01 (1/7) boon=no
   ```

### What to Point Out

- **Cable connection**: `--auto-cable` flag simulates physical serial connection
- **State transitions**: Watch the state names change (Idle → FdnDetected → FdnPrompt → SignalEchoIntro → SignalEchoShowSequence → SignalEchoInput → Win → FdnComplete → Idle)
- **Konami progress**: After winning, the player has unlocked 1 of 7 buttons (0x01 bitmask)
- **Demo script**: The `.demo` file contains scripted commands with precise timing

### Transition

*"That was the basic loop - encounter an NPC, play a minigame, collect a Konami button. In a real LARP event, players would physically walk around the venue connecting to different NPC stations. Let's see what happens when a player completes all 7 games..."*

---

## Demo 2: Full Progression

**Duration**: ~3 minutes
**Purpose**: Complete player journey through all 7 minigames
**What it shows**: Sequential encounters, Konami button collection, progression tracking, automatic boon unlock

### Talking Points

Start with: *"This is the full progression loop. Our player will encounter all 7 FDN minigame stations, collect all 7 Konami buttons (UP, DOWN, LEFT, RIGHT, B, A, START), and unlock the Konami Boon - a special reward for completing the entire challenge. Each game tests different skills: timing, memory, pattern recognition, navigation."*

### Commands to Type

```bash
# Run the full progression demo
.pio/build/native_cli/program 1 --script demos/full-progression.demo
```

### Expected Output

The demo progresses through 7 games in sequence:

#### Game 1: Ghost Runner (~25 seconds)
- **NPC Device Added**: Device 1 spawns as Ghost Runner NPC
- **Cable Connection**: Player connects to NPC
- **Handshake**: FDN protocol establishes connection (4 seconds)
- **Gameplay**:
  - Intro screen (2.5 seconds)
  - Timer display (2 seconds)
  - Player presses button when ghost is in target zone
  - Win state (4 seconds)
- **Reward**: UP button unlocked (0x01)
- **Disconnection**: Cable removed

#### Game 2: Spike Vector (~25 seconds)
- **NPC Added**: Device 2 (Spike Vector)
- **Gameplay**: Player dodges spikes with timed button presses
  - Button sequence: PRIMARY, SECONDARY, PRIMARY
- **Reward**: DOWN button unlocked (0x03 total)

#### Game 3: Firewall Decrypt (~30 seconds)
- **NPC Added**: Device 3 (Firewall Decrypt)
- **Gameplay**: Multi-round puzzle
  - Round 1: Scroll and confirm pattern
  - Round 2: Match decryption key
- **Reward**: LEFT button unlocked (0x07 total)

#### Game 4: Cipher Path (~25 seconds)
- **NPC Added**: Device 4 (Cipher Path)
- **Gameplay**: Navigate maze with directional inputs
  - Button sequence: UP, DOWN, UP, DOWN
- **Reward**: RIGHT button unlocked (0x0F total)

#### Game 5: Exploit Sequencer (~25 seconds)
- **NPC Added**: Device 5 (Exploit Sequencer)
- **Gameplay**: Quick-time event sequence
  - Three precisely-timed button presses
- **Reward**: B button unlocked (0x1F total)

#### Game 6: Breach Defense (~25 seconds)
- **NPC Added**: Device 6 (Breach Defense)
- **Gameplay**: Defensive strategy with secondary button
  - Three defensive actions
- **Reward**: A button unlocked (0x3F total)

#### Game 7: Signal Echo (~25 seconds)
- **NPC Added**: Device 7 (Signal Echo)
- **Gameplay**: Memory sequence (same as Demo 1)
  - 4-button pattern: UP, DOWN, UP, DOWN
- **Reward**: START button unlocked (0x7F total)

#### Final State
```
Konami: 0x7F (7/7) boon=yes
Device PDN-0 [0]: Hunter
```

All 7 Konami buttons collected! Boon automatically granted.

### What to Point Out

- **Dynamic device spawning**: Watch as new NPC devices are created on-demand with `add npc <game_name>`
- **Progression tracking**: After each game, the `konami 0` command shows updated bitmask
  - Start: 0x00 (0/7)
  - After Game 1: 0x01 (1/7)
  - After Game 2: 0x03 (2/7)
  - After Game 3: 0x07 (3/7)
  - After Game 4: 0x0F (4/7)
  - After Game 5: 0x1F (5/7)
  - After Game 6: 0x3F (6/7)
  - After Game 7: 0x7F (7/7) → **BOON ACTIVE**
- **Game variety**: Each minigame has unique mechanics (timing, memory, navigation, strategy)
- **Cable management**: Devices are disconnected between encounters to free up serial port
- **Automatic boon**: When all 7 buttons are collected (bitmask = 0x7F), the boon is granted automatically
- **Role display**: The final `role 0` command confirms the player is a Hunter with full progression

### Key Moments to Emphasize

- **First button unlock** (after Ghost Runner): *"Notice the Konami progress shows 0x01 - binary bit 0 is set. This maps to the UP button on a Konami controller."*
- **Halfway point** (after Cipher Path): *"Four buttons down, three to go. The player is building towards the full Konami code."*
- **Final unlock** (after Signal Echo): *"0x7F means all seven bits are set - the player has completed the challenge! The boon is automatically unlocked."*

### Transition

*"So that's the full single-player progression. But PDN also supports player-versus-player combat. Let's see two hunters face off in a quickdraw duel..."*

---

## Demo 3: Quickdraw Duel

**Duration**: ~20 seconds
**Purpose**: Player-vs-player wireless combat demonstration
**What it shows**: Two players dueling, countdown sequence, reaction time mechanics, winner determination

### Talking Points

Start with: *"The PDN isn't just about minigames - it's also a combat system. Two players can engage in wireless quickdraw duels using ESP-NOW, the same protocol that powers Apple AirDrop. Watch as two hunters initiate a duel, wait through a suspenseful countdown, and the first to draw their weapon wins. This measures reaction time to the millisecond."*

### Commands to Type

```bash
# Run the duel demo with 2 devices
.pio/build/native_cli/program 2 --script demos/duel.demo
```

### Expected Output

#### Initial Setup (3 seconds)
- Device 0: Hunter (Idle)
- Device 1: Bounty (Idle)

#### State Check
```
Device PDN-0: Idle
Device PDN-1: Idle
```

#### Duel Initiation (1 second)
- Device 0 (Hunter) **long-presses primary button** (`l 0` command)
- Hunter broadcasts duel challenge via ESP-NOW
- Device 1 (Bounty) receives challenge
- State changes:
  ```
  Device PDN-0: DuelInitiated
  Device PDN-1: DuelReceived
  ```

#### Duel Acceptance (1 second)
- Device 1 (Bounty) **long-presses primary button** (`l 1` command)
- Bounty accepts duel
- Both devices enter countdown
- State changes:
  ```
  Device PDN-0: DuelCountdown
  Device PDN-1: DuelCountdown
  ```

#### Countdown (3 seconds)
- Both devices display countdown timer: 3... 2... 1...
- LEDs pulse in sync
- Haptic feedback on each count

#### Draw Phase (immediate)
- Countdown reaches zero
- Both devices enter "Draw" state
- **Device 1 (Bounty) draws first** (`b 1` command)
- Reaction times measured

#### Result (1 second)
- Winner: Device 1 (Bounty)
- Loser: Device 0 (Hunter)
- State changes:
  ```
  Device PDN-0: DuelLost
  Device PDN-1: DuelWon
  ```

#### Result Display (3 seconds)
- Winner sees "VICTORY" screen with reaction time
- Loser sees "DEFEAT" screen
- Stats updated (wins/losses/streaks)

#### Return to Idle (immediate)
```
Device PDN-0: Idle
Device PDN-1: Idle
```

#### Final Roles
```
Device PDN-0 [0]: Hunter | Device PDN-1 [1]: Bounty
```

### What to Point Out

- **Wireless communication**: No cable needed - uses ESP-NOW peer-to-peer protocol (30m range)
- **Long-press mechanics**: Duel initiation requires holding button down (prevents accidental duels)
- **Synchronization**: Both devices countdown in sync despite wireless latency
- **Reaction time**: Winner is determined by who presses first after countdown ends
- **Win/loss tracking**: Player stats are updated (use `stats 0` or `stats 1` to see details)
- **Fair play**: Pressing too early results in false start / disqualification

### Additional Commands (Optional)

If you want to show stats after the duel:

```bash
# After the demo completes, you can run:
.pio/build/native_cli/program 2

# Then type these commands:
stats 0    # Show Device 0 stats (Hunter - lost the duel)
stats 1    # Show Device 1 stats (Bounty - won the duel)
```

**Expected stats output**:
```
PDN-1 [Bounty] Stats:
  Matches: 1 (W:1 L:0) Streak:1
  Reaction: Last=XXXms Avg=XXXms
  Konami: 0/7 unlocked (0x00) Boon:No
  Color Profile: BOUNTY DEFAULT
  Sync: PENDING
```

### Transition

*"That's the PvP system. Hunters can duel each other in the arena for glory and bragging rights. Now let's take a tour of all seven minigames running in standalone mode - this gives you a sense of the gameplay variety..."*

---

## Demo 4: All Games Showcase

**Duration**: ~2 minutes
**Purpose**: Showcase each minigame individually
**What it shows**: All 7 games in standalone challenge mode (no FDN progression)

### Talking Points

Start with: *"Let's tour all seven minigames. This demo runs each game in standalone mode - no NPC encounters, just pure gameplay. You'll see the variety of mechanics we've implemented: timing challenges, pattern matching, navigation puzzles, defensive strategy. Each game is designed to test different player skills."*

### Commands to Type

```bash
# Run the all-games showcase demo
.pio/build/native_cli/program 1 --script demos/all-games-showcase.demo
```

### Expected Output & Game Descriptions

The demo will cycle through all 7 games automatically. Here's what each game demonstrates:

#### 1. Ghost Runner (~10 seconds)
- **Mechanic**: Timing and reaction
- **Display**: Ghost sprite moves across screen, target zone highlighted
- **Gameplay**: Player presses button when ghost enters target zone
- **Goal**: Catch the ghost at the right moment
- **Difficulty**: EASY has wider target zone, HARD has narrow zone and faster ghost
- **Visual**: You'll see the ghost position counter incrementing, then the button press (`b 0`), then win state
- **Talking point**: *"This tests pure reaction time - like catching a fastball."*

#### 2. Spike Vector (~10 seconds)
- **Mechanic**: Directional targeting with movement prediction
- **Display**: Spike walls approaching from different directions
- **Gameplay**: Player must dodge or target spikes with correct button presses
- **Button sequence**: PRIMARY (dodge up), SECONDARY (dodge down), PRIMARY (dodge up)
- **Goal**: Survive all spike waves
- **Difficulty**: EASY has slower spikes, HARD has faster spikes and more waves
- **Talking point**: *"This is about spatial awareness - you're dodging obstacles in real time."*

#### 3. Firewall Decrypt (~15 seconds)
- **Mechanic**: Pattern matching and selection
- **Display**: List of decryption patterns, target pattern highlighted
- **Gameplay**: Two rounds - scroll through patterns with PRIMARY, confirm with SECONDARY
- **Round 1**: Scroll (b 0), scroll (b 0), confirm (b2 0)
- **Round 2**: Scroll (b 0), confirm (b2 0)
- **Goal**: Match the correct decryption patterns
- **Difficulty**: EASY has fewer patterns, HARD has more patterns and tighter timing
- **Talking point**: *"This is a puzzle - you're a hacker breaking through security layers."*

#### 4. Cipher Path (~10 seconds)
- **Mechanic**: Navigation and pathfinding
- **Display**: Cipher maze visualization, player position marked
- **Gameplay**: Navigate through maze with directional button inputs
- **Button sequence**: UP (b 0), DOWN (b2 0), UP (b 0), DOWN (b2 0)
- **Goal**: Reach the exit without hitting walls
- **Difficulty**: EASY has shorter path, HARD has longer path with more turns
- **Talking point**: *"This is orienteering - finding the path through the cipher maze."*

#### 5. Exploit Sequencer (~10 seconds)
- **Mechanic**: Quick-time event sequence memory
- **Display**: Timing bars and exploit chain progress
- **Gameplay**: Execute exploit chain with precisely-timed button presses
- **Button sequence**: Three PRIMARY presses with 0.4s intervals
- **Goal**: Hit all timing windows without missing
- **Difficulty**: EASY has wider timing windows, HARD has narrow windows
- **Talking point**: *"This is about rhythm and timing - like hitting combos in a fighting game."*

#### 6. Breach Defense (~10 seconds)
- **Mechanic**: Defensive strategy with resource management
- **Display**: Incoming threats, defense status
- **Gameplay**: Defend against breaches with SECONDARY button presses
- **Button sequence**: Three SECONDARY presses with 0.4s intervals
- **Goal**: Block all incoming threats
- **Difficulty**: EASY has fewer threats, HARD has more threats and faster pace
- **Talking point**: *"This is defensive - you're holding the line against attacks."*

#### 7. Signal Echo (~10 seconds)
- **Mechanic**: Memory and pattern replication (the game from Demo 1)
- **Display**: LED light sequence, then input prompt
- **Gameplay**: Watch sequence, then repeat it
- **Button sequence**: UP (b 0), DOWN (b2 0), UP (b 0), DOWN (b2 0)
- **Goal**: Perfectly replicate the shown sequence
- **Difficulty**: EASY has 4 elements and allows 3 mistakes, HARD has 8 elements and allows 1 mistake
- **Talking point**: *"This is the classic Simon Says pattern memory game - watch and repeat."*

#### Final State
```
Device PDN-0: Idle
```

All games completed!

### What to Point Out

- **Game variety**: *"Notice how each game feels completely different - we're not just reskinning the same mechanics."*
- **Standalone mode**: *"These games can run independently without NPC encounters - useful for testing and training."*
- **Reboot command**: Between games, the `reboot 0` command resets the device to Idle state (like power cycling)
- **Consistent difficulty**: All games are on EASY mode in this demo - HARD mode increases speed, complexity, and reduces error tolerance
- **State machine**: Each game follows the same lifecycle: Intro → Gameplay → Evaluation → Win/Loss → Idle
- **Visual feedback**: In a real device, each game has unique LED animations, haptic patterns, and OLED graphics

### Key Moments to Emphasize

- **After Ghost Runner**: *"That was pure timing - you'll see that theme repeated in Spike Vector and Exploit Sequencer."*
- **After Firewall Decrypt**: *"This is the most puzzle-like game - it requires deliberate thought, not just reflexes."*
- **After all games**: *"Seven distinct game types means players never get bored. Each Konami button represents mastery of a different skill."*

### Transition

*"And that's the full PDN experience - single-player progression, PvP combat, and a variety of engaging minigames. The CLI simulator makes all of this testable without needing physical hardware. Questions?"*

---

## Recovery & Troubleshooting

### If the CLI Binary Won't Build

**Symptom**: `pio run -e native_cli` fails with compilation errors

**Fix**:
1. Check PlatformIO installation:
   ```bash
   pip install --upgrade platformio
   ```

2. Clean and rebuild:
   ```bash
   rm -rf .pio/
   pio run -e native_cli
   ```

3. Verify build environment:
   ```bash
   pio run -e native_cli_test
   ```
   This tests the same codebase with unit tests - if this works but `native_cli` doesn't, the issue is likely in `src/cli-main.cpp`.

4. **Fallback**: If build continues to fail, use pre-recorded demo videos (if available in `docs/demo-recordings/`)

### If a Demo Script Fails Mid-Run

**Symptom**: Demo hangs, states don't transition, or commands don't execute

**Fix**:

1. **Kill the process**: Press `Ctrl+C` to stop the simulator

2. **Restart the demo**: Run the same command again
   ```bash
   .pio/build/native_cli/program [options] --script demos/<demo>.demo
   ```

3. **Check demo file**: Ensure the `.demo` file hasn't been modified
   ```bash
   git status demos/
   ```

4. **Manual execution**: If script mode fails, run interactively:
   ```bash
   .pio/build/native_cli/program 2
   # Then manually type commands from the demo script
   ```

### If the Terminal Gets Garbled

**Symptom**: Display corruption, strange characters, unresponsive terminal

**Fix**:

1. **Reset terminal**: Press `Ctrl+C`, then type:
   ```bash
   reset
   ```
   or
   ```bash
   clear
   ```

2. **Restart simulator**: Close and reopen terminal window if reset doesn't work

3. **Disable TUI features**: If display mirror causes issues, run demos without it (scripts don't use TUI by default)

### If a Game Hangs

**Symptom**: Demo script stops progressing, no state changes

**Fix**:

1. **Force quit**: Press `Ctrl+C`

2. **Check for timing issues**: Some games may need longer wait times on slower systems
   - Edit demo script temporarily to increase `wait` durations
   - Example: Change `wait 2` to `wait 3`

3. **Run game individually**:
   ```bash
   .pio/build/native_cli/program 1
   # Then type:
   add challenge <game-name>
   b 0    # Start the game
   ```

### If Cable Connection Fails

**Symptom**: Devices don't communicate, FDN handshake never completes

**Fix**:

1. **Manual cable connection**:
   ```bash
   .pio/build/native_cli/program 2
   cable 0 1    # Connect device 0 to device 1
   state        # Verify connection
   ```

2. **Check cable status**:
   ```bash
   cable -l     # List all cable connections
   ```

3. **Disconnect and reconnect**:
   ```bash
   cable -d 0 1    # Disconnect
   wait 1
   cable 0 1       # Reconnect
   ```

### If You Run Out of Time

**Fallback Plan**:

1. **Show only Demo 1**: The quickstart demo is 30 seconds and shows the core loop
2. **Use screenshots**: Take screenshots during practice runs and show those
3. **Describe instead of demo**: Walk through the concept without live execution
4. **Reference documentation**: Point audience to `demos/README.md` and GitHub repository

### Common Error Messages

| Error | Meaning | Fix |
|-------|---------|-----|
| `Command not found: pio` | PlatformIO not installed | `pip install platformio` |
| `Device not found` | Invalid device index | Use `0` for first device, `1` for second |
| `Failed to connect` | Devices already connected | Use `cable -d 0 1` to disconnect first |
| `Invalid game` | Typo in game name | Check `games` command for valid names |
| `Cannot add more devices (max 8)` | Device limit reached | Restart simulator or use `reboot` |

### Pre-Demo Checklist

Before the presentation:

- [ ] Build completed successfully: `pio run -e native_cli`
- [ ] Binary exists: `ls -lh .pio/build/native_cli/program`
- [ ] Binary runs: `.pio/build/native_cli/program --help`
- [ ] Demo 1 tested: Run `fdn-quickstart.demo` once
- [ ] Terminal font size increased (16pt+)
- [ ] Terminal window maximized
- [ ] `demos/` directory contains all 4 `.demo` files
- [ ] Backup plan ready (screenshots or skip to Q&A)

---

## Quick Reference Card

*Print this page and keep it next to your keyboard during the demo.*

### Demo 1: FDN Quickstart (30s)
```bash
.pio/build/native_cli/program --npc signal-echo --auto-cable --script demos/fdn-quickstart.demo
```
**Say**: *"Player connects to NPC, plays Signal Echo memory game, unlocks first Konami button"*

### Demo 2: Full Progression (3min)
```bash
.pio/build/native_cli/program 1 --script demos/full-progression.demo
```
**Say**: *"Complete journey through all 7 games, collecting all Konami buttons, unlocking the boon"*

### Demo 3: Quickdraw Duel (20s)
```bash
.pio/build/native_cli/program 2 --script demos/duel.demo
```
**Say**: *"Wireless PvP combat - two hunters duel, countdown, fastest draw wins"*

### Demo 4: All Games Showcase (2min)
```bash
.pio/build/native_cli/program 1 --script demos/all-games-showcase.demo
```
**Say**: *"Tour of all 7 minigames showing gameplay variety - timing, memory, navigation, strategy"*

### Emergency Commands

| Command | Purpose |
|---------|---------|
| `Ctrl+C` | Kill simulator |
| `reset` | Fix garbled terminal |
| `clear` | Clear screen |
| `.pio/build/native_cli/program 2` | Start interactive mode |
| `state` | Show current state |
| `help` | Show command list |

### 7 Minigames

1. **Ghost Runner** - Timing (UP button)
2. **Spike Vector** - Targeting (DOWN button)
3. **Firewall Decrypt** - Puzzle (LEFT button)
4. **Cipher Path** - Navigation (RIGHT button)
5. **Exploit Sequencer** - Timing (B button)
6. **Breach Defense** - Defense (A button)
7. **Signal Echo** - Memory (START button)

### Key Talking Points

- **No hardware required** - CLI simulator runs on any Unix system
- **State machine architecture** - clean, testable, extensible
- **Driver abstraction** - ESP32 drivers + native mocks for testing
- **Multiplayer** - serial cables (wired) + ESP-NOW (wireless)
- **Progression system** - Konami code meta-game rewards mastery
- **126+ test cases** - comprehensive test coverage
- **LARP focus** - designed for Neotropolis live events

### If Things Go Wrong

1. Press `Ctrl+C`
2. Type `reset`
3. Re-run the demo command
4. If still broken, skip to next demo or Q&A

---

## Tips for Success

### Before the Demo

1. **Practice**: Run each demo at least twice to familiarize yourself with the flow
2. **Timing**: Note the actual duration of each demo on your system (may vary ±20%)
3. **Narration**: Practice your talking points out loud
4. **Questions**: Prepare answers for common questions:
   - *"What hardware does this run on?"* → ESP32-S3 with OLED, LEDs, buttons, haptics
   - *"How many players?"* → 1-8 players simultaneously (CLI supports 8, hardware typically 2-4)
   - *"What's the range?"* → ESP-NOW is ~30m line-of-sight, serial cables are ~2m
   - *"Can I try it?"* → Yes! GitHub repo is public, runs on any Linux/macOS machine

### During the Demo

1. **Speak slowly**: Give audience time to read terminal output
2. **Point out key moments**: State transitions, Konami unlocks, win states
3. **Pause between demos**: Give audience time to ask questions
4. **Show your screen**: Ensure projector/screen share is working before you start
5. **Have water**: 6+ minutes of continuous talking requires hydration

### After the Demo

1. **Thank the audience**
2. **Point to resources**:
   - GitHub: `github.com/alleycatassetacquisitions/pdn`
   - Discord: `discord.gg/6XGTCbKkUy`
   - Documentation: `docs/` directory in repo
3. **Invite questions**
4. **Offer to send demo videos** (if recorded)

---

**Good luck with your demo! 🎉**

If you have any questions or issues during practice, reach out in the Discord community. We're here to help!

---

*Last Updated: February 14, 2026*
*Maintained by: Alleycat Asset Acquisitions*
*Built for: Neotropolis LARP hackathon demo*
