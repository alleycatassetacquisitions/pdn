# PDN CLI Demo Recordings

This directory contains pre-recorded terminal sessions of the PDN CLI simulator running various demo scripts. These recordings serve as:

1. **Hackathon fallback** - If live demos fail during the February 15, 2026 presentation, play these recordings instead
2. **Documentation** - Visual reference showing expected behavior of each demo scenario
3. **Quick preview** - View demos without building and running the CLI simulator

## Recordings

### fdn-quickstart.recording
**Duration:** ~30 seconds
**Demonstrates:** Player encountering NPC, playing Signal Echo minigame, winning, and unlocking Konami button

**Invocation:**
```bash
.pio/build/native_cli/program --npc signal-echo --auto-cable --script demos/fdn-quickstart.demo
```

**What to expect:**
- Hunter device auto-cables to Signal Echo NPC
- FDN handshake and game launch
- 4-button sequence gameplay
- Win state and Konami button unlock

---

### full-progression.recording
**Duration:** ~3 minutes
**Demonstrates:** Completing all 7 FDN minigames, collecting all Konami buttons, unlocking the boon

**Invocation:**
```bash
.pio/build/native_cli/program 1 --script demos/full-progression.demo
```

**What to expect:**
- Sequential play through all 7 minigames:
  1. Ghost Runner (UP button)
  2. Spike Vector (DOWN button)
  3. Firewall Decrypt (LEFT button)
  4. Cipher Path (RIGHT button)
  5. Exploit Sequencer (B button)
  6. Breach Defense (A button)
  7. Signal Echo (START button)
- Konami button collection after each game
- Final boon unlock when all 7 buttons collected

---

### all-games-showcase.recording
**Duration:** ~2 minutes
**Demonstrates:** Each of the 7 minigames played in standalone challenge mode

**Invocation:**
```bash
.pio/build/native_cli/program 1 --script demos/all-games-showcase.demo
```

**What to expect:**
- Rapid showcase of all 7 minigame mechanics
- Standalone challenge devices (no FDN handshake)
- Device reboots between games for clean state

---

### duel.recording
**Duration:** ~20 seconds
**Demonstrates:** Two players engaging in a face-to-face Quickdraw duel

**Invocation:**
```bash
.pio/build/native_cli/program 2 --script demos/duel.demo
```

**What to expect:**
- Hunter initiates duel broadcast
- Bounty accepts duel
- 3-second countdown
- Draw phase - first to press wins
- Result display and return to Idle

---

## How to Replay

### Simple playback (no timing)
```bash
cat docs/demo-recordings/fdn-quickstart.recording
```

This will dump the entire recording to your terminal at once. ANSI color codes and formatting will render if your terminal supports them.

### Frame-by-frame playback (requires scriptreplay)
If the recordings include timing files (`.timing`), use:
```bash
scriptreplay docs/demo-recordings/fdn-quickstart.timing docs/demo-recordings/fdn-quickstart.recording
```

**Note:** Current recordings do not include timing files. To create recordings with timing:
```bash
script -t 2>file.timing file.recording
```

---

## Terminal Requirements

### Minimum terminal size
**80x24** (standard VT100 size)

**Recommended:** 120x50 for full device panel display without line wrapping

### Color support
Terminal must support ANSI escape codes for proper rendering:
- **256-color mode** recommended (most modern terminals)
- True color (24-bit) optional but enhances LED visualization

### Compatible terminals
- **Linux/macOS:** iTerm2, Terminal.app, GNOME Terminal, Konsole, Alacritty, Kitty
- **Windows:** Windows Terminal, WSL2 terminal

---

## Re-recording Demos

If demo scripts change or you need to update recordings:

```bash
# Build the CLI binary first
python3 -m platformio run -e native_cli

# Record a demo (example: fdn-quickstart)
script -q -c ".pio/build/native_cli/program --npc signal-echo --auto-cable --script demos/fdn-quickstart.demo" docs/demo-recordings/fdn-quickstart.recording

# With timing information (for scriptreplay)
script -q -t 2>docs/demo-recordings/fdn-quickstart.timing -c ".pio/build/native_cli/program --npc signal-echo --auto-cable --script demos/fdn-quickstart.demo" docs/demo-recordings/fdn-quickstart.recording
```

### Recording commands for all demos

```bash
# FDN Quickstart
script -q -c ".pio/build/native_cli/program --npc signal-echo --auto-cable --script demos/fdn-quickstart.demo" docs/demo-recordings/fdn-quickstart.recording

# Full Progression
script -q -c ".pio/build/native_cli/program 1 --script demos/full-progression.demo" docs/demo-recordings/full-progression.recording

# All Games Showcase
script -q -c ".pio/build/native_cli/program 1 --script demos/all-games-showcase.demo" docs/demo-recordings/all-games-showcase.recording

# Duel
script -q -c ".pio/build/native_cli/program 2 --script demos/duel.demo" docs/demo-recordings/duel.recording
```

---

## Troubleshooting

### Recording shows garbled text
- Ensure terminal supports ANSI escape codes
- Try setting `TERM=xterm-256color` before replay

### Recording doesn't fit in terminal
- Resize terminal to at least 120x50
- Or use smaller terminal font size

### Colors don't render
- Check terminal color support: `echo $TERM`
- Modern terminals default to `xterm-256color` or better

### Recording playback is too fast/slow
- Use `scriptreplay` with timing files for accurate playback speed
- Or manually control speed with `pv` command:
  ```bash
  pv -qL 9600 docs/demo-recordings/fdn-quickstart.recording
  ```
  (Simulates 9600 baud modem speed for retro effect)

---

## Notes

- All recordings include full ANSI color codes and terminal control sequences
- Recordings are platform-independent (Linux/macOS/WSL)
- File sizes are small (~50-200KB per recording) due to text format
- Safe to commit to git (no binary data)

---

*Last updated: 2026-02-14*
*Recordings generated for Neotropolis Quickdraw hackathon demo (February 15, 2026)*
