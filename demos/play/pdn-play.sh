#!/usr/bin/env bash
# PDN Demo Player
# Helper script to launch the CLI simulator with demo files
# Usage: ./pdn-play.sh <demo-name> [device-count]

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Project paths
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CLI_BINARY="$PROJECT_ROOT/.pio/build/native_cli/program"
DEMOS_DIR="$PROJECT_ROOT/demos"

# Print colored message
print_msg() {
    local color=$1
    shift
    echo -e "${color}$@${NC}"
}

# Print usage
usage() {
    cat << EOF
$(print_msg "$BLUE" "PDN Demo Player")
Helper script to launch the CLI simulator with demo files

$(print_msg "$GREEN" "Usage:")
  $(basename "$0") <demo-name> [device-count]

$(print_msg "$GREEN" "Arguments:")
  demo-name       Name of the demo file (without .demo extension)
  device-count    Number of devices to spawn (default: auto-detect from demo)

$(print_msg "$GREEN" "Available Demos:")
  duel                        Two-player face-to-face quickdraw duel (~20s)
  fdn-quickstart              Quick FDN encounter with Signal Echo (~45s)
  full-progression            Complete all 7 FDN games, unlock Konami boon (~5min)
  all-games-showcase          Showcase all 7 minigames in standalone mode (~3min)
  3player-2bounty-1hunter     3-player game: 2 bounty + 1 hunter (~30s)
  3player-2hunter-1bounty     3-player game: 2 hunter + 1 bounty (~30s)
  all-fdn-bounty-walkthrough  Full FDN progression, all bounty allegiance (~5min)
  all-fdn-walkthrough         Full FDN progression, mixed allegiance (~5min)

$(print_msg "$GREEN" "Examples:")
  $(basename "$0") duel
  $(basename "$0") fdn-quickstart
  $(basename "$0") full-progression
  $(basename "$0") 3player-2bounty-1hunter 3

$(print_msg "$GREEN" "Notes:")
  - CLI binary must be built first: pio run -e native_cli
  - Some demos require specific device counts (auto-detected)
  - Press Ctrl+C to exit the simulator

EOF
    exit 0
}

# Check if CLI binary exists
check_binary() {
    if [[ ! -f "$CLI_BINARY" ]]; then
        print_msg "$RED" "Error: CLI binary not found at $CLI_BINARY"
        print_msg "$YELLOW" "Build it first with: pio run -e native_cli"
        exit 1
    fi
}

# Get device count from demo file (if specified in comments)
auto_detect_devices() {
    local demo_file=$1

    # Check for "Usage:" line with device count
    # Example: "# Usage: .pio/build/native_cli/program 2 --script ..."
    if grep -q "Usage:.*program [0-9]" "$demo_file"; then
        local count=$(grep "Usage:.*program [0-9]" "$demo_file" | grep -oP 'program \K[0-9]+' | head -1)
        echo "$count"
    else
        # Default to 1 device
        echo "1"
    fi
}

# Main execution
main() {
    # Show help if no arguments or -h/--help
    if [[ $# -eq 0 ]] || [[ "$1" == "-h" ]] || [[ "$1" == "--help" ]]; then
        usage
    fi

    local demo_name=$1
    local device_count=${2:-""}
    local demo_file="$DEMOS_DIR/${demo_name}.demo"

    # Check if demo file exists
    if [[ ! -f "$demo_file" ]]; then
        print_msg "$RED" "Error: Demo file not found: $demo_file"
        print_msg "$YELLOW" "Run '$(basename "$0")' without arguments to see available demos"
        exit 1
    fi

    # Check if binary exists
    check_binary

    # Auto-detect device count if not specified
    if [[ -z "$device_count" ]]; then
        device_count=$(auto_detect_devices "$demo_file")
    fi

    # Print info
    print_msg "$BLUE" "PDN Demo Player"
    echo "Demo: $demo_name"
    echo "File: $demo_file"
    echo "Devices: $device_count"
    echo "Binary: $CLI_BINARY"
    echo ""
    print_msg "$GREEN" "Starting demo... (Press Ctrl+C to exit)"
    echo ""

    # Launch CLI with demo script
    "$CLI_BINARY" "$device_count" --script "$demo_file"
}

# Run main
main "$@"
