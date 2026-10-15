"""PlatformIO post-build step: signs firmware.bin with espsecure.

FIRMWARE_SIGNING_KEY names the RSA-3072 key (espsecure.py generate_signing_key
--version 2 --scheme rsa3072). A device takes an update only if it is signed with
the key that signed the image it runs and built for the same project (pdn or fdn),
so one key can sign both families. Without the variable the build stays unsigned:
fine to flash over USB, but such a device can neither seed nor take updates.
"""
import os
import subprocess

Import("env")  # noqa: F821  (injected by PlatformIO)

# The platform's own espsecure, run as its secure-boot signing step runs it: the
# platform does not install esptool as a module, and its esptool 4.x takes only
# the underscore command spellings.
ESPSECURE = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy") or "", "espsecure.py")  # noqa: F821


def _sign(target, source, env):
    del source
    image = str(target[0])
    key = os.environ.get("FIRMWARE_SIGNING_KEY")
    if not key:
        print(f"WARNING: FIRMWARE_SIGNING_KEY not set; {image} is unsigned.")
        return
    signed = image + ".signed"
    subprocess.check_call([env.subst("$PYTHONEXE"), ESPSECURE, "sign_data", "--version", "2",
                           "--keyfile", key, "--output", signed, image])
    os.replace(signed, image)


# Rebuilt every build, so setting FIRMWARE_SIGNING_KEY on an up-to-date tree still signs.
env.AlwaysBuild(env.File("$BUILD_DIR/${PROGNAME}.bin"))  # noqa: F821
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _sign)  # noqa: F821
