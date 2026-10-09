#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
if [[ -n "$(git status --porcelain)" ]]; then
    echo 'Commit or stash your changes before merging upstream.' >&2
    exit 1
fi
git fetch https://github.com/throneproj/Throne.git dev
branch="qthrone/upstream-$(date -u +%Y%m%d-%H%M%S)"
git switch -c "$branch"
if ! git merge --no-edit FETCH_HEAD; then
    echo "Resolve the conflicts on $branch, or run git merge --abort." >&2
    exit 1
fi
echo "Upstream merged locally into $branch. Run tests/builds before merging it into your default branch."
