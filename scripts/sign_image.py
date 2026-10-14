"""PlatformIO post-build step: signs firmware.bin with espsecure, so IDF accepts it
as an update on devices whose running image was signed with the same key.

FIRMWARE_SIGNING_KEY names the RSA-3072 key (espsecure generate-signing-key
--version 2 --scheme rsa3072). PDN and FDN builds use different keys: a device
only trusts the keys that signed the image it runs, so neither can take the
other's image. Without the variable the build stays unsigned, which is fine to
flash over USB but cannot be distributed or receive updates.
"""
import os
import subprocess
import sys

Import("env")  # noqa: F821  (injected by PlatformIO)


def _sign(target, source, env):
    del source
    image = str(target[0])
    key = os.environ.get("FIRMWARE_SIGNING_KEY")
    if not key:
        print(f"WARNING: FIRMWARE_SIGNING_KEY not set; {image} is unsigned.")
        return
    signed = image + ".signed"
    subprocess.check_call([sys.executable, "-m", "espsecure", "sign-data", "--version", "2",
                           "--keyfile", key, "--output", signed, image])
    os.replace(signed, image)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _sign)  # noqa: F821
