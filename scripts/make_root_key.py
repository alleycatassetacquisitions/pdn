#!/usr/bin/env python3
"""Generates the offline firmware-signing root keypair.

Run this once, on an offline machine, to provision a production root. It
never touches the repo: the private half goes to whatever path the operator
names (outside the working tree), and the public half is printed as the C
array to hand-paste into firmware-root-key.hpp. This script does not modify
that header itself — custody of a production root is the operator's call,
not the tooling's.
"""
import argparse
import pathlib
import sys

from cryptography.hazmat.primitives.asymmetric import ec

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent


def refuse_if_under_repo(path):
    resolved = pathlib.Path(path).resolve()
    if resolved == REPO_ROOT or REPO_ROOT in resolved.parents:
        raise SystemExit(f"refusing to write key material under the repo: {resolved}")


def format_c_array(public_key_bytes):
    lines = []
    for i in range(0, len(public_key_bytes), 12):
        chunk = public_key_bytes[i : i + 12]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    body = "\n".join(lines)
    return f"constexpr uint8_t FIRMWARE_ROOT_PUBLIC_KEY[64] = {{\n{body}\n}};"


def generate_root_keypair():
    """Returns (private_key_raw_32_bytes, public_key_raw_64_bytes)."""
    private_key = ec.generate_private_key(ec.SECP256R1())
    numbers = private_key.private_numbers()
    public_numbers = numbers.public_numbers
    private_raw = numbers.private_value.to_bytes(32, "big")
    public_raw = public_numbers.x.to_bytes(32, "big") + public_numbers.y.to_bytes(32, "big")
    return private_raw, public_raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="path to write the 32-byte root private key (outside the repo)")
    args = parser.parse_args()

    refuse_if_under_repo(args.out)

    private_raw, public_raw = generate_root_keypair()

    out_path = pathlib.Path(args.out)
    out_path.write_bytes(private_raw)

    print(f"Root private key written to {out_path} — keep this OFFLINE, never commit it.\n")
    print("Paste into lib/core/include/device/firmware-root-key.hpp:\n")
    print(format_c_array(public_raw))
    return 0


if __name__ == "__main__":
    sys.exit(main())
