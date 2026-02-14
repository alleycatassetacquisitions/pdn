#!/bin/bash
# PDN Konami Unlock Demo
# Shows full Konami progression: beat all 7 games on hard mode, collect buttons, trigger Konami Challenge

set -e

CLI_BIN="${CLI_BIN:-.pio/build/native_cli/program}"
DEMO_SPEED="${DEMO_SPEED:-1}"

echo "=== PDN Konami Code Progression Demo ==="
echo ""
echo "This demo shows the complete Konami code progression:"
echo "  • Beat all 7 minigames on HARD MODE"
echo "  • Collect all 7 Konami buttons (↑↓←→BA START)"
echo "  • Unlock the Konami Boon"
echo ""
echo "The Konami code buttons map to minigames:"
echo "  ↑ (UP)    = Signal Echo"
echo "  ↓ (DOWN)  = Spike Vector"
echo "  ← (LEFT)  = Firewall Decrypt"
echo "  → (RIGHT) = Cipher Path"
echo "  B         = Exploit Sequencer"
echo "  A         = Breach Defense"
echo "  START     = Ghost Runner"
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

echo "Starting hunter device..."
echo "display on" >&3
demo_sleep 1

echo ""
echo "Initial Konami progress (should be empty):"
echo "progress 0" >&3
demo_sleep 2

# Games in Konami button order
GAMES=(
    "signal-echo:UP:0"
    "spike-vector:DOWN:1"
    "firewall-decrypt:LEFT:2"
    "cipher-path:RIGHT:3"
    "exploit-sequencer:B:4"
    "breach-defense:A:5"
    "ghost-runner:START:6"
)

for GAME_INFO in "${GAMES[@]}"; do
    GAME_NAME="${GAME_INFO%%:*}"
    TEMP="${GAME_INFO#*:}"
    BUTTON="${TEMP%:*}"
    BUTTON_ID="${TEMP##*:}"

    echo ""
    echo "==================================================================="
    echo "Unlocking Konami Button: $BUTTON (Game: ${GAME_NAME^^})"
    echo "==================================================================="
    echo ""

    # Calculate NPC device ID
    NPC_ID=$((BUTTON_ID + 1))

    echo "Adding NPC for $GAME_NAME..."
    echo "add npc $GAME_NAME" >&3
    demo_sleep 1

    echo ""
    echo "Connecting to hunter..."
    echo "cable 0 $NPC_ID" >&3
    demo_sleep 2

    echo ""
    echo "Starting game (easy mode first)..."
    echo "b 0" >&3
    demo_sleep 1

    # Play easy mode quickly
    echo "Playing easy mode..."
    for i in {1..6}; do
        echo "b 0" >&3
        demo_sleep 0.2
    done
    demo_sleep 1

    echo ""
    echo "Easy mode complete. Disconnecting to trigger re-encounter..."
    echo "cable -d 0 $NPC_ID" >&3
    demo_sleep 1

    echo ""
    echo "Reconnecting for HARD MODE..."
    echo "cable 0 $NPC_ID" >&3
    demo_sleep 2

    echo ""
    echo "Device should show difficulty prompt. Selecting HARD MODE..."
    echo "l2 0" >&3
    demo_sleep 1

    echo ""
    echo "Playing HARD MODE for $GAME_NAME..."
    case "$GAME_NAME" in
        "ghost-runner"|"spike-vector"|"breach-defense")
            # Fast-paced games need more inputs
            for i in {1..14}; do
                echo "b 0" >&3
                demo_sleep 0.25
            done
            ;;
        *)
            # Other games
            for i in {1..10}; do
                echo "b 0" >&3
                demo_sleep 0.3
            done
            ;;
    esac

    demo_sleep 2

    echo ""
    echo "HARD MODE COMPLETE! Button unlocked: $BUTTON"
    echo "cable -d 0 $NPC_ID" >&3
    demo_sleep 1

    echo ""
    echo "Current Konami progress:"
    echo "konami 0" >&3
    demo_sleep 1

    COUNT=$((BUTTON_ID + 1))
    echo ""
    echo "Progress: $COUNT/7 buttons collected"
    demo_sleep 1

    if [ "$COUNT" -lt 7 ]; then
        echo ""
        echo "Moving to next game..."
        demo_sleep 1
    fi
done

echo ""
echo "==================================================================="
echo "ALL 7 KONAMI BUTTONS COLLECTED!"
echo "==================================================================="
echo ""

demo_sleep 1

echo "Visual Konami grid (all buttons should be marked with X):"
echo "progress 0" >&3
demo_sleep 3

echo ""
echo "Checking Konami completion status:"
echo "konami 0" >&3
demo_sleep 2

echo ""
echo "Player statistics (should show all 7 games beaten on hard):"
echo "stats 0" >&3
demo_sleep 3

echo ""
echo "Color profiles (all 7 should be unlocked):"
echo "colors 0" >&3
demo_sleep 3

echo ""
echo "=== Konami Code Complete! ==="
echo ""
echo "The player has now:"
echo "  ✓ Beaten all 7 minigames on HARD MODE"
echo "  ✓ Collected all 7 Konami buttons (↑↓←→BA START)"
echo "  ✓ Unlocked the Konami Boon (0x7F = 0111 1111 binary)"
echo "  ✓ Unlocked all color profiles"
echo ""
echo "What happens next on real hardware:"
echo "  • Player can enter the Konami code sequence"
echo "  • Unlocks special Konami Challenge mode"
echo "  • Access to exclusive rewards and features"
echo ""
echo "Konami code sequence: ↑ ↑ ↓ ↓ ← → ← → B A START"
echo ""

demo_sleep 3
echo "quit" >&3

wait "$CLI_PID" 2>/dev/null || true
