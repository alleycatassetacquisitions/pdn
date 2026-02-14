#!/bin/bash
# PDN All Games Showcase
# Demonstrates all 7 minigames in sequence

set -e

CLI_BIN="${CLI_BIN:-.pio/build/native_cli/program}"
DEMO_SPEED="${DEMO_SPEED:-1}"

echo "=== PDN All Minigames Showcase ==="
echo ""
echo "This demo showcases all 7 minigames:"
echo "  1. Ghost Runner    (START button)"
echo "  2. Spike Vector    (DOWN button)"
echo "  3. Firewall Decrypt (LEFT button)"
echo "  4. Cipher Path     (RIGHT button)"
echo "  5. Exploit Sequencer (B button)"
echo "  6. Breach Defense  (A button)"
echo "  7. Signal Echo     (UP button)"
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

PIPE=$(mktemp -u)
mkfifo "$PIPE"
# Start with 1 hunter and all 7 NPC devices at once
"$CLI_BIN" 1 --npc ghost-runner --npc spike-vector --npc firewall-decrypt --npc cipher-path --npc exploit-sequencer --npc breach-defense --npc signal-echo < "$PIPE" &
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

echo "Starting with 1 hunter device and 7 NPC devices..."
echo "display on" >&3
demo_sleep 1

# Array of game names and their Konami buttons
GAMES=(
    "ghost-runner:START"
    "spike-vector:DOWN"
    "firewall-decrypt:LEFT"
    "cipher-path:RIGHT"
    "exploit-sequencer:B"
    "breach-defense:A"
    "signal-echo:UP"
)

GAME_NUM=1

for GAME_INFO in "${GAMES[@]}"; do
    GAME_NAME="${GAME_INFO%:*}"
    KONAMI_BUTTON="${GAME_INFO#*:}"

    echo ""
    echo "==================================================================="
    echo "GAME $GAME_NUM/7: ${GAME_NAME^^} (Konami Button: $KONAMI_BUTTON)"
    echo "==================================================================="
    echo ""

    # NPC device ID (0 is hunter, 1-7 are NPCs)
    NPC_ID=$GAME_NUM

    echo ""
    echo "Connecting hunter (0) to NPC ($NPC_ID)..."
    echo "cable 0 $NPC_ID" >&3
    demo_sleep 2

    echo ""
    echo "Starting $GAME_NAME..."
    echo "b 0" >&3
    demo_sleep 1

    echo ""
    echo "Current state:"
    echo "state 0" >&3
    demo_sleep 1

    echo ""
    echo "Playing $GAME_NAME (simulating button presses)..."

    # Different games might need different input patterns
    case "$GAME_NAME" in
        "ghost-runner"|"spike-vector"|"breach-defense")
            # Fast-paced timing games
            for i in {1..10}; do
                echo "b 0" >&3
                demo_sleep 0.3
            done
            ;;
        "firewall-decrypt"|"exploit-sequencer")
            # Pattern matching games
            for i in {1..8}; do
                echo "b 0" >&3
                demo_sleep 0.4
            done
            ;;
        "cipher-path"|"signal-echo")
            # Memory/sequence games
            for i in {1..6}; do
                echo "b 0" >&3
                demo_sleep 0.5
            done
            ;;
    esac

    demo_sleep 2

    echo ""
    echo "Game complete! Disconnecting..."
    echo "cable -d 0 $NPC_ID" >&3
    demo_sleep 1

    echo ""
    echo "Konami progress after $GAME_NAME:"
    echo "konami 0" >&3
    demo_sleep 1

    GAME_NUM=$((GAME_NUM + 1))

    if [ $GAME_NUM -le 7 ]; then
        echo ""
        echo "Moving to next game..."
        demo_sleep 2
    fi
done

echo ""
echo "==================================================================="
echo "All 7 Minigames Completed!"
echo "==================================================================="
echo ""

echo "Final Konami progress (visual grid):"
echo "progress 0" >&3
demo_sleep 3

echo ""
echo "Final player statistics:"
echo "stats 0" >&3
demo_sleep 3

echo ""
echo "Unlocked color profiles:"
echo "colors 0" >&3
demo_sleep 3

echo ""
echo "=== Showcase Complete! ==="
echo ""
echo "Each minigame unlocks a specific Konami button:"
echo "  Ghost Runner      → START button"
echo "  Spike Vector      → DOWN button"
echo "  Firewall Decrypt  → LEFT button"
echo "  Cipher Path       → RIGHT button"
echo "  Exploit Sequencer → B button"
echo "  Breach Defense    → A button"
echo "  Signal Echo       → UP button"
echo ""
echo "Collecting all 7 buttons unlocks the Konami Challenge!"
echo ""

demo_sleep 3
echo "quit" >&3

wait "$CLI_PID" 2>/dev/null || true
