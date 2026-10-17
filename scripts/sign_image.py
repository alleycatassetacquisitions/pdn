"""PlatformIO post-build step: espsecure-signs firmware.bin when the sdkconfig enables signed apps.
FIRMWARE_SIGNING_KEY names the fleet's RSA-3072 key; unset, a per-machine key in ~/.config/pdn is
generated once, so a developer's devices update each other but never trade images with the fleet."""
import os
import subprocess

Import("env")  # noqa: F821  (injected by PlatformIO)

# The platform's espsecure.py: esptool is not importable here, and its 4.x CLI takes underscore commands only.
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


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _sign)  # noqa: F821
