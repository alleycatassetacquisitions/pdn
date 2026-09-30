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
import os
import pathlib

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

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
