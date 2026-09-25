# PlatformIO pre-build: embed git commit hash for firmware identification (crash logs, etc.).

Import("env")  # noqa: F821

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


commitHash = readCommitHash()
env.Append(CPPDEFINES=[("FIRMWARE_COMMIT_HASH", '\\"' + commitHash + '\\"')])
