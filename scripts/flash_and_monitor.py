#!/usr/bin/env python3
"""
Flash all connected ESP32-S3 devices with the release_verbose firmware, then
stream serial logs from all of them concurrently to per-device files in
/tmp/pdn-logs/devN.log.

Usage:
  scripts/flash_and_monitor.py            # flash + monitor indefinitely
  scripts/flash_and_monitor.py --duration 60   # monitor 60s then exit
  scripts/flash_and_monitor.py --no-flash      # skip flashing, just monitor
  scripts/flash_and_monitor.py --env esp32-s3_pdn_debug   # different env
  scripts/flash_and_monitor.py --tail-grep "JACKDEAD|HELLO"   # only print matching lines

Defaults to the esp32-s3_pdn_release_verbose pio environment. Devices are
discovered as /dev/ttyACM* ports. Flash is sequential (pio holds a lock per
build dir, and the upload tool's --upload-port goes one at a time anyway).
Monitor runs reading-side, one thread per port, via pyserial. Lines are
prefixed with [devN] and time-of-host so concurrent streams are
interleaved-friendly.

Stop with Ctrl-C; pending writes are flushed.
"""

import argparse
import glob
import os
import re
import signal
import subprocess
import sys
import threading
import time

PIO_ENV_DEFAULT = "esp32-s3_pdn_release_verbose"
PIO_BIN = "/usr/bin/pio"
PYSERIAL_PYTHON = "/home/tirefire/.platformio/penv/bin/python"
LOG_DIR = "/tmp/pdn-logs"

_print_lock = threading.Lock()
_stop = threading.Event()


def emit(prefix, line):
    with _print_lock:
        sys.stdout.write(f"[{prefix}] {line}\n")
        sys.stdout.flush()


def discover_ports():
    ports = sorted(glob.glob("/dev/ttyACM*"))
    return ports


def build_firmware(env):
    emit("BUILD", f"pio run -e {env}")
    r = subprocess.run([PIO_BIN, "run", "-e", env],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout)
        emit("BUILD", "FAILED")
        sys.exit(r.returncode)
    last = [l for l in r.stdout.splitlines() if "SUCCESS" in l or "FAILED" in l]
    for l in last[-3:]:
        emit("BUILD", l.strip())


def flash_one(env, port):
    emit("FLASH", f"{port} starting")
    r = subprocess.run(
        [PIO_BIN, "run", "-e", env, "-t", "upload", "--upload-port", port],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    last_line = ""
    for l in r.stdout.splitlines():
        if "SUCCESS" in l or "FAILED" in l:
            last_line = l.strip()
    emit("FLASH", f"{port} {last_line or ('rc=' + str(r.returncode))}")
    return r.returncode == 0


def flash_all(env, ports):
    # multi_flash flashes all devices concurrently (~30s vs ~2min sequential);
    # it can fail on an older pio target or esptool missing from its python.
    emit("FLASH", f"multi_flash all ({len(ports)} devices)")
    r = subprocess.run(
        [PIO_BIN, "run", "-e", env, "-t", "multi_flash"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    success_line = ""
    for l in r.stdout.splitlines():
        if "succeeded" in l.lower() or "SUCCESS" in l:
            success_line = l.strip()
    if r.returncode == 0:
        emit("FLASH", f"multi_flash OK: {success_line}")
        return True
    emit("FLASH", "multi_flash failed, falling back to sequential")
    sys.stderr.write(r.stdout)
    ok = True
    for p in ports:
        if not flash_one(env, p):
            ok = False
    return ok


def reader_thread(port, idx, duration, tail_filter):
    # pyserial lives in the platformio venv; import inside the thread so the
    # script itself doesn't need pyserial on system Python.
    sys.path.insert(0, "/home/tirefire/.platformio/penv/lib/python3.13/site-packages")
    sys.path.insert(0, "/home/tirefire/.platformio/penv/lib/python3.12/site-packages")
    sys.path.insert(0, "/home/tirefire/.platformio/penv/lib/python3.11/site-packages")
    try:
        import serial  # noqa: E402
    except ImportError:
        run_reader_subprocess(port, idx, duration, tail_filter)
        return

    try:
        s = serial.Serial(port, 115200, timeout=1)
    except Exception as e:
        emit(f"dev{idx}", f"OPEN FAILED: {e}")
        return

    os.makedirs(LOG_DIR, exist_ok=True)
    log_path = f"{LOG_DIR}/dev{idx}.log"
    f = open(log_path, "wb")
    emit(f"dev{idx}", f"streaming {port} -> {log_path}")

    end_at = (time.time() + duration) if duration else None
    pat = re.compile(tail_filter) if tail_filter else None
    line_buf = b""
    try:
        while not _stop.is_set():
            if end_at and time.time() >= end_at:
                break
            data = s.read(4096)
            if not data:
                continue
            f.write(data)
            f.flush()
            line_buf += data
            # Emit per newline so prints stay readable.
            while b"\n" in line_buf:
                line, line_buf = line_buf.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace").rstrip()
                if not text:
                    continue
                if pat and not pat.search(text):
                    continue
                emit(f"dev{idx}", text)
    finally:
        f.close()
        try:
            s.close()
        except Exception:
            pass


def run_reader_subprocess(port, idx, duration, tail_filter):
    # Spawn the platformio venv python so pyserial is available.
    code = (
        "import sys, time, re, serial\n"
        f"s = serial.Serial({port!r}, 115200, timeout=1)\n"
        f"end = time.time() + {duration if duration else 0}\n"
        f"pat = re.compile({tail_filter!r}) if {tail_filter!r} else None\n"
        f"f = open({(LOG_DIR + '/dev' + str(idx) + '.log')!r}, 'wb')\n"
        "buf = b''\n"
        "while True:\n"
        "  if end and time.time() >= end: break\n"
        "  d = s.read(4096)\n"
        "  if not d: continue\n"
        "  f.write(d); f.flush()\n"
        "  buf += d\n"
        "  while b'\\n' in buf:\n"
        "    line, buf = buf.split(b'\\n', 1)\n"
        "    t = line.decode('utf-8', errors='replace').rstrip()\n"
        "    if not t: continue\n"
        "    if pat and not pat.search(t): continue\n"
        f"    print('[dev{idx}]', t, flush=True)\n"
    )
    subprocess.run([PYSERIAL_PYTHON, "-c", code])


def install_signal_handlers():
    def handler(sig, frame):
        emit("MAIN", "stopping (signal)")
        _stop.set()
    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--env", default=PIO_ENV_DEFAULT,
                    help="platformio env name (default: esp32-s3_pdn_release_verbose)")
    ap.add_argument("--duration", type=int, default=0,
                    help="seconds to monitor (0 = until Ctrl-C)")
    ap.add_argument("--no-flash", action="store_true",
                    help="skip building/flashing, only monitor")
    ap.add_argument("--no-build", action="store_true",
                    help="skip building (use last firmware.bin); still flashes")
    ap.add_argument("--tail-grep", default="",
                    help="only print lines matching this regex (still writes full log)")
    args = ap.parse_args()

    install_signal_handlers()

    ports = discover_ports()
    if not ports:
        emit("MAIN", "no /dev/ttyACM* devices found")
        return 1
    emit("MAIN", f"ports: {ports}")

    if not args.no_flash:
        if not args.no_build:
            build_firmware(args.env)
        if not flash_all(args.env, ports):
            emit("MAIN", "flash failed; aborting before monitor")
            return 1

    os.makedirs(LOG_DIR, exist_ok=True)
    # Truncate prior logs so each run starts clean.
    for i in range(len(ports)):
        try:
            open(f"{LOG_DIR}/dev{i}.log", "wb").close()
        except Exception:
            pass

    threads = []
    for i, p in enumerate(ports):
        t = threading.Thread(
            target=reader_thread,
            args=(p, i, args.duration, args.tail_grep),
            daemon=True,
        )
        t.start()
        threads.append(t)

    if args.duration:
        end = time.time() + args.duration
        while time.time() < end and not _stop.is_set():
            time.sleep(0.5)
        _stop.set()
    else:
        try:
            while not _stop.is_set():
                time.sleep(0.5)
        except KeyboardInterrupt:
            _stop.set()

    for t in threads:
        t.join(timeout=2)
    emit("MAIN", f"logs in {LOG_DIR}/dev[0-{len(ports)-1}].log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
