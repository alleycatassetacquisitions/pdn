# Generate nanopb C sources from proto/alleycat/*.proto.
#
# Prerequisites (one-time, host-only):
#   pip install grpcio-tools
#
# Usage (from repo root):
#   python scripts/generate_proto.py
#
# Output:
#   lib/alleycat-server/src/<name>.pb.c
#   lib/alleycat-server/include/alleycat-server/<name>.pb.h
#
# Re-run after any change to proto/alleycat/*.proto or *.options.
# Commit the generated files alongside the schema changes.

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def projectRoot() -> Path:
    return Path(__file__).resolve().parent.parent


def findNanopbGenerator(root: Path) -> Path:
    """Locate nanopb_generator.py inside .pio/libdeps (any env will do)."""
    pattern = ".pio/libdeps/*/Nanopb/generator/nanopb_generator.py"
    candidates = sorted(root.glob(pattern))
    if not candidates:
        raise FileNotFoundError(
            "nanopb_generator.py not found. Run 'pio run -e esp32-s3_pdn_release' "
            "first to populate .pio/libdeps."
        )
    return candidates[0]


def generate(protoFile: Path, optionsFile: Path | None, generatorScript: Path,
             outSrcDir: Path, outIncludeDir: Path) -> None:
    with tempfile.TemporaryDirectory() as tmpStr:
        tmpDir = Path(tmpStr)

        # Copy options file next to the proto so the generator finds it by name.
        if optionsFile and optionsFile.is_file():
            shutil.copy(optionsFile, tmpDir / optionsFile.name)

        cmd = [
            sys.executable,
            str(generatorScript),
            "--output-dir", str(tmpDir),
            "--proto-path", str(protoFile.parent),
            protoFile.name,
        ]

        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stdout)
            print(result.stderr, file=sys.stderr)
            raise RuntimeError(f"nanopb_generator failed for {protoFile.name}")

        stem = protoFile.stem
        pbC = tmpDir / f"{stem}.pb.c"
        pbH = tmpDir / f"{stem}.pb.h"

        if not pbC.is_file() or not pbH.is_file():
            raise RuntimeError(
                f"Expected {stem}.pb.c and {stem}.pb.h but generator output was: "
                + ", ".join(p.name for p in tmpDir.iterdir())
            )

        outSrcDir.mkdir(parents=True, exist_ok=True)
        outIncludeDir.mkdir(parents=True, exist_ok=True)

        shutil.copy(pbC, outSrcDir / pbC.name)
        shutil.copy(pbH, outIncludeDir / pbH.name)
        print(f"  {outSrcDir.relative_to(outSrcDir.parent.parent.parent)}/{pbC.name}")
        print(f"  {outIncludeDir.relative_to(outIncludeDir.parent.parent.parent)}/{pbH.name}")


def main() -> int:
    root = projectRoot()
    generator = findNanopbGenerator(root)

    protoDir = root / "lib" / "alleycat-server" / "proto"
    outSrcDir = root / "lib" / "alleycat-server" / "src"
    outIncludeDir = root / "lib" / "alleycat-server" / "include" / "alleycat-server"

    protoFiles = sorted(protoDir.glob("*.proto"))
    if not protoFiles:
        print("No .proto files found in proto/alleycat/", file=sys.stderr)
        return 1

    print(f"Generator: {generator.relative_to(root)}")
    for protoFile in protoFiles:
        optionsFile = protoFile.with_suffix(".options")
        print(f"Generating {protoFile.name}...")
        try:
            generate(protoFile, optionsFile, generator, outSrcDir, outIncludeDir)
        except (RuntimeError, FileNotFoundError) as exc:
            print(f"Error: {exc}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
