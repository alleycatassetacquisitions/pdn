# PDN CLI Simulator

The CLI tool allows you to simulate a network of pdn's to test and build games without having to worry about device deployment and hw related concerns

## Platform Requirements

**Unix/POSIX only** Windows users should use WSL or Git Bash.

The tool requires POSIX terminal APIs (`termios`, `fcntl`) for raw input handling and ANSI escape codes for the UI.

## Building

```bash
pio run -e native_cli
```

## Running

```bash
.pio/build/native_cli/program [num_devices]
```

- `num_devices` (optional): Number of PDN devices to spawn (default: 2)
- All devices start as Hunters; `add bounty` adds a Bounty at runtime
- Device IDs start at `0010` and increment

## Controls

| Key | Action |
|-----|--------|
| `←` / `→` | Select previous/next device |
| `↑` | Primary button press (selected device) |
| `↓` | Secondary button press (selected device) |
| `q` | Quit simulator |

## Commands

Type commands at the prompt and press Enter:

| Command | Description |
|---------|-------------|
| `help` | Show all available commands |
| `select <n>` | Select device by index (e.g., `select 1`) |
| `add [hunter\|bounty]` | Add a new device at runtime |
| `cable <a> <b>` | Connect devices via serial cable |
| `disconnect <n>` | Disconnect device's serial cable |
| `press <n> [primary\|secondary]` | Simulate button press |
| `longpress <n> <ms>` | Simulate long press with duration |
| `peer <src> <dst> <type> [hex]` | Send ESP-NOW packet between devices |
| `inject <dst> <type> [hex]` | Inject ESP-NOW packet from external source |
| `role [n\|all]` | Show a device's role, or every device's |
| `state` | Show all device states |

## UI Panel

The selected device displays a detailed panel showing:

- **Player Info**: ID, allegiance, win/loss record
- **State**: Current game state with history
- **Display**: Simulated 128x64 OLED content (text mode)
- **LEDs**: Left/Right light strips with RGB values
- **Serial Jacks**: Buffer sizes, connection status, TX/RX history
- **ESP-NOW**: Connection state, MAC address, packet history
- **Errors**: Recent LOG_E messages

## Serial Cable Simulation

The `cable` command simulates plugging in an audio cable between two devices:

- **Hunter ↔ Bounty**: Connects Primary-to-Primary jacks (normal gameplay)
- **Same role**: Connects Primary-to-Auxiliary jacks

Example workflow:
```
cable 0 1          # Connect device 0 to device 1
disconnect 0       # Disconnect device 0's cable
```

## Example Session

```bash
# Start with 2 devices (both Hunters)
.pio/build/native_cli/program 2

# In the simulator:
add bounty             # Device 2 joins as a Bounty
role all               # Check each device's role
cable 0 2              # Connect hunter to bounty - starts handshake
# Watch devices progress through: Idle → MatchCountdown → MatchState

# Press buttons during match
press 0 primary        # Hunter presses button
press 2 primary        # Bounty presses button
```

## Architecture

The CLI simulator reuses the core PDN game logic with native driver implementations:

- `NativeSerialDriver` - Buffered serial with cable broker integration
- `NativePeerCommsDriver` - ESP-NOW simulation via `NativePeerBroker`
- `NativeDisplayDriver` - Text-based display capture
- `NativeLightStripDriver` - LED state tracking
- `NativeButtonDriver` - Callback-based button simulation
