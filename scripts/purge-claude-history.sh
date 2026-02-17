#!/usr/bin/env bash
#
# purge-claude-history.sh — Remove CLAUDE.md, test/CLAUDE.md, docs/AGENT-REFERENCE.md
# from ALL git history using git-filter-repo.
#
# WHEN TO RUN: Between waves, when all open PRs are merged or can be rebased.
#              This is a DESTRUCTIVE operation — it rewrites every commit hash.
#
# PREREQUISITES:
#   - pip3 install git-filter-repo
#   - All agents IDLE (no in-flight work)
#   - All important branches pushed to origin
#   - PR #349 (NFS migration gitignore) merged first
#
# WHAT IT DOES:
#   1. Fresh-clones the repo (required by filter-repo)
#   2. Removes 3 files from ALL commits in ALL branches
#   3. Force-pushes rewritten main + all branches to origin
#
# AFTER RUNNING:
#   - Every commit hash changes (content preserved, just no CLAUDE files)
#   - All agent VMs must re-clone (old clones have diverged history)
#   - Open PRs must be recreated from rewritten branches
#   - ~119 empty commits get dropped (commits that ONLY touched these files)
#
# STATS (tested 2026-02-17):
#   - 542 commits → 423 after purge
#   - 93 KB of blob data removed from history
#   - 7 commits directly touched these files
#   - 119 empty commits dropped (merge commits, docs-only commits)
#
# Usage:
#   ./scripts/purge-claude-history.sh [--dry-run]
#

set -euo pipefail

REPO_URL="git@github.com:FinallyEve/pdn.git"
WORK_DIR="/tmp/pdn-history-purge-$$"
FILES_TO_PURGE=(
    "CLAUDE.md"
    "test/CLAUDE.md"
    "docs/AGENT-REFERENCE.md"
)
DRY_RUN=false

if [[ "${1:-}" == "--dry-run" ]]; then
    DRY_RUN=true
    echo "=== DRY RUN MODE — no changes will be pushed ==="
fi

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  PDN Git History Purge — Remove CLAUDE.md from all history  ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "Files to purge:"
for f in "${FILES_TO_PURGE[@]}"; do
    echo "  - $f"
done
echo ""

# --- Preflight checks ---

if ! command -v git-filter-repo &>/dev/null; then
    echo "ERROR: git-filter-repo not found. Install with: pip3 install git-filter-repo"
    exit 1
fi

echo "Step 1/5: Fresh clone from origin..."
rm -rf "$WORK_DIR"
git clone --no-local "$REPO_URL" "$WORK_DIR" 2>&1
cd "$WORK_DIR"

# Fetch all remote branches as local tracking branches
echo ""
echo "Step 2/5: Fetching all remote branches..."
git branch -r | grep -v HEAD | while read branch; do
    local_branch="${branch#origin/}"
    if [[ "$local_branch" != "main" ]]; then
        git branch --track "$local_branch" "$branch" 2>/dev/null || true
    fi
done
BRANCH_COUNT=$(git branch | wc -l)
echo "  Tracking $BRANCH_COUNT branches locally"

BEFORE_COUNT=$(git rev-list --count --all)
echo ""
echo "Step 3/5: Running git filter-repo..."
echo "  Commits before: $BEFORE_COUNT"

git filter-repo \
    --invert-paths \
    --path "CLAUDE.md" \
    --path "test/CLAUDE.md" \
    --path "docs/AGENT-REFERENCE.md" \
    --force

AFTER_COUNT=$(git rev-list --count --all)
DROPPED=$((BEFORE_COUNT - AFTER_COUNT))
echo "  Commits after:  $AFTER_COUNT"
echo "  Dropped:        $DROPPED empty commits"

echo ""
echo "Step 4/5: Verifying purge..."
REMAINING=$(git log --all --oneline -- CLAUDE.md test/CLAUDE.md docs/AGENT-REFERENCE.md | wc -l)
if [[ "$REMAINING" -ne 0 ]]; then
    echo "ERROR: $REMAINING references still found! Aborting."
    exit 1
fi
echo "  ✓ Zero references to purged files in any branch"

echo ""
if $DRY_RUN; then
    echo "Step 5/5: DRY RUN — skipping push."
    echo ""
    echo "Would force-push these branches:"
    git branch | head -20
    echo "  ... (and all others)"
else
    echo "Step 5/5: Force-pushing to origin..."
    echo ""
    echo "⚠️  THIS IS DESTRUCTIVE — all commit hashes will change."
    echo "    Press Ctrl+C within 10 seconds to abort."
    echo ""
    for i in $(seq 10 -1 1); do
        printf "\r    Pushing in %d... " "$i"
        sleep 1
    done
    echo ""

    # Re-add origin (filter-repo removes it)
    git remote add origin "$REPO_URL"

    # Push main first, then all other branches
    git push --force origin main
    git push --force --all origin
    git push --force --tags origin

    echo ""
    echo "✓ Force push complete."
fi

echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║                    POST-PURGE CHECKLIST                 ║"
echo "╠══════════════════════════════════════════════════════════╣"
echo "║ □ All agent VMs must re-clone the repo                 ║"
echo "║ □ Run: rm -rf ~/pdn && git clone $REPO_URL ~/pdn      ║"
echo "║ □ Open PRs need to be recreated from rewritten branches║"
echo "║ □ Update .agent-checkpoint.json on each VM             ║"
echo "║ □ Run tests on fresh clone: pio test -e native         ║"
echo "║ □ Verify NFS symlinks still work                       ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# Cleanup
cd /
rm -rf "$WORK_DIR"
echo "Cleaned up working directory."
echo "Done."
