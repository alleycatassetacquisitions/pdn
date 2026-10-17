"""PlatformIO post-build step: signs firmware.bin with espsecure.

Only builds whose sdkconfig enables signed apps (the PDN's, see sdkconfig.defaults.pdn)
are signed, and IDF refuses to start such a build unsigned. FIRMWARE_SIGNING_KEY names
the fleet's RSA-3072 key (espsecure.py generate_signing_key --version 2 --scheme
rsa3072). A device takes an update only if it is signed with the key that signed the
image it runs. Without the variable the build is signed with a key generated once per
machine, so a developer's devices boot and update each other but neither take the
fleet's images nor give theirs to the fleet.
"""
import os
import subprocess

Import("env")  # noqa: F821  (injected by PlatformIO)

# The platform's own espsecure, run as its secure-boot signing step runs it: the
# platform does not install esptool as a module, and its esptool 4.x takes only
# the underscore command spellings.
ESPSECURE = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy") or "", "espsecure.py")  # noqa: F821
MACHINE_KEY = os.path.join(os.path.expanduser("~"), ".config", "pdn", "dev-signing-key.pem")


def _signs_apps(env):
    sdkconfig = os.path.join(env["PROJECT_DIR"], "sdkconfig." + env["PIOENV"])
    with open(sdkconfig) as handle:
        return "CONFIG_SECURE_SIGNED_APPS=y\n" in handle.read()


def _key(env):
    key = os.environ.get("FIRMWARE_SIGNING_KEY")
    if key:
        return key
    if not os.path.isfile(MACHINE_KEY):
        os.makedirs(os.path.dirname(MACHINE_KEY), exist_ok=True)
        subprocess.check_call([env.subst("$PYTHONEXE"), ESPSECURE, "generate_signing_key", "--version", "2",
                               "--scheme", "rsa3072", MACHINE_KEY])
        os.chmod(MACHINE_KEY, 0o600)
    print(f"FIRMWARE_SIGNING_KEY not set; signing with this machine's key {MACHINE_KEY}.")
    return MACHINE_KEY


def _sign(target, source, env):
    del source
    if not _signs_apps(env):
        return
    image = str(target[0])
    signed = image + ".signed"
    subprocess.check_call([env.subst("$PYTHONEXE"), ESPSECURE, "sign_data", "--version", "2",
                           "--keyfile", _key(env), "--output", signed, image])
    os.replace(signed, image)


# Rebuilt every build, so changing FIRMWARE_SIGNING_KEY on an up-to-date tree still signs.
env.AlwaysBuild(env.File("$BUILD_DIR/${PROGNAME}.bin"))  # noqa: F821
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _sign)  # noqa: F821
