# PlatformIO pre-build hook for the device envs: embed the git commit hash for
# firmware identification (crash logs, etc.), and keep sdkconfig.defaults authoritative.

Import("env")  # noqa: F821

import hashlib
import os
import subprocess


def readCommitHash():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short=8", "HEAD"],
            text=True,
            cwd=env["PROJECT_DIR"],
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return "unknown"


def invalidateStaleSdkconfig():
    # sdkconfig.defaults only seeds a per-env sdkconfig that does not exist yet;
    # afterwards kconfgen layers the generated file back over defaults, so editing
    # defaults changes nothing and reports nothing. Nor does the edit prompt a
    # reconfigure: kconfig.cmake puts CMAKE_CONFIGURE_DEPENDS on the generated
    # sdkconfig, its header and confgen.py, and not on defaults. Deleting the
    # generated file is the only lever, since board_build.esp-idf.sdkconfig_path
    # is read through os.path.expandvars and so can never resolve $BUILD_DIR to
    # put it somewhere disposable.
    #
    # Keyed on a hash of defaults rather than its mtime: a defaults file restored
    # with its timestamp preserved (cp -p, tar extract) is older than the generated
    # file while differing in content, which an mtime check would wave through.
    defaults = os.path.join(env["PROJECT_DIR"], "sdkconfig.defaults")
    if not os.path.isfile(defaults):
        return
    generated = os.path.join(env["PROJECT_DIR"], "sdkconfig." + env["PIOENV"])
    stamp = os.path.join(env.subst("$BUILD_DIR"), "sdkconfig-defaults.sha1")

    with open(defaults, "rb") as handle:
        digest = hashlib.sha1(handle.read()).hexdigest()
    try:
        with open(stamp) as handle:
            if handle.read().strip() == digest:
                return
    except OSError:
        pass

    if os.path.isfile(generated):
        os.remove(generated)
        print("*** sdkconfig.defaults changed; dropped stale sdkconfig.%s ***"
              % env["PIOENV"])
    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    with open(stamp, "w") as handle:
        handle.write(digest)


invalidateStaleSdkconfig()

commitHash = readCommitHash()
env.Append(CPPDEFINES=[("FIRMWARE_COMMIT_HASH", '\\"' + commitHash + '\\"')])
