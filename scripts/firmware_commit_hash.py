# PlatformIO pre-build: embed the git commit hash (crash logs) and stamp the build time in UTC (update ordering).

Import("env")  # noqa: F821

import subprocess
import time


def readCommitHash():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short=8", "HEAD"],
            text=True,
            cwd=env["PROJECT_DIR"],
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return "unknown"


commitHash = readCommitHash()
env.Append(CPPDEFINES=[("FIRMWARE_COMMIT_HASH", '\\"' + commitHash + '\\"')])

# SOURCE_DATE_EPOCH makes GCC stamp __DATE__/__TIME__ in UTC, which devices compare to order images.
# SCons ignores ENV, so the object carrying the stamp rebuilds every build.
env["ENV"]["SOURCE_DATE_EPOCH"] = str(int(time.time()))
env.AlwaysBuild(env.File("$BUILD_DIR/esp_app_format/esp_app_desc.c.o"))
