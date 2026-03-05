# ESP32-S3 Multi-Device Flasher (non-interactive, parallel)
#
# Original implementation by Sofronio:
#   https://github.com/Sofronio/ESP32-S3-Auto-Flasher-Tool
#
# Adapted for this project: --build-dir, USB-JTAG detection (VID 0x303A / PID 0x1001),
# --clear-nvs, non-interactive "flash all identified devices" behaviour,
# and parallel flashing via ThreadPoolExecutor.

import os
import subprocess
import sys
import argparse
import signal
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed

ESPRESSIF_VID = 0x303A
ESP32S3_JTAG_PID = 0x1001

# Guards stdout so output lines from concurrent flashes don't interleave
_print_lock = threading.Lock()
# Set to True by the signal handler so threads can exit early
_abort = threading.Event()


def _print(port, *args, **kwargs):
    """Thread-safe print prefixed with the port name."""
    with _print_lock:
        print(f"[{port}]", *args, **kwargs)


def signal_handler(sig, frame):
    _abort.set()
    with _print_lock:
        print("\nAborting — waiting for in-progress flashes to finish...")


# ---------------------------------------------------------------------------
# Port detection
# ---------------------------------------------------------------------------

def list_com_ports_with_pyserial():
    """Return list of (port_name, description, vid, pid) for all COM ports."""
    try:
        import serial.tools.list_ports
    except ImportError:
        print("Error: pyserial not installed. Install it with: pip install pyserial")
        sys.exit(1)

    ports = []
    for p in serial.tools.list_ports.comports():
        port_name = p.device.upper()
        desc = p.description.upper() if p.description else ""
        ports.append((port_name, desc, p.vid, p.pid))
    return ports


def is_jtag_port(port_description, vid, pid):
    """True if the port looks like an ESP32-S3 USB-JTAG/Serial device."""
    if vid == ESPRESSIF_VID and pid == ESP32S3_JTAG_PID:
        return True
    if port_description:
        if "ESPRESSIF" in port_description or "JTAG" in port_description:
            return True
    return False


def test_esp32_on_port(port_name):
    """Probe the port with esptool chip-id. Returns (is_esp32: bool, chip_info: str)."""
    for subcmd in ("chip-id", "chip_id"):
        try:
            result = subprocess.run(
                [sys.executable, "-m", "esptool", "--port", port_name, subcmd],
                capture_output=True, text=True, timeout=2,
            )
            if result.returncode == 0 and "ESP32" in result.stdout:
                out = result.stdout
                chip = (
                    "ESP32-S3" if "ESP32-S3" in out
                    else "ESP32-C3" if "ESP32-C3" in out
                    else "ESP32-S2" if "ESP32-S2" in out
                    else "ESP32"
                )
                return True, chip
        except subprocess.TimeoutExpired:
            return False, "Timeout"
        except Exception as exc:
            return False, str(exc)[:30]
    return False, ""


# ---------------------------------------------------------------------------
# Flash helpers
# ---------------------------------------------------------------------------

NVS_OFFSET = 0x9000
NVS_SIZE = 0x5000  # 20 KB — matches default_8MB.csv


def _esptool(*args):
    """Return a subprocess arg list that invokes esptool via the current Python interpreter.

    Using sys.executable -m esptool avoids relying on an 'esptool' entry-point
    on PATH, which on Windows would resolve to esptool.py and trigger an
    "Open with" dialog if .py files have no shell association.
    """
    return [sys.executable, "-m", "esptool"] + list(args)


def _run_esptool(port_name, cmd, timeout):
    """Run an esptool command, capturing output and re-emitting it with port prefix.

    encoding="utf-8" is required — esptool outputs UTF-8 (e.g. █ progress bar
    characters) but the Windows default encoding (CP1252) mangles them.

    esptool uses \\r (not \\n) to rewrite the progress bar in place. We split
    each newline-delimited chunk on \\r and print only the last non-empty
    segment, which is the most recent progress update.
    """
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
    )
    try:
        for raw_line in proc.stdout:
            segments = [s for s in raw_line.split("\r") if s.strip()]
            if segments:
                _print(port_name, segments[-1].rstrip("\n"))
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        raise
    if proc.returncode != 0:
        raise subprocess.CalledProcessError(proc.returncode, cmd)


def erase_flash_if_needed(port_name, erase_flash):
    if not erase_flash:
        return True
    _print(port_name, "Erasing flash...")
    cmd = _esptool("--chip", "esp32s3", "--port", port_name, "--before", "usb-reset", "erase-flash")
    try:
        _run_esptool(port_name, cmd, timeout=30)
        import time
        time.sleep(2)
        return True
    except subprocess.TimeoutExpired:
        _print(port_name, "Erase timed out, continuing anyway...")
        return True
    except Exception as exc:
        _print(port_name, f"Erase failed: {exc}")
        return False


def clear_nvs_partition(port_name):
    """Erase the NVS region so the device starts with a blank key-value store."""
    _print(port_name, "Clearing NVS partition...")
    cmd = _esptool(
        "--chip", "esp32s3", "--port", port_name,
        "--before", "usb-reset", "--after", "hard-reset",
        "erase_region", str(NVS_OFFSET), str(NVS_SIZE),
    )
    try:
        _run_esptool(port_name, cmd, timeout=30)
        _print(port_name, "NVS cleared")
        return True
    except (subprocess.TimeoutExpired, subprocess.CalledProcessError) as exc:
        _print(port_name, f"NVS clear failed: {exc}")
        return False


def flash_to_port(port_name, port_desc, build_dir, chip_info="", erase_flash=False,
                  has_littlefs=False, clear_nvs=False):
    """Flash firmware (from build_dir) to a single port. Returns True on success."""
    if _abort.is_set():
        return False

    _print(port_name, f"Starting flash ({chip_info or port_desc})")

    if not erase_flash_if_needed(port_name, erase_flash):
        return False

    if _abort.is_set():
        return False

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")
    littlefs = os.path.join(build_dir, "littlefs.bin")

    cmd = _esptool(
        "--chip", "esp32s3", "--port", port_name, "--baud", "921600",
        "--before", "usb-reset", "--after", "hard-reset",
        "write-flash", "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "8MB",
        "0x0000", bootloader, "0x8000", partitions, "0x10000", firmware,
    )
    if has_littlefs:
        cmd += ["0x670000", littlefs]

    try:
        _run_esptool(port_name, cmd, timeout=60)
    except subprocess.TimeoutExpired:
        _print(port_name, "Flash timed out")
        return False
    except subprocess.CalledProcessError as exc:
        _print(port_name, f"Flash failed: {exc}")
        return False
    except KeyboardInterrupt:
        return False

    if clear_nvs:
        clear_nvs_partition(port_name)

    _print(port_name, "OK")
    return True


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    signal.signal(signal.SIGINT, signal_handler)

    parser = argparse.ArgumentParser(
        description="Flash all connected ESP32-S3 devices in parallel (non-interactive)."
    )
    parser.add_argument(
        "--build-dir",
        default=os.path.join(".pio", "build", "esp32-s3_release"),
        help="Directory containing built .bin files",
    )
    parser.add_argument("--erase", action="store_true", help="Erase entire flash before writing")
    parser.add_argument("--clear-nvs", action="store_true", help="Wipe NVS partition after each flash")
    args = parser.parse_args()

    try:
        import serial.tools.list_ports  # noqa: F401
    except ImportError:
        print("pyserial is required. Install it with: pip install pyserial")
        sys.exit(1)

    build_dir = args.build_dir
    required = ["bootloader.bin", "partitions.bin", "firmware.bin"]
    missing = [f for f in required if not os.path.exists(os.path.join(build_dir, f))]
    if missing:
        for f in missing:
            print(f"Error: Missing {os.path.join(build_dir, f)}")
        print("Run 'pio run -e esp32-s3_release' first to build.")
        sys.exit(1)

    has_littlefs = os.path.exists(os.path.join(build_dir, "littlefs.bin"))
    print("littlefs.bin found — filesystem will be flashed." if has_littlefs
          else "littlefs.bin not found — skipping filesystem partition.")

    print("Scanning for devices...")
    ports_info = list_com_ports_with_pyserial()
    if not ports_info:
        print("No COM ports detected.")
        sys.exit(1)

    jtag_ports = [
        (port, desc)
        for port, desc, vid, pid in ports_info
        if is_jtag_port(desc, vid, pid)
    ]
    if not jtag_ports:
        print("No ESP32-S3 USB-JTAG devices found.")
        print(f"Available: {', '.join(p[0] for p in ports_info)}")
        sys.exit(1)

    esp32_devices = []
    for port, desc in jtag_ports:
        is_esp32, chip_info = test_esp32_on_port(port)
        if is_esp32:
            esp32_devices.append((port, desc, chip_info))
        else:
            print(f"  {port}: not ESP32 or not in bootloader")

    if not esp32_devices:
        print("No ESP32 devices responded.")
        sys.exit(1)

    print(f"\nFlashing {len(esp32_devices)} device(s) in parallel...")

    success = 0
    with ThreadPoolExecutor(max_workers=len(esp32_devices)) as executor:
        futures = {
            executor.submit(
                flash_to_port,
                port, desc, build_dir, chip_info,
                args.erase, has_littlefs, args.clear_nvs,
            ): port
            for port, desc, chip_info in esp32_devices
        }
        for future in as_completed(futures):
            port = futures[future]
            try:
                if future.result():
                    success += 1
            except Exception as exc:
                _print(port, f"Unexpected error: {exc}")

    print(f"\nDone: {success}/{len(esp32_devices)} devices flashed successfully.")
    sys.exit(0 if success == len(esp32_devices) else 1)


if __name__ == "__main__":
    main()
