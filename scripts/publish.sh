#!/usr/bin/env bash
set -euo pipefail

fail() {
    echo "$1" >&2
    exit 1
}

cd "$(git rev-parse --show-toplevel)"
gh auth status >/dev/null 2>&1 || fail "gh is not signed in to GitHub, run gh auth login and then make publish again."
version=$(<VERSION)
git fetch --quiet origin main
sha=$(git rev-parse origin/main)
[[ $(git show "$sha:VERSION") == "$version" ]] || fail "origin/main does not hold VERSION $version yet, push it first."
[[ -z $(git ls-remote --tags origin "v$version") ]] || fail "v$version is already released."

run=""
for _ in $(seq 120); do
    run=$(gh run list --workflow ci.yml --event push --commit "$sha" --json databaseId --jq '.[0].databaseId' || true)
    [[ -n $run ]] && break
    sleep 5
done
[[ -n $run ]] || fail "No CI run started for $sha within 10 minutes, so we stop before the release."
gh run watch "$run" --exit-status || fail "CI failed or was cancelled on $sha, fix it, push and run make publish again."

if [[ -z ${NOTES:-} ]]; then
    notes=(--generate-notes)
elif [[ -f $NOTES ]]; then
    notes=(--notes-file "$NOTES")
else
    notes=(--notes "$NOTES")
fi
gh release create "v$version" --target "$sha" --title "v$version" "${notes[@]}"
