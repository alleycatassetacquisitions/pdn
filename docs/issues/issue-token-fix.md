# GitHub Issue: chore: fix PAT token lifetime for GitHub API access

**Labels:** process, fleet

---

## Problem

The current fine-grained personal access token (PAT) for `FinallyEve` has a lifetime > 366 days. The upstream `alleycatassetacquisitions` organization enforces a policy that **forbids access via fine-grained PATs with lifetime > 366 days**.

This blocks ALL write operations on `FinallyEve/pdn` (the fork):
- Creating issues
- Creating/updating PRs
- Commenting on issues/PRs
- Any mutation via the GitHub API or `gh` CLI

**Error message:**
```
GraphQL: The 'alleycatassetacquisitions' organization forbids access via a fine-grained
personal access tokens if the token's lifetime is greater than 366 days.
Please adjust your token's lifetime at the following URL:
https://github.com/settings/personal-access-tokens/11517743 (repository.parent)
```

## Fix

1. Go to https://github.com/settings/personal-access-tokens/11517743
2. Regenerate the token with a lifetime of **≤ 365 days**
3. Update the token in the agent environment: `gh auth login`

## Impact

Without this fix, Agent 01 and the entire fleet cannot:
- File issues for tracked work
- Create PRs for code changes
- Participate in code review workflows
- Automate any GitHub operations

This is blocking multiple pending tasks including issue creation for the refactoring initiative.
