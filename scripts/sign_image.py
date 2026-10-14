"""PlatformIO post-build step: signs firmware.bin with espsecure.

FIRMWARE_SIGNING_KEY names the RSA-3072 key (espsecure generate-signing-key
--version 2 --scheme rsa3072). A device takes an update only if it is signed with
the key that signed the image it runs and built for the same project (pdn or fdn),
so one key can sign both families. Without the variable the build stays unsigned:
fine to flash over USB, but such a device can neither seed nor take updates.
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


# Rebuilt every build: it carries the build time devices order images by, and the
# relink it forces is what makes this signing step run on every build.
env.AlwaysBuild(env.File("$BUILD_DIR/esp_app_format/esp_app_desc.c.o"))  # noqa: F821
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _sign)  # noqa: F821
