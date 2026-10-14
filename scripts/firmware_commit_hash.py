# PlatformIO pre-build: embed git commit hash for firmware identification (crash logs, etc.).

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
# image from an older one. GCC takes __DATE__/__TIME__ from SOURCE_DATE_EPOCH, in
# UTC, so every build is stamped with the time it ran on any host's clock zone;
# sign_image.py makes the file that carries the stamp recompile every build.
env["ENV"]["SOURCE_DATE_EPOCH"] = str(int(time.time()))
