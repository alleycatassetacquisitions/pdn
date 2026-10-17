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

# The image's build time (esp_app_desc_t date/time) is how a device tells a newer
# image from an older one. With SOURCE_DATE_EPOCH set, GCC stamps __DATE__ and
# __TIME__ in UTC, so images built in different time zones still compare
# correctly. SCons does not track ENV, so the file that carries the stamp is
# rebuilt every build.
env["ENV"]["SOURCE_DATE_EPOCH"] = str(int(time.time()))
env.AlwaysBuild(env.File("$BUILD_DIR/esp_app_format/esp_app_desc.c.o"))
