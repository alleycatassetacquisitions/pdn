#!/usr/bin/env python3
"""Issues a signer certificate delegated from the offline root key.

Run this offline, once per signer. It takes the root's private key (from
make_root_key.py) and a label, generates a fresh signer keypair, and writes
out the 149-byte SignerCert plus the signer's private key. The root key
itself never leaves the machine this runs on — only the cert and signer key
travel to wherever sign_firmware.py runs (including CI).

SignerCert layout (packed, little-endian), matching
lib/core/include/device/drivers/peer-comms-types.hpp:
    keyId[4] | generation:u8 | publicKey[64] | label[16] | rootSignature[64]
rootSignature covers keyId|generation|publicKey|label (the first 85 bytes),
matching what firmware-verify.cpp's verifyOffer checks the root signature
over: offsetof(SignerCert, rootSignature).
"""
import argparse
import pathlib
import struct
import sys

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))
from firmware_signing_io import SigningInputError, load_private_key_from_file, write_new_file  # noqa: E402

KEY_ID_LENGTH = 4
PUBLIC_KEY_LENGTH = 64
LABEL_LENGTH = 16
SIG_LENGTH = 64
UNSIGNED_CERT_LENGTH = KEY_ID_LENGTH + 1 + PUBLIC_KEY_LENGTH + LABEL_LENGTH  # 85
CERT_LENGTH = UNSIGNED_CERT_LENGTH + SIG_LENGTH  # 149


def refuse_if_under_repo(path):
    resolved = pathlib.Path(path).resolve()
    if resolved == REPO_ROOT or REPO_ROOT in resolved.parents:
        raise SystemExit(f"refusing to write key material under the repo: {resolved}")


def public_key_raw(private_key):
    numbers = private_key.public_key().public_numbers()
    return numbers.x.to_bytes(32, "big") + numbers.y.to_bytes(32, "big")


def sign_raw(private_key, data):
    """Raw P-256 R||S over SHA-256(data) — never DER, matching verifyP256."""
    der_sig = private_key.sign(data, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der_sig)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def verify_raw(public_key_raw_bytes, data, signature):
    numbers = ec.EllipticCurvePublicNumbers(
        int.from_bytes(public_key_raw_bytes[:32], "big"),
        int.from_bytes(public_key_raw_bytes[32:], "big"),
        ec.SECP256R1(),
    )
    public_key = numbers.public_key()
    der_sig = encode_dss_signature(int.from_bytes(signature[:32], "big"), int.from_bytes(signature[32:], "big"))
    public_key.verify(der_sig, data, ec.ECDSA(hashes.SHA256()))


def build_cert(root_private_key, signer_public_key_raw, key_id, generation, label):
    label_bytes = label.encode("ascii")
    if len(label_bytes) > LABEL_LENGTH:
        raise SystemExit(f"label {label!r} is {len(label_bytes)} bytes, exceeds {LABEL_LENGTH}")
    unsigned = struct.pack(
        f"<{KEY_ID_LENGTH}sB{PUBLIC_KEY_LENGTH}s{LABEL_LENGTH}s",
        key_id.to_bytes(KEY_ID_LENGTH, "big"),
        generation,
        signer_public_key_raw,
        label_bytes.ljust(LABEL_LENGTH, b"\x00"),
    )
    assert len(unsigned) == UNSIGNED_CERT_LENGTH
    root_signature = sign_raw(root_private_key, unsigned)
    cert = unsigned + root_signature
    assert len(cert) == CERT_LENGTH
    return cert


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root-key", required=True, help="path to the root's 32-byte raw private key")
    parser.add_argument("--label", required=True, help="signer label, at most 16 ASCII bytes")
    parser.add_argument("--generation", type=int, default=1, help="cert generation counter (default 1)")
    parser.add_argument("--key-id", type=lambda s: int(s, 0), default=1, help="4-byte key identifier (default 1)")
    parser.add_argument("--out-cert", required=True, help="path to write the 149-byte SignerCert (outside the repo)")
    parser.add_argument("--out-key", required=True, help="path to write the signer's private key (outside the repo)")
    args = parser.parse_args()

    refuse_if_under_repo(args.out_cert)
    refuse_if_under_repo(args.out_key)

    try:
        root_private_key = load_private_key_from_file(args.root_key, what="root private key")
    except SigningInputError as e:
        sys.exit(f"error: {e}")

    signer_private_key = ec.generate_private_key(ec.SECP256R1())
    signer_public_raw = public_key_raw(signer_private_key)
    signer_private_raw = signer_private_key.private_numbers().private_value.to_bytes(32, "big")

    cert = build_cert(root_private_key, signer_public_raw, args.key_id, args.generation, args.label)

    # A cert that doesn't verify against its own root is useless to write out; catch it here
    # rather than downstream where the symptom looks like a device problem.
    root_public_raw = public_key_raw(root_private_key)
    verify_raw(root_public_raw, cert[:UNSIGNED_CERT_LENGTH], cert[UNSIGNED_CERT_LENGTH:])

    try:
        write_new_file(args.out_cert, cert)
        write_new_file(args.out_key, signer_private_raw)
    except SigningInputError as e:
        sys.exit(f"error: {e}")

    print(f"Signer cert written to {args.out_cert} ({len(cert)} bytes), key to {args.out_key}.")
    print(f"keyId=0x{args.key_id:08x} generation={args.generation} label={args.label!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
