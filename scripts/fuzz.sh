#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

ENGINE="${1:-libfuzzer}"   # libfuzzer | afl | build
TARGET="${2:-all}"         # serial | wireless | match | all
DURATION="${3:-10}"        # seconds per target when running

case "$TARGET" in
    all)      TARGETS=(serial match wireless) ;;
    serial)   TARGETS=(serial) ;;
    match)    TARGETS=(match) ;;
    wireless) TARGETS=(wireless) ;;
    *)
        echo "Unknown target '$TARGET'. Expected: serial | match | wireless | all" >&2
        exit 1
        ;;
esac

BUILD_DIR=".pio/fuzz/bin"
CORPUS_DIR=".pio/fuzz/corpus"
SEEDS_DIR=".pio/fuzz/seeds"
AFL_OUT_DIR=".pio/fuzz/afl-out"
CRASHES_DIR=".pio/fuzz/crashes"
DICT_FILE=".pio/fuzz/pdn.dict"
ALLOWLIST_FILE=".pio/fuzz/allowlist.txt"
MAX_INPUT_LEN=256

mkdir -p "$BUILD_DIR" "$CORPUS_DIR" "$SEEDS_DIR" "$AFL_OUT_DIR" "$CRASHES_DIR"

ARDUINO_JSON_INC=".pio/libdeps/native/ArduinoJson/src"
if [[ ! -d "$ARDUINO_JSON_INC" ]]; then
    echo "Installing native dependencies via PlatformIO..."
    pio pkg install -e native
fi

COMMON_CXXFLAGS=(
    -std=c++17
    -DNATIVE_BUILD
    -D_GLIBCXX_DEBUG
    -g3
    -O1
    -fno-omit-frame-pointer
    -I lib/core/include
    -I lib/native-drivers/include
    -I test/fuzz
    -I src/pdn
    -I src
    -I "$ARDUINO_JSON_INC"
)

export ASAN_OPTIONS="abort_on_error=1:halt_on_error=1:symbolize=0:detect_stack_use_after_return=1:max_malloc_fill_size=$((1<<30))"
export UBSAN_OPTIONS="abort_on_error=1:halt_on_error=1:symbolize=0"

SERIAL_SRCS=(
    test/fuzz/fuzz-serial-frame-parser.cpp
    lib/core/src/device/serial-frame-parser.cpp
    lib/core/src/utils/simple-timer.cpp
    lib/core/src/utils/logger.cpp
)

MATCH_SRCS=(
    test/fuzz/fuzz-match-serialization.cpp
    lib/core/src/game/match.cpp
    lib/core/src/utils/UUID.cpp
    lib/core/src/utils/logger.cpp
)

WIRELESS_SRCS=(
    test/fuzz/fuzz-wireless-quickdraw.cpp
    lib/core/src/wireless/reliable-channel.cpp
    lib/core/src/wireless/reliable-transport.cpp
    lib/core/src/wireless/remote-player-manager.cpp
    lib/core/src/wireless/resender.cpp
    lib/core/src/wireless/symbol-wireless-manager.cpp
    lib/core/src/device/remote-device-coordinator.cpp
    lib/core/src/device/serial-frame-parser.cpp
    lib/core/src/device/device.cpp
    lib/core/src/device/light-manager.cpp
    lib/core/src/game/match.cpp
    lib/core/src/game/player.cpp
    lib/core/src/game/symbol.cpp
    lib/core/src/utils/UUID.cpp
    lib/core/src/utils/uuid-bytes.cpp
    lib/core/src/utils/simple-timer.cpp
    lib/core/src/utils/logger.cpp
    src/pdn/game/match-manager.cpp
    src/pdn/game/shootout-manager.cpp
)

generate_seeds_and_dict() {
    mkdir -p "$SEEDS_DIR/serial" "$SEEDS_DIR/match" "$SEEDS_DIR/wireless"

    # Valid HELLO frame seed: 0xAA 0x55 + OP_HELLO(0x00) + 14-byte payload + CRC16(0xDC48)
    printf '\xaa\x55\x00\x11\x22\x33\x44\x55\x66\x01\xde\xad\xbe\xef\x00\x01\x01\xdc\x48' \
        > "$SEEDS_DIR/serial/valid_hello.bin"

    printf '{"match_id":"12345678-abcd-ef01-2345-6789abcdef01","hunter":"0001","bounty":"0002","winner_is_hunter":true,"hunter_time":250,"bounty_time":400}' \
        > "$SEEDS_DIR/match/valid_match.json"

    printf '\x03\x02AAAA\x00\x00\x07\x01\x05\x02BBBB\x00\x00\x09\x02\x08' \
        > "$SEEDS_DIR/wireless/match_flow.bin"

    cat > "$DICT_FILE" << 'EOF'
kw_match_id="match_id"
kw_hunter="hunter"
kw_bounty="bounty"
kw_winner="winner_is_hunter"
kw_hunter_time="hunter_time"
kw_bounty_time="bounty_time"
kw_matches="matches"
kw_true="true"
kw_false="false"
kw_shootout="SHT-"
kw_preamble="\xaa\x55"
kw_hello="\xaa\x55\x00"
EOF

    # Instrument only project sources and harnesses, skipping ArduinoJson and STL.
    cat > "$ALLOWLIST_FILE" << 'EOF'
*lib/core/*
*src/pdn/*
*test/fuzz/*
EOF
}

build_target() {
    local cxx="$1"
    local suffix="$2"
    local name="$3"
    shift 3
    local srcs=("$@")

    local out="$BUILD_DIR/fuzz-${name}-${suffix}"
    echo "[build] Compiling $out with $cxx..."
    if [[ "$suffix" == "afl" ]]; then
        AFL_LLVM_ALLOWLIST="$REPO_ROOT/$ALLOWLIST_FILE" \
            "$cxx" "${COMMON_CXXFLAGS[@]}" -fsanitize=fuzzer,address,undefined "${srcs[@]}" -o "$out"
        echo "[build] Compiling CMPLOG binary ${out}-cmplog with $cxx..."
        AFL_LLVM_ALLOWLIST="$REPO_ROOT/$ALLOWLIST_FILE" AFL_LLVM_CMPLOG=1 \
            "$cxx" "${COMMON_CXXFLAGS[@]}" -fsanitize=fuzzer,address,undefined "${srcs[@]}" -o "${out}-cmplog"
    else
        "$cxx" "${COMMON_CXXFLAGS[@]}" -fsanitize=fuzzer,address,undefined "${srcs[@]}" -o "$out"
    fi
}

build_for_target_name() {
    local cxx="$1"
    local suffix="$2"
    local target_name="$3"

    case "$target_name" in
        serial)   build_target "$cxx" "$suffix" "serial" "${SERIAL_SRCS[@]}" ;;
        match)    build_target "$cxx" "$suffix" "match" "${MATCH_SRCS[@]}" ;;
        wireless) build_target "$cxx" "$suffix" "wireless" "${WIRELESS_SRCS[@]}" ;;
    esac
}

build_all_for_compiler() {
    local cxx="$1"
    local suffix="$2"

    for t in "${TARGETS[@]}"; do
        build_for_target_name "$cxx" "$suffix" "$t"
    done
}

run_libfuzzer_target() {
    local name="$1"
    local bin="$BUILD_DIR/fuzz-${name}-libfuzzer"
    local corpus="$CORPUS_DIR/$name"
    local seeds="$SEEDS_DIR/$name"
    mkdir -p "$corpus"

    echo "=== Running libFuzzer on [$name] for ${DURATION}s (max_len=${MAX_INPUT_LEN}) ==="
    "$bin" "$corpus" "$seeds" \
        -max_len="$MAX_INPUT_LEN" \
        -dict="$DICT_FILE" \
        -max_total_time="$DURATION" \
        -print_final_stats=1 \
        -artifact_prefix="$CRASHES_DIR/${name}-"
}

run_afl_target() {
    local name="$1"
    local bin="$BUILD_DIR/fuzz-${name}-afl"
    local seeds="$SEEDS_DIR/$name"
    local out="$AFL_OUT_DIR/$name"
    mkdir -p "$out"

    echo "=== Running AFL++ on [$name] for ${DURATION}s (max_len=${MAX_INPUT_LEN}, CMPLOG enabled) ==="
    AFL_AUTORESUME=1 AFL_SKIP_CPUFREQ=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
        afl-fuzz -V "$DURATION" -G "$MAX_INPUT_LEN" -c "${bin}-cmplog" -l 2AT -x "$DICT_FILE" -i "$seeds" -o "$out" -- "$bin"
}

generate_seeds_and_dict

case "$ENGINE" in
    libfuzzer)
        CLANG_BIN="${CLANG_CXX:-clang++-18}"
        build_all_for_compiler "$CLANG_BIN" "libfuzzer"
        if [[ "$DURATION" -gt 0 ]]; then
            for t in "${TARGETS[@]}"; do
                run_libfuzzer_target "$t"
            done
        fi
        ;;
    afl)
        build_all_for_compiler "afl-clang-fast++" "afl"
        if [[ "$DURATION" -gt 0 ]]; then
            for t in "${TARGETS[@]}"; do
                run_afl_target "$t"
            done
        fi
        ;;
    build)
        CLANG_BIN="${CLANG_CXX:-clang++-18}"
        build_all_for_compiler "$CLANG_BIN" "libfuzzer"
        build_all_for_compiler "afl-clang-fast++" "afl"
        ;;
    *)
        echo "Usage: $0 [libfuzzer|afl|build] [serial|match|wireless|all] [duration_seconds]"
        exit 1
        ;;
esac
