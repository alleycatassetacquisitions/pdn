# PlatformIO extra_scripts: registers a "multi_flash" custom target.
#
# Inherited by all envs that extend env:esp32-s3_base.
#
# Usage (after building): flashes all identified devices, no prompts.
#   pio run -e esp32-s3_release -t multi_flash   (with --clear-nvs)
#   pio run -e esp32-s3_debug   -t multi_flash   (no extra args)
#
# Per-environment extra flags are configured via custom_multi_flash_args in
# platformio.ini. To also erase flash, append --erase at runtime:
#   $env:MULTI_FLASH_ARGS="--erase"; pio run -e esp32-s3_release -t multi_flash

Import("env")  # noqa: F821  (PlatformIO injects this)

import os
import subprocess
import sys


def _check_pyserial():
    try:
        import serial.tools.list_ports  # noqa: F401
    except ImportError:
        print(
            "\nERROR: pyserial is required for multi_flash but is not installed.\n"
            "Install it with:  pip install pyserial\n"
        )
        env.Exit(1)


def multi_flash(source, target, env):
    _check_pyserial()

    build_dir  = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    script     = os.path.join(project_dir, "scripts", "flash_multi.py")

    # Per-env flags from platformio.ini (e.g. --clear-nvs for release)
    ini_args_str = env.GetProjectOption("custom_multi_flash_args", default="")
    ini_args = ini_args_str.split() if ini_args_str.strip() else []

    # Runtime override — use MULTI_FLASH_ARGS to append flags like --erase
    runtime_args = os.environ.get("MULTI_FLASH_ARGS", "").split()

    extra_args = ini_args + runtime_args

    cmd = [sys.executable, script, "--build-dir", build_dir] + extra_args

    print(f"\nLaunching multi-device flasher (build dir: {build_dir})")
    print(f"Command: {' '.join(cmd)}\n")

    result = subprocess.run(cmd)
    if result.returncode not in (0, 130):  # 130 = Ctrl+C (SIGINT)
        env.Exit(result.returncode)


env.AddCustomTarget(
    name="multi_flash",
    # Depend on firmware.bin so PlatformIO builds the project first if needed.
    # env.subst() must be called here — os.path.join leaves $BUILD_DIR as a
    # literal string which causes Windows to show an "Open with" dialog at exit.
    dependencies=[os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")],
    actions=multi_flash,
    title="Flash multiple ESP32-S3 devices",
    description=(
        "Scan for connected ESP32-S3 boards and flash all identified devices (non-interactive). "
        "Per-env flags via custom_multi_flash_args in platformio.ini. "
        "MULTI_FLASH_ARGS env var to append runtime flags (e.g. --erase)."
    ),
)
