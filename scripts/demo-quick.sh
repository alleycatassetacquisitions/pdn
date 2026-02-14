#!/bin/bash
# PDN Quick Demo - 60-second highlight reel
# Shows core functionality: device pairing, game play, Konami button unlock

set -e

CLI_BIN="${CLI_BIN:-.pio/build/native_cli/program}"
DEMO_SPEED="${DEMO_SPEED:-1}"  # Multiplier for sleep durations

echo "=== PDN Quick Demo (60 seconds) ==="
echo "Showcasing: Device pairing, minigame, Konami button unlock"
echo ""

# Helper function for delays
demo_sleep() {
    sleep $(echo "$1 / $DEMO_SPEED" | bc -l 2>/dev/null || echo "$1")
}

# Check if CLI simulator is built
if [ ! -x "$CLI_BIN" ]; then
    echo "Error: CLI simulator not found at $CLI_BIN"
    echo "Building CLI simulator..."
    ~/.platformio/penv/bin/platformio run -e native_cli || {
        echo "Build failed. Please run: pio run -e native_cli"
        exit 1
    }
fi

echo "Starting CLI simulator..."
echo ""

# Create a named pipe for commands
PIPE=$(mktemp -u)
mkfifo "$PIPE"

# Start the CLI simulator in background, reading from pipe
"$CLI_BIN" 2 < "$PIPE" &
CLI_PID=$!

# Open pipe for writing
exec 3>"$PIPE"

# Cleanup on exit
cleanup() {
    echo ""
    echo "Cleaning up..."
    exec 3>&-
    kill "$CLI_PID" 2>/dev/null || true
    rm -f "$PIPE"
}
trap cleanup EXIT INT TERM

# Give simulator time to start
demo_sleep 1

echo "[0s] Two devices spawned: Hunter and Bounty"
echo "---"
echo "list" >&3
demo_sleep 1

echo ""
echo "[3s] Enable display mirror to see OLED screens"
echo "---"
echo "display on" >&3
demo_sleep 1

echo ""
echo "[6s] Connecting Hunter device (0) to Bounty NPC (1) via serial cable"
echo "    This simulates plugging in a physical cable connection"
echo "---"
echo "cable 0 1" >&3
demo_sleep 2

echo ""
echo "[10s] Viewing device states - Hunter should detect NPC"
echo "---"
echo "state 0" >&3
demo_sleep 1
echo "state 1" >&3
demo_sleep 2

echo ""
echo "[15s] Hunter clicks primary button to start Ghost Runner minigame"
echo "---"
echo "b 0" >&3
demo_sleep 3

echo ""
echo "[20s] Simulating gameplay (button presses)..."
echo "---"
# Simulate some gameplay
for i in {1..5}; do
    echo "b 0" >&3
    demo_sleep 0.3
done
demo_sleep 2

echo ""
echo "[28s] Checking player stats on Hunter device"
echo "---"
echo "stats 0" >&3
demo_sleep 2

echo ""
echo "[32s] Viewing Konami progress - should show START button unlocked"
echo "    Beating minigames unlocks Konami code buttons"
echo "---"
echo "progress 0" >&3
demo_sleep 3

echo ""
echo "[38s] Disconnecting devices"
echo "---"
echo "cable -d 0 1" >&3
demo_sleep 2

echo ""
echo "[42s] Adding a third device (Hunter) for PvP demo"
echo "---"
echo "add hunter" >&3
demo_sleep 1

echo ""
echo "[45s] Enabling ESP-NOW peer discovery between hunters"
echo "---"
echo "peer 0 2 1" >&3  # Send discovery packet
demo_sleep 2

echo ""
echo "[50s] Viewing all device roles"
echo "---"
echo "role all" >&3
demo_sleep 2

echo ""
echo "[55s] Final stats summary"
echo "---"
echo "stats 0" >&3
demo_sleep 3

echo ""
echo "[60s] Demo complete!"
echo ""
echo "=== Key Features Demonstrated ==="
echo "✓ Multi-device simulation"
echo "✓ Serial cable connections (Primary-to-Auxiliary pairing)"
echo "✓ Minigame gameplay (Ghost Runner)"
echo "✓ Konami button progression system"
echo "✓ Player stats tracking"
echo "✓ ESP-NOW peer discovery"
echo ""
echo "For more demos, see: demo-full-journey.sh, demo-all-games.sh, demo-konami-unlock.sh"

demo_sleep 2
echo "quit" >&3

wait "$CLI_PID" 2>/dev/null || true
