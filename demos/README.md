# PDN Demo Scripts

Pre-scripted demonstrations for the CLI simulator showcasing the full PDN experience. These are designed for hackathon presentations and can be run automatically using the `--script` flag.

## Running Demos

All demos are run using the CLI simulator with the `--script` flag:

```bash
.pio/build/native_cli/program [options] --script demos/<demo-file>.demo
```

## Available Demos

### 1. FDN Quickstart (`fdn-quickstart.demo`)

**Runtime:** ~30 seconds
**Purpose:** Quick introduction to the FDN encounter system

Demonstrates:
- Player encountering an FDN NPC device
- Playing Signal Echo minigame (EASY difficulty)
- Winning and unlocking the first Konami button
- Returning to Idle state

**Run command:**
```bash
.pio/build/native_cli/program --npc signal-echo --auto-cable --script demos/fdn-quickstart.demo
```

### 2. Full Progression (`full-progression.demo`)

**Runtime:** ~3 minutes
**Purpose:** Complete progression loop showcasing all 7 minigames

Demonstrates:
- Sequential encounters with all 7 FDN game types
- Collecting all 7 Konami buttons (UP, DOWN, LEFT, RIGHT, B, A, START)
- Automatic Konami boon unlock after completing all games
- Full progression tracking

**Run command:**
```bash
.pio/build/native_cli/program 1 --script demos/full-progression.demo
```

**Games played (in order):**
1. Ghost Runner → UP button
2. Spike Vector → DOWN button
3. Firewall Decrypt → LEFT button
4. Cipher Path → RIGHT button
5. Exploit Sequencer → B button
6. Breach Defense → A button
7. Signal Echo → START button

### 3. Quickdraw Duel (`duel.demo`)

**Runtime:** ~20 seconds
**Purpose:** Player-vs-player combat demonstration

Demonstrates:
- Two players (Hunter vs Bounty) engaging in duel
- Duel initiation and acceptance
- Countdown sequence (3 seconds)
- Draw phase and result determination
- Winner/loser outcome

**Run command:**
```bash
.pio/build/native_cli/program 2 --script demos/duel.demo
```

### 4. All Games Showcase (`all-games-showcase.demo`)

**Runtime:** ~2 minutes
**Purpose:** Showcase each minigame individually in standalone mode

Demonstrates:
- All 7 minigames running in standalone mode (no FDN progression)
- Each game played to completion
- Quick transitions between games using reboot command

**Run command:**
```bash
.pio/build/native_cli/program 1 --script demos/all-games-showcase.demo
```

**Games showcased:**
1. Ghost Runner
2. Spike Vector
3. Firewall Decrypt
4. Cipher Path
5. Exploit Sequencer
6. Breach Defense
7. Signal Echo

## Script Syntax Reference

Demo scripts support the following commands:

### Wait Commands
- `wait <seconds>` - Pause execution for the specified duration (supports decimals)

### Device Commands
- `state` - Display current state of all devices
- `add <role>` - Add a new device (`hunter`, `bounty`, `npc <game>`, `challenge <game>`)
- `select <device>` - Select a device for subsequent commands
- `role [device]` - Show device role (hunter/bounty)
- `konami [device]` - Show Konami button progress and boon status
- `reboot [device]` - Reboot device to FetchUserData state

### Connection Commands
- `cable <devA> <devB>` - Connect two devices via serial cable
- `cable -d <devA> <devB>` - Disconnect serial cable
- `cable -l` - List all cable connections

### Button Commands
- `b [device]` - Click primary button (UP/confirm)
- `b2 [device]` - Click secondary button (DOWN/cancel)
- `l [device]` - Long press primary button
- `l2 [device]` - Long press secondary button

### Display Commands
- `display [on|off]` - Toggle display mirror and captions
- `mirror [on|off]` - Toggle display mirror only
- `captions [on|off]` - Toggle captions only

### Other Commands
- `games` - List all available game names
- `help` - Show command help
- `quit` - Exit simulator

### Comments
- Lines starting with `#` are comments and ignored
- Empty lines are ignored

## CLI Startup Flags

The CLI simulator supports several startup flags that can be used with demos:

- `--script <file>` - Run a demo script (non-interactive mode)
- `--npc <game>` - Spawn an FDN NPC device (can be repeated)
- `--game <game>` - Launch a minigame directly in standalone mode
- `--auto-cable` - Automatically cable first player to first NPC
- `-n <count>` or `--count <count>` - Create N devices (1-8)
- `-h` or `--help` - Show help message

**Valid game names:**
- `ghost-runner`
- `spike-vector`
- `firewall-decrypt`
- `cipher-path`
- `exploit-sequencer`
- `breach-defense`
- `signal-echo`

## Tips for Creating Custom Demos

1. **Timing is critical** - Wait durations must account for:
   - State transition delays (~100-500ms)
   - Game intro timers (typically 2s)
   - Display/animation durations
   - Evaluation states (3-4s for win/lose)

2. **Use state commands** - Insert `state` commands to verify expected states during debugging

3. **Button sequences** - Minigame input sequences are deterministic based on RNG seed:
   - Use sequences from test suites for reliable wins
   - Signal Echo EASY: 4 buttons, 3 mistakes allowed
   - Each game has specific input patterns documented in test files

4. **Device indexing** - Devices are indexed starting from 0 in the order they're created

5. **Cable management** - Remember to disconnect cables when switching NPCs to avoid conflicts

6. **Script mode differences** - Script mode:
   - No TUI (text user interface)
   - No keyboard input
   - State output after each command
   - Simpler output format

## Troubleshooting

**Demo doesn't complete successfully:**
- Verify timing - some games may need longer wait durations
- Check button sequences - minigames have specific expected inputs
- Ensure cables are connected before FDN handshake
- Verify device indices match your setup

**State transitions don't occur:**
- Increase wait duration before the failing transition
- Check that previous state completed successfully
- Some states have mandatory timers that must expire

**Button presses don't register:**
- Add small wait between button presses (0.1-0.3s)
- Ensure device index is correct
- Verify game is in input-accepting state

## Development

Demo scripts are located in the `demos/` directory. To add a new demo:

1. Create a new `.demo` file
2. Follow the script syntax reference
3. Test with `--script` flag
4. Document in this README

For more information on the CLI simulator, see the main PDN documentation.
