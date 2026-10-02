"""Shared file I/O for the offline firmware-signing tools.

Centralizes the two ways this tooling can fail badly and silently:
  - A truncated or corrupted private-key file parsing as *some* valid P-256
    scalar instead of raising — the operator sees a signed image and a green
    build, and every device in the fleet rejects it while the evidence points
    at the fleet rather than at the short file.
  - A generated key or cert landing world-readable, or a root key generation
    silently clobbering an existing one.

It also holds the raw P-256 signing the device verifies against, and the
refusal to write key material inside the working tree.

Used by make_root_key.py, issue_signer_cert.py, and sign_firmware.py so
there is exactly one implementation of each, not one per script.
"""
import getpass
import os
import pathlib

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
from cryptography.hazmat.primitives.kdf.scrypt import Scrypt

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent


class SigningInputError(Exception):
    """A key, cert, or image file is missing, unreadable, or malformed.

    Raised instead of letting OSError/ValueError surface as a raw traceback;
    callers catch this at the top level and exit with just its message.
    """


def read_bytes_or_die(path, what):
    """Reads `path`, turning missing/unreadable into a clean diagnostic
    naming the path and what was expected there."""
    try:
        with open(path, "rb") as f:
            return f.read()
    except FileNotFoundError:
        raise SigningInputError(f"{what} not found: {path}") from None
    except IsADirectoryError:
        raise SigningInputError(f"{what} is a directory, not a file: {path}") from None
    except PermissionError:
        raise SigningInputError(f"{what} is unreadable (permission denied): {path}") from None
    except OSError as e:
        raise SigningInputError(f"{what} could not be read ({e.strerror}): {path}") from None


def load_private_key(raw_bytes, source=""):
    """Parses a raw 32-byte big-endian P-256 private scalar. Rejects any
    length other than 32 and any scalar outside [1, n-1] (the underlying
    library validates the range) rather than silently deriving whatever key
    the wrong-length bytes happen to produce."""
    label = f" ({source})" if source else ""
    if len(raw_bytes) != 32:
        raise SigningInputError(f"expected a 32-byte raw private key{label}, got {len(raw_bytes)} bytes")
    scalar = int.from_bytes(raw_bytes, "big")
    try:
        return ec.derive_private_key(scalar, ec.SECP256R1())
    except ValueError as e:
        raise SigningInputError(f"invalid P-256 private key{label}: {e}") from None


def load_private_key_from_file(path, what="private key"):
    return load_private_key(read_bytes_or_die(path, what), source=path)


def write_new_file(path, data):
    """Writes `data` to a brand-new file at `path`, mode 0600 from the moment
    it exists (no window where a broader umask makes it readable), and
    refuses to overwrite an existing file — silently clobbering an existing
    root key would be unrecoverable."""
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        raise SigningInputError(f"refusing to overwrite existing file: {path}") from None
    with os.fdopen(fd, "wb") as f:
        f.write(data)


def refuse_if_under_repo(path):
    """Refuses a destination inside the working tree. Key material committed
    by accident cannot be un-leaked, so the check is on the write, not on the
    operator remembering."""
    resolved = pathlib.Path(path).resolve()
    if resolved == REPO_ROOT or REPO_ROOT in resolved.parents:
        raise SystemExit(f"refusing to write key material under the repo: {resolved}")


def sign_raw(private_key, data):
    """Raw P-256 R||S over SHA-256(data) — never DER, matching verifyP256."""
    der_sig = private_key.sign(data, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der_sig)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")

# Fixed, non-secret salt. It only domain-separates, so the same passphrase cannot
# produce this fleet's root key and some unrelated system's. Changing it changes
# every derived root, so it is a constant, not a knob.
ROOT_KDF_SALT = b"pdn-firmware-root-v1"
# scrypt at n=2**20 costs roughly 1GB of RAM per guess, which is what makes a
# written-down passphrase a defensible root rather than a convenience.
ROOT_KDF_N = 2 ** 20
ROOT_KDF_R = 8
ROOT_KDF_P = 1
# Below this a passphrase is not carrying enough entropy to stand in for 32 random
# bytes. Eight diceware words land around 60 characters; this refuses the obviously
# too-short rather than pretending to measure entropy.
ROOT_PASSPHRASE_MIN_CHARS = 32

SECP256R1_ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551


def derive_root_private_key(passphrase):
    """Derive the root P-256 private key deterministically from a passphrase.

    The root then has no at-rest form: it is reconstructed when needed and
    discarded. What gets stored is the passphrase, which a personal password
    manager and a sheet of paper both handle well — unlike 32 random bytes that
    need a shared vault nobody has.
    """
    if not isinstance(passphrase, str) or not passphrase.strip():
        raise SigningInputError("empty passphrase")
    if len(passphrase) < ROOT_PASSPHRASE_MIN_CHARS:
        raise SigningInputError(
            f"passphrase is {len(passphrase)} characters; at least "
            f"{ROOT_PASSPHRASE_MIN_CHARS} are required. Use 8 diceware words."
        )
    kdf = Scrypt(salt=ROOT_KDF_SALT, length=32,
                 n=ROOT_KDF_N, r=ROOT_KDF_R, p=ROOT_KDF_P)
    raw = kdf.derive(passphrase.encode("utf-8"))
    # Map the KDF output onto [1, n-1]. Rejecting out-of-range would make the
    # derivation non-total; folding keeps it deterministic for every passphrase.
    scalar = (int.from_bytes(raw, "big") % (SECP256R1_ORDER - 1)) + 1
    return ec.derive_private_key(scalar, ec.SECP256R1())


def prompt_root_passphrase(confirm):
    """Read the root passphrase from the terminal, never from argv.

    argv lands in shell history and in every process listing on the machine, so a
    passphrase passed as a flag is a passphrase published locally. When confirm is
    set the phrase is entered twice: a typo yields a different-but-perfectly-valid
    root, which would otherwise surface much later as every device rejecting every
    image.
    """
    first = getpass.getpass("Root passphrase: ")
    if confirm:
        second = getpass.getpass("Repeat passphrase: ")
        if first != second:
            raise SigningInputError("passphrases do not match")
    return first
