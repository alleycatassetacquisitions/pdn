#!/usr/bin/env python3
"""
Check code duplication against baseline.
Fails if new duplication exceeds baseline by >10 tokens.
"""

import sys
import json
import re

def parse_cpd_output(filename):
    """Parse CPD text output and return token/block counts."""
    with open(filename, 'r') as f:
        content = f.read()

    # Find all "Found a X line (Y tokens) duplication" lines
    pattern = r"Found a \d+ line \((\d+) tokens\) duplication"
    matches = re.findall(pattern, content)

    if not matches:
        return {
            'total_duplicate_tokens': 0,
            'total_duplicate_blocks': 0
        }

    # Convert to integers and sum
    token_counts = [int(m) for m in matches]
    return {
        'total_duplicate_tokens': sum(token_counts),
        'total_duplicate_blocks': len(token_counts)
    }

def main():
    if len(sys.argv) != 3:
        print("Usage: check-duplication.py <baseline.json> <cpd-output.txt>")
        sys.exit(1)

    baseline_file = sys.argv[1]
    cpd_output_file = sys.argv[2]

    # Load baseline
    with open(baseline_file, 'r') as f:
        baseline = json.load(f)

    # Parse current CPD output
    current = parse_cpd_output(cpd_output_file)

    # Check duplication
    print("Code Duplication Check:")
    print("-" * 60)
    print(f"{'Metric':<30} {'Baseline':<12} {'Current':<12} {'Change'}")
    print("-" * 60)

    baseline_tokens = baseline['total_duplicate_tokens']
    current_tokens = current['total_duplicate_tokens']
    token_change = current_tokens - baseline_tokens

    baseline_blocks = baseline['total_duplicate_blocks']
    current_blocks = current['total_duplicate_blocks']
    block_change = current_blocks - baseline_blocks

    print(f"{'Duplicate Tokens':<30} {baseline_tokens:<12,} {current_tokens:<12,} {token_change:>+6,}")
    print(f"{'Duplicate Blocks':<30} {baseline_blocks:<12,} {current_blocks:<12,} {block_change:>+6,}")
    print("-" * 60)

    # Threshold: fail if duplication increases by >10 tokens
    threshold = 10

    if token_change > threshold:
        print(f"\n❌ FAILED: Duplication increased by {token_change} tokens (threshold: {threshold})")
        print("Consider refactoring duplicated code into shared functions.")
        sys.exit(1)
    elif token_change < 0:
        print(f"\n✅ EXCELLENT: Duplication reduced by {abs(token_change)} tokens!")
        sys.exit(0)
    else:
        print(f"\n✅ PASSED: Duplication within acceptable limits")
        sys.exit(0)

if __name__ == '__main__':
    main()
