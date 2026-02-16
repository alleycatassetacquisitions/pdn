#!/bin/bash
# Script to post technical documents to GitHub issues
# Run this after authenticating with: gh auth login

set -e

REPO="FinallyEve/pdn"

echo "Posting technical document to issue #144..."
gh issue comment 144 -R "$REPO" --body-file issue-144-technical-doc.md

echo "Updating labels for issue #144..."
gh issue edit 144 -R "$REPO" --remove-label "needs-triage" --add-label "tech-doc-ready,needs-review"

echo ""
echo "Posting technical document to issue #145..."
gh issue comment 145 -R "$REPO" --body-file issue-145-technical-doc.md

echo "Updating labels for issue #145..."
gh issue edit 145 -R "$REPO" --remove-label "needs-triage" --add-label "tech-doc-ready,needs-review"

echo ""
echo "✅ Both technical documents posted and labels updated!"
echo ""
echo "Summary:"
echo "  - Issue #144: Device destructor missing onStateDismounted() - CONFIRMED"
echo "  - Issue #145: KonamiPuzzle fixed, but Duel state has identical bug - CONFIRMED"
