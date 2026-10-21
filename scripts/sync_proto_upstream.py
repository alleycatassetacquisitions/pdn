# Refresh vendored .proto files from proto/upstream.lock.json.
#
# Usage (from repo root):
#   python scripts/sync_proto_upstream.py
#   python scripts/sync_proto_upstream.py --check   # exit 1 if local files differ

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


def projectRoot() -> Path:
    return Path(__file__).resolve().parent.parent


def githubHeaders(accept: str | None = None) -> dict[str, str]:
    headers = {"User-Agent": "pdn-sync-proto"}
    if accept:
        headers["Accept"] = accept
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return headers


def rawUrl(owner: str, name: str, commit: str, upstreamPath: str) -> str:
    path = upstreamPath.lstrip("/")
    return (
        f"https://raw.githubusercontent.com/{owner}/{name}/"
        f"{commit}/{path}"
    )


def fetchText(url: str) -> str:
    request = urllib.request.Request(url, headers={"User-Agent": "pdn-sync-proto"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read().decode("utf-8")


def resolveUpstreamCommit(
    owner: str, name: str, ref: str, upstreamPath: str
) -> str:
    """Resolve the latest commit at or before `ref` that touched `upstreamPath`."""
    if not ref or ref.startswith("-"):
        raise ValueError(f"invalid upstream ref: {ref!r}")
    path = urllib.parse.quote(upstreamPath.lstrip("/"))
    sha = urllib.parse.quote(ref)
    url = (
        f"https://api.github.com/repos/{owner}/{name}/commits"
        f"?path={path}&sha={sha}&per_page=1"
    )
    request = urllib.request.Request(
        url, headers=githubHeaders("application/vnd.github+json")
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        payload = json.loads(response.read().decode("utf-8"))
        if not isinstance(payload, list) or not payload:
            raise KeyError(f"no commits found for {upstreamPath} at {ref}")
        return payload[0]["sha"]


def loadLock(lockPath: Path) -> dict:
    with lockPath.open(encoding="utf-8") as handle:
        return json.load(handle)


def saveLock(lockPath: Path, lock: dict) -> None:
    lockPath.write_text(
        json.dumps(lock, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def syncArtifact(root: Path, artifact: dict, checkOnly: bool) -> list[str]:
    errors: list[str] = []
    repo = artifact["repository"]
    owner = repo["owner"]
    name = repo["name"]
    commit = repo["commit"]
    upstreamPath = artifact["upstream"]["path"]
    localPath = root / artifact["local"]["path"]
    url = rawUrl(owner, name, commit, upstreamPath)

    try:
        upstreamText = fetchText(url)
    except (urllib.error.URLError, TimeoutError) as exc:
        errors.append(f"{artifact['id']}: fetch failed ({url}): {exc}")
        return errors

    if not localPath.is_file():
        if checkOnly:
            errors.append(f"{artifact['id']}: missing local file {localPath}")
            return errors
        localPath.parent.mkdir(parents=True, exist_ok=True)
        localPath.write_text(upstreamText, encoding="utf-8", newline="\n")
        print(f"Wrote {localPath.relative_to(root)}")
        return errors

    localText = localPath.read_text(encoding="utf-8")
    if localText != upstreamText:
        if checkOnly:
            errors.append(
                f"{artifact['id']}: {localPath.relative_to(root)} "
                f"does not match {commit}"
            )
            return errors
        localPath.write_text(upstreamText, encoding="utf-8", newline="\n")
        print(f"Updated {localPath.relative_to(root)}")
    else:
        print(f"OK {localPath.relative_to(root)}")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify local files match the lock; do not write",
    )
    parser.add_argument(
        "--bump",
        nargs="?",
        const="HEAD",
        default=None,
        metavar="REF",
        help="Update lockfile commit to upstream REF (default: HEAD) before syncing",
    )
    parser.add_argument(
        "--generate",
        action="store_true",
        help="Run scripts/generate_proto.py after syncing",
    )
    args = parser.parse_args()

    root = projectRoot()
    lockPath = root / "lib" / "alleycat-server" / "proto" / "upstream.lock.json"
    if not lockPath.is_file():
        print(f"Missing lock file: {lockPath}", file=sys.stderr)
        return 1

    lock = loadLock(lockPath)
    if lock.get("lockfileVersion") != 1:
        print("Unsupported lockfileVersion", file=sys.stderr)
        return 1

    allErrors: list[str] = []
    lockChanged = False
    for artifact in lock.get("artifacts", []):
        if args.bump and not args.check:
            repo = artifact["repository"]
            upstreamPath = artifact["upstream"]["path"]
            try:
                resolved = resolveUpstreamCommit(
                    repo["owner"], repo["name"], args.bump, upstreamPath
                )
            except (urllib.error.URLError, TimeoutError, KeyError, ValueError) as exc:
                allErrors.append(f"{artifact['id']}: resolve {args.bump} failed: {exc}")
                continue
            if repo.get("commit") != resolved:
                repo["commit"] = resolved
                lockChanged = True
                print(f"Bumped {artifact['id']} -> {resolved}")
        allErrors.extend(syncArtifact(root, artifact, args.check))

    if lockChanged and not allErrors:
        saveLock(lockPath, lock)

    if allErrors:
        for message in allErrors:
            print(message, file=sys.stderr)
        return 1

    if args.generate and not args.check:
        genScript = root / "scripts" / "generate_proto.py"
        result = subprocess.run([sys.executable, str(genScript)])
        return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
