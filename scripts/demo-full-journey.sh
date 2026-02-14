#!/bin/bash
# PDN Full Journey Demo
# Complete player journey: Build simulator, create devices, play game, show progress

set -e

CLI_BIN="${CLI_BIN:-.pio/build/native_cli/program}"
DEMO_SPEED="${DEMO_SPEED:-1}"

echo "=== PDN Full Player Journey Demo ==="
echo "This demo shows a complete player experience:"
echo "  1. Device creation and roles"
echo "  2. NPC device connection"
echo "  3. Minigame gameplay"
echo "  4. Konami progress tracking"
echo "  5. Player statistics"
echo "  6. Color profile unlocks"
echo ""

demo_sleep() {
    sleep $(echo "$1 / $DEMO_SPEED" | bc -l 2>/dev/null || echo "$1")
}

if [ ! -x "$CLI_BIN" ]; then
    echo "Building CLI simulator..."
    ~/.platformio/penv/bin/platformio run -e native_cli || {
        echo "Build failed. Please run: pio run -e native_cli"
        exit 1
    }
fi

echo "Starting CLI simulator with 1 hunter device..."
echo ""

PIPE=$(mktemp -u)
mkfifo "$PIPE"
"$CLI_BIN" 1 < "$PIPE" &
CLI_PID=$!
exec 3>"$PIPE"

cleanup() {
    echo ""
    echo "Cleaning up..."
    exec 3>&-
    kill "$CLI_PID" 2>/dev/null || true
    rm -f "$PIPE"
}
trap cleanup EXIT INT TERM

demo_sleep 1

echo "=== STEP 1: Device Setup ==="
echo "Created hunter device (role: Hunter)"
echo ""
echo "list" >&3
demo_sleep 1

echo ""
echo "Checking device role:"
echo "role 0" >&3
demo_sleep 1

echo ""
echo "Initial player stats (before any gameplay):"
echo "stats 0" >&3
demo_sleep 2

echo ""
echo "=== STEP 2: Adding NPC Device ==="
echo "Creating Ghost Runner NPC for the hunter to play against"
echo ""
echo "add npc ghost-runner" >&3
demo_sleep 1

echo ""
echo "Listing all devices:"
echo "list" >&3
demo_sleep 1

echo ""
echo "=== STEP 3: Connecting Devices ==="
echo "Connecting hunter (0) to Ghost Runner NPC (1) via serial cable"
echo ""
echo "display on" >&3
demo_sleep 0.5
echo "cable 0 1" >&3
demo_sleep 2

echo ""
echo "Checking connection status:"
echo "cable -l" >&3
demo_sleep 1

echo ""
echo "Device states after connection:"
echo "state 0" >&3
demo_sleep 0.5
echo "state 1" >&3
demo_sleep 2

echo ""
echo "=== STEP 4: Playing Ghost Runner (Easy Mode) ==="
echo "Hunter presses button to start game..."
echo ""
echo "b 0" >&3
demo_sleep 2

echo ""
echo "Current state:"
echo "state 0" >&3
demo_sleep 1

echo ""
echo "Simulating gameplay (player hitting targets)..."
for i in {1..8}; do
    echo "b 0" >&3
    demo_sleep 0.4
done
demo_sleep 2

echo ""
echo "Game complete! Checking results..."
demo_sleep 1

echo ""
echo "=== STEP 5: Konami Progress ==="
echo "After beating Ghost Runner, the START button should be unlocked"
echo ""
echo "progress 0" >&3
demo_sleep 3

echo ""
echo "Konami button value (hex):"
echo "konami 0" >&3
demo_sleep 1

echo ""
echo "=== STEP 6: Player Statistics ==="
echo ""
echo "stats 0" >&3
demo_sleep 3

echo ""
echo "=== STEP 7: Color Profiles ==="
echo "Beating a minigame on hard mode unlocks that game's color profile"
echo ""
echo "colors 0" >&3
demo_sleep 3

echo ""
echo "=== STEP 8: Playing Again (Hard Mode) ==="
echo "Disconnecting and reconnecting to trigger re-encounter..."
echo ""
echo "cable -d 0 1" >&3
demo_sleep 1
echo "cable 0 1" >&3
demo_sleep 2

echo ""
echo "Hunter should see difficulty prompt now"
echo "state 0" >&3
demo_sleep 1

echo ""
echo "Selecting hard mode (button 2 long press)..."
echo "l2 0" >&3
demo_sleep 2

echo ""
echo "Playing hard mode..."
for i in {1..12}; do
    echo "b 0" >&3
    demo_sleep 0.3
done
demo_sleep 2

echo ""
echo "=== STEP 9: Final Stats ==="
echo ""
echo "stats 0" >&3
demo_sleep 2

echo ""
echo "Updated Konami progress:"
echo "progress 0" >&3
demo_sleep 2

echo ""
echo "Updated color profiles (Ghost Runner should now be unlocked):"
echo "colors 0" >&3
demo_sleep 3

echo ""
echo "=== Journey Complete! ==="
echo ""
echo "Summary of what we demonstrated:"
echo "  ✓ Hunter device creation"
echo "  ✓ NPC device creation and connection"
echo "  ✓ Serial cable pairing (Primary-to-Auxiliary)"
echo "  ✓ Minigame gameplay (Ghost Runner)"
echo "  ✓ Difficulty selection (Easy vs Hard)"
echo "  ✓ Konami button progression (START button unlocked)"
echo "  ✓ Player statistics tracking"
echo "  ✓ Color profile unlocks"
echo "  ✓ Re-encounter mechanics"
echo ""
echo "Next steps for a real player:"
echo "  • Beat all 7 minigames on hard mode to unlock all Konami buttons"
echo "  • Collect all buttons to enable Konami Challenge"
echo "  • Unlock all color profiles"
echo "  • Challenge other hunters in PvP quickdraw battles"
echo ""

demo_sleep 3
echo "quit" >&3

wait "$CLI_PID" 2>/dev/null || true
