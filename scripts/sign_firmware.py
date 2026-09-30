"""Signs a built firmware.bin and appends the trailer the device reads.

Runs two ways:
  - As a PlatformIO post-build step (extra_scripts = post:scripts/sign_firmware.py):
    hooked onto firmware.bin via AddPostAction, reads the signer key/cert paths from
    FIRMWARE_SIGNER_KEY / FIRMWARE_SIGNER_CERT env vars. Neither set (the normal local
    build) -> prints a warning and leaves the build unsigned; the build still succeeds,
    since an unsigned image is still fine to flash over USB, it just can't seed onward.
  - As a standalone CLI: `sign_firmware.py firmware.bin --signer-key K --signer-cert C
    --device-type PDN|FDN [--out OUT | --in-place]`.

The signed span and trailer layout are pinned by the device's own structs, not chosen
here: FirmwareOfferPayload (peer-comms-types.hpp) signs imageSha256|imageLength|
chunkSize|chunkCount|deviceType (see firmware-verify.cpp's verifyOffer), and
FirmwareTrailer is SignerCert|imageSignature|imageLength|magic. chunkSize must be the
same SEED_CHUNK_SIZE the device recomputes at seed time (firmware-update-manager.cpp),
not a value chosen here, or every receiving device's own recomputation disagrees with
what got signed and every offer comes back BAD_SIGNATURE.
"""
import argparse
import os
import pathlib
import re
import struct
import sys

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

try:
    Import("env")  # noqa: F821  (PlatformIO injects this; SCons execs the script, so
    # __file__ below is undefined when this succeeds — read the project dir from env
    # instead.)
except NameError:
    env = None

REPO_ROOT = pathlib.Path(env["PROJECT_DIR"]) if env is not None else pathlib.Path(__file__).resolve().parent.parent

CERT_LENGTH = 149  # keyId[4] + generation:u8 + publicKey[64] + label[16] + rootSignature[64]


def _extract(pattern, path, what):
    text = path.read_text()
    match = re.search(pattern, text)
    if not match:
        raise RuntimeError(f"could not find {what} in {path}")
    return match.group(1)


def read_seed_chunk_size():
    path = REPO_ROOT / "lib/core/src/device/firmware-update-manager.cpp"
    return int(_extract(r"constexpr\s+uint16_t\s+SEED_CHUNK_SIZE\s*=\s*(\d+)\s*;", path, "SEED_CHUNK_SIZE"))


def read_trailer_magic():
    path = REPO_ROOT / "lib/core/include/device/drivers/peer-comms-types.hpp"
    value = _extract(
        r"constexpr\s+uint32_t\s+FIRMWARE_TRAILER_MAGIC\s*=\s*(0[xX][0-9A-Fa-f]+)\s*;", path, "FIRMWARE_TRAILER_MAGIC"
    )
    return int(value, 16)


def read_device_type(name):
    path = REPO_ROOT / "lib/core/include/device/device-type.hpp"
    return int(_extract(rf"\b{name}\s*=\s*(\d+)", path, f"DeviceType::{name}"))


def sign_raw(private_key, data):
    """Raw P-256 R||S over SHA-256(data) — never DER, matching verifyP256."""
    der_sig = private_key.sign(data, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der_sig)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def load_private_key(raw_32_bytes):
    return ec.derive_private_key(int.from_bytes(raw_32_bytes, "big"), ec.SECP256R1())


def signed_span(image_bytes, device_type, chunk_size=None):
    """The exact 41-byte span verifyOffer checks the image signature over:
    imageSha256|imageLength|chunkSize|chunkCount|deviceType, packed little-endian."""
    if chunk_size is None:
        chunk_size = read_seed_chunk_size()
    image_length = len(image_bytes)
    chunk_count = (image_length + chunk_size - 1) // chunk_size
    image_sha256 = hashes.Hash(hashes.SHA256())
    image_sha256.update(image_bytes)
    digest = image_sha256.finalize()
    span = struct.pack("<32sIHHB", digest, image_length, chunk_size, chunk_count, device_type)
    return span, digest, image_length


def build_trailer(image_bytes, device_type, signer_private_key, cert_bytes):
    if len(cert_bytes) != CERT_LENGTH:
        raise ValueError(f"cert is {len(cert_bytes)} bytes, expected {CERT_LENGTH}")
    span, _digest, image_length = signed_span(image_bytes, device_type)
    image_signature = sign_raw(signer_private_key, span)
    magic = read_trailer_magic()
    trailer = cert_bytes + image_signature + struct.pack("<II", image_length, magic)
    return trailer


def sign_file(firmware_bin_path, signer_key_path, cert_path, device_type):
    image_bytes = pathlib.Path(firmware_bin_path).read_bytes()
    signer_private_key = load_private_key(pathlib.Path(signer_key_path).read_bytes())
    cert_bytes = pathlib.Path(cert_path).read_bytes()
    return build_trailer(image_bytes, device_type, signer_private_key, cert_bytes)


# ---- PlatformIO post-build hook ----


def _pio_device_type_name(pioenv):
    return "FDN" if "fdn" in pioenv.lower() else "PDN"


def _pio_sign_action(target, source, env):
    del source
    bin_path = str(target[0])

    key_path = os.environ.get("FIRMWARE_SIGNER_KEY")
    cert_path = os.environ.get("FIRMWARE_SIGNER_CERT")
    if not key_path or not cert_path:
        print(
            "WARNING: FIRMWARE_SIGNER_KEY/FIRMWARE_SIGNER_CERT not set; "
            f"{bin_path} is UNSIGNED and cannot be seeded. This is fine for a USB flash."
        )
        return

    device_type_name = _pio_device_type_name(env["PIOENV"])
    device_type = read_device_type(device_type_name)
    trailer = sign_file(bin_path, key_path, cert_path, device_type)

    with open(bin_path, "ab") as f:
        f.write(trailer)
    print(f"Signed {bin_path}: appended {len(trailer)}-byte trailer, deviceType={device_type_name}")


if env is not None:
    # SCons build actions take (target, source, env), in that order.
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _pio_sign_action)


# ---- Standalone CLI ----


def _cli_main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("firmware_bin")
    parser.add_argument("--signer-key", required=True)
    parser.add_argument("--signer-cert", required=True)
    parser.add_argument("--device-type", required=True, choices=["PDN", "FDN"])
    out_group = parser.add_mutually_exclusive_group(required=True)
    out_group.add_argument("--in-place", action="store_true", help="append the trailer to firmware_bin")
    out_group.add_argument("--out", help="write image+trailer to this path, leaving firmware_bin untouched")
    args = parser.parse_args()

    device_type = read_device_type(args.device_type)
    trailer = sign_file(args.firmware_bin, args.signer_key, args.signer_cert, device_type)

    if args.in_place:
        with open(args.firmware_bin, "ab") as f:
            f.write(trailer)
        print(f"Appended {len(trailer)}-byte trailer to {args.firmware_bin}")
    else:
        image_bytes = pathlib.Path(args.firmware_bin).read_bytes()
        pathlib.Path(args.out).write_bytes(image_bytes + trailer)
        print(f"Wrote signed image to {args.out} ({len(image_bytes)} + {len(trailer)} bytes)")
    return 0


if env is None and __name__ == "__main__":
    sys.exit(_cli_main())
