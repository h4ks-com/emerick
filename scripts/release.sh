#!/usr/bin/env bash
set -euo pipefail

fail() {
    echo "$1" >&2
    exit 1
}

version=${1:-}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "Pass the version as x.y.z: make release VERSION=1.2.3"
cd "$(git rev-parse --show-toplevel)"
[[ $(git branch --show-current) == main ]] || fail "Switch to main first."
[[ -z $(git status --porcelain) ]] || fail "Commit or stash your changes first."
git fetch --quiet origin main
[[ $(git rev-parse HEAD) == $(git rev-parse origin/main) ]] || fail "Bring main level with origin/main first."
[[ -z $(git ls-remote --tags origin "v$version") ]] || fail "v$version is already released, pick a newer version."
current=$(<VERSION)
[[ $current != "$version" ]] || fail "VERSION already reads $version."
[[ $(printf '%s\n%s\n' "$current" "$version" | sort -V | tail -n 1) == "$version" ]] ||
    fail "$version is older than $current, pick a newer version."

printf '%s\n' "$version" > VERSION
git commit --quiet -m "chore: bump version to $version" VERSION
echo "Committed v$version. Push it with: git push origin main"
echo "Then run make publish to release it once CI is green."
