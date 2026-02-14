#!/bin/bash
# PDN PvP Battle Demo
# Two hunter devices discover each other and engage in quickdraw duels

set -e

CLI_BIN="${CLI_BIN:-.pio/build/native_cli/program}"
DEMO_SPEED="${DEMO_SPEED:-1}"

echo "=== PDN PvP Quickdraw Battle Demo ==="
echo ""
echo "This demo showcases player-vs-player quickdraw duels:"
echo "  1. Two hunter devices spawn"
echo "  2. ESP-NOW peer discovery"
echo "  3. Wireless connection establishment"
echo "  4. Multiple quickdraw rounds"
echo "  5. Win/loss tracking and streaks"
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

echo "Starting CLI simulator with 2 hunter devices..."
echo ""

PIPE=$(mktemp -u)
mkfifo "$PIPE"
"$CLI_BIN" 2 < "$PIPE" &
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
echo "Two hunter devices are spawned (Primary-to-Primary pairing)"
echo ""
echo "list" >&3
demo_sleep 1

echo ""
echo "Checking device roles:"
echo "role all" >&3
demo_sleep 1

echo ""
echo "Enabling display mirror to see both screens:"
echo "display on" >&3
demo_sleep 1

echo ""
echo "Initial stats for both hunters:"
echo "stats 0" >&3
demo_sleep 1
echo "stats 1" >&3
demo_sleep 2

echo ""
echo "=== STEP 2: ESP-NOW Peer Discovery ==="
echo "In the real hardware, hunters broadcast discovery packets."
echo "Here we'll simulate peer discovery between the two devices."
echo ""

echo "Hunter 0 discovers Hunter 1 (simulating ESP-NOW broadcast)..."
echo "peer 0 broadcast 1" >&3
demo_sleep 1

echo ""
echo "Hunter 1 responds to discovery..."
echo "peer 1 broadcast 1" >&3
demo_sleep 2

echo ""
echo "Current device states (should be in discovery/pairing state):"
echo "state 0" >&3
demo_sleep 0.5
echo "state 1" >&3
demo_sleep 2

echo ""
echo "=== STEP 3: Quickdraw Challenge Initiation ==="
echo "Hunter 0 initiates quickdraw challenge..."
echo ""

# Simulate challenge initiation
echo "b 0" >&3
demo_sleep 2

echo ""
echo "Hunter 1 accepts the challenge..."
echo "b 1" >&3
demo_sleep 2

echo ""
echo "Device states (entering quickdraw countdown):"
echo "state 0" >&3
demo_sleep 0.5
echo "state 1" >&3
demo_sleep 2

echo ""
echo "=== STEP 4: Quickdraw Duel - Round 1 ==="
echo "Countdown begins: 3... 2... 1... DRAW!"
echo ""

demo_sleep 2

echo "Hunter 1 draws first!"
echo "b 1" >&3
demo_sleep 1

echo ""
echo "Round complete! Checking results..."
demo_sleep 2

echo ""
echo "Hunter 0 stats (should show 1 loss):"
echo "stats 0" >&3
demo_sleep 2

echo ""
echo "Hunter 1 stats (should show 1 win):"
echo "stats 1" >&3
demo_sleep 2

echo ""
echo "=== STEP 5: Quickdraw Duel - Round 2 ==="
echo "Hunters prepare for another round..."
echo ""

demo_sleep 1
echo "b 0" >&3
demo_sleep 1
echo "b 1" >&3
demo_sleep 2

echo ""
echo "Countdown: 3... 2... 1... DRAW!"
demo_sleep 2

echo "This time Hunter 0 is faster!"
echo "b 0" >&3
demo_sleep 1

echo ""
echo "Round complete!"
demo_sleep 2

echo ""
echo "Updated stats for Hunter 0 (1 win, 1 loss):"
echo "stats 0" >&3
demo_sleep 2

echo ""
echo "Updated stats for Hunter 1 (1 win, 1 loss):"
echo "stats 1" >&3
demo_sleep 2

echo ""
echo "=== STEP 6: Quickdraw Duel - Round 3 (Tiebreaker) ==="
echo "One more round to determine the victor!"
echo ""

demo_sleep 1
echo "b 0" >&3
demo_sleep 1
echo "b 1" >&3
demo_sleep 2

echo ""
echo "Countdown: 3... 2... 1... DRAW!"
demo_sleep 2

echo "Hunter 0 draws first again!"
echo "b 0" >&3
demo_sleep 1

echo ""
echo "Round complete! Hunter 0 takes the series 2-1!"
demo_sleep 2

echo ""
echo "=== STEP 7: Final Statistics ==="
echo ""

echo "Hunter 0 final stats (2 wins, 1 loss, win streak: 2):"
echo "stats 0" >&3
demo_sleep 2

echo ""
echo "Hunter 1 final stats (1 win, 2 losses):"
echo "stats 1" >&3
demo_sleep 2

echo ""
echo "=== Battle Complete! ==="
echo ""
echo "Summary of what we demonstrated:"
echo "  ✓ Multi-hunter device simulation"
echo "  ✓ ESP-NOW peer discovery and pairing"
echo "  ✓ Wireless Primary-to-Primary connection"
echo "  ✓ Quickdraw challenge initiation"
echo "  ✓ Multiple rounds of PvP duels"
echo "  ✓ Win/loss tracking and reaction times"
echo "  ✓ Win streak mechanics"
echo ""
echo "Key mechanics:"
echo "  • Hunters discover each other via ESP-NOW broadcast"
echo "  • First to press their button after 'DRAW' signal wins"
echo "  • Reaction times are measured in milliseconds"
echo "  • Win streaks are tracked for competitive play"
echo "  • False starts (pressing before 'DRAW') result in penalties"
echo ""
echo "On real hardware:"
echo "  • LED strips show visual feedback (red/green for win/loss)"
echo "  • Haptic feedback provides tactile response"
echo "  • OLED displays show countdown and results"
echo "  • Wireless range: ~30m line-of-sight"
echo ""

demo_sleep 3
echo "quit" >&3

wait "$CLI_PID" 2>/dev/null || true
