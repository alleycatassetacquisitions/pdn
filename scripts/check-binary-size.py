#!/usr/bin/env python3
"""
Check binary size against baseline.
Warns if any section grows >5%.
Exits with code 1 if threshold exceeded.
"""

import sys
import json
import re

def parse_size_output(filename):
    """Parse size command output and return section sizes."""
    with open(filename, 'r') as f:
        lines = f.readlines()

    # Expected format:
    #    text    data     bss     dec     hex filename
    # 4094229   41232  157768 4293229  41826d .pio/build/native_cli/program

    for line in lines:
        line = line.strip()
        if line and not line.startswith('text'):
            parts = line.split()
            if len(parts) >= 4:
                return {
                    'text': int(parts[0]),
                    'data': int(parts[1]),
                    'bss': int(parts[2]),
                    'total': int(parts[3])
                }

    raise ValueError("Could not parse size output")

def main():
    if len(sys.argv) != 3:
        print("Usage: check-binary-size.py <baseline.json> <size-output.txt>")
        sys.exit(1)

    baseline_file = sys.argv[1]
    size_output_file = sys.argv[2]

    # Load baseline
    with open(baseline_file, 'r') as f:
        baseline = json.load(f)

    # Parse current sizes
    current = parse_size_output(size_output_file)

    # Check each section
    print("Binary Size Check:")
    print("-" * 60)
    print(f"{'Section':<10} {'Baseline':<12} {'Current':<12} {'Change':<12} {'Status'}")
    print("-" * 60)

    threshold = 0.05  # 5% growth threshold
    failed = False

    for section in ['text', 'data', 'bss', 'total']:
        baseline_val = baseline[section]
        current_val = current[section]
        change = current_val - baseline_val
        change_pct = (change / baseline_val * 100) if baseline_val > 0 else 0

        status = "✓ OK"
        if change_pct > (threshold * 100):
            status = "⚠ WARN"
            failed = True
        elif change_pct < 0:
            status = "✓ BETTER"

        print(f"{section:<10} {baseline_val:<12,} {current_val:<12,} {change:>+6,} ({change_pct:>+.1f}%)  {status}")

    print("-" * 60)

    if failed:
        print("\n❌ FAILED: Binary size grew by >5% in one or more sections")
        print("Consider refactoring to reduce code bloat.")
        sys.exit(1)
    else:
        print("\n✅ PASSED: Binary size within acceptable limits")
        sys.exit(0)

if __name__ == '__main__':
    main()
