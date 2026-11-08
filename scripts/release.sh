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

printf '%s\n' "$version" > VERSION
git commit --quiet -m "chore: bump version to $version" VERSION
git push --quiet origin main
sha=$(git rev-parse HEAD)

run=""
until [[ -n $run ]]; do
    sleep 5
    run=$(gh run list --workflow ci.yml --event push --commit "$sha" --json databaseId --jq '.[0].databaseId')
done
gh run watch "$run" --exit-status || fail "CI failed on $sha, so we stop before the release."

if [[ -z ${NOTES:-} ]]; then
    notes=(--generate-notes)
elif [[ -f $NOTES ]]; then
    notes=(--notes-file "$NOTES")
else
    notes=(--notes "$NOTES")
fi
gh release create "v$version" --target "$sha" --title "v$version" "${notes[@]}"
