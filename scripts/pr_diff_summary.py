#!/usr/bin/env python3
"""Markdown PR diff summary grouped by repository area.

Reads BASE_SHA and HEAD_SHA from the environment (three-dot diff: base...head).
Prints a marked comment body to stdout for GitHub Actions.
"""
import hashlib
import os
import subprocess
import sys

MARKER = "<!-- pr-diff-summary -->"
HASH_MARKER_PREFIX = "<!-- pr-diff-summary-sha:"

BUCKETS = (
    ("lib/core/", "lib/core/"),
    ("lib/alleycat-server/", "lib/alleycat-server/"),
    ("src/pdn/", "src/pdn/"),
    ("src/fdn/", "src/fdn/"),
    ("src/cli/", "src/cli/"),
    ("openspec/", "openspec/"),
    ("test/", "test/"),
)


def bucket_for(path: str) -> str:
    normalized = path.replace("\\", "/")
    for label, prefix in BUCKETS:
        if normalized.startswith(prefix):
            return label
    return "Everything else"


def git_numstat(base: str, head: str) -> list[tuple[int, int, str]]:
    proc = subprocess.run(
        ["git", "diff", f"{base}...{head}", "--numstat", "--find-renames"],
        capture_output=True,
        text=True,
        check=True,
    )
    rows: list[tuple[int, int, str]] = []
    for line in proc.stdout.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) != 3:
            continue
        added, deleted, path = parts
        if added == "-" or deleted == "-":
            continue
        rows.append((int(added), int(deleted), path))
    return rows


def aggregate(rows: list[tuple[int, int, str]]) -> dict[str, list[int]]:
    totals: dict[str, list[int]] = {label: [0, 0] for label, _ in BUCKETS}
    totals["Everything else"] = [0, 0]
    for added, deleted, path in rows:
        label = bucket_for(path)
        totals[label][0] += added
        totals[label][1] += deleted
    return totals


def table_markdown(totals: dict[str, list[int]], base: str, head: str) -> str:
    order = [label for label, _ in BUCKETS] + ["Everything else"]
    lines = [
        MARKER,
        "### PR diff by area",
        "",
        f"Compare `{base[:7]}...` → `{head[:7]}` (merge base three-dot diff).",
        "",
        "| Area | + | − | Net |",
        "| --- | ---: | ---: | ---: |",
    ]
    sum_add = sum_del = 0
    any_row = False
    for label in order:
        added, deleted = totals[label]
        if added == 0 and deleted == 0:
            continue
        any_row = True
        net = added - deleted
        sum_add += added
        sum_del += deleted
        lines.append(f"| `{label}` | {added} | {deleted} | {net:+d} |")
    if not any_row:
        lines.append("| _(no changed files)_ | 0 | 0 | +0 |")
    else:
        lines.append(f"| **Total** | **{sum_add}** | **{sum_del}** | **{sum_add - sum_del:+d}** |")
    lines.append("")
    lines.append(
        "_Line counts from `git diff --numstat` (includes comments and specs in each bucket)._"
    )
    return "\n".join(lines)


def main() -> int:
    base = os.environ.get("BASE_SHA", "").strip()
    head = os.environ.get("HEAD_SHA", "").strip()
    if not base or not head:
        print("BASE_SHA and HEAD_SHA must be set", file=sys.stderr)
        return 1
    rows = git_numstat(base, head)
    totals = aggregate(rows)
    body = table_markdown(totals, base, head)
    content_hash = hashlib.sha256(body.encode("utf-8")).hexdigest()[:16]
    print(body)
    print(f"{HASH_MARKER_PREFIX}{content_hash} -->")
    return 0


if __name__ == "__main__":
    sys.exit(main())
