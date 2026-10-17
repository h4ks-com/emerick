#!/usr/bin/env bash
# Stop: when sources changed, the turn only ends once the build and the unit tests pass.
cd "$(git rev-parse --show-toplevel)" || exit 0
input=$(cat)
if [ "$(jq -r '.stop_hook_active' <<<"$input")" = "true" ]; then
    exit 0
fi
if [ -z "$(git status --porcelain -- '*.cpp' '*.hpp' '*.qml' '*CMakeLists.txt' '*.cmake')" ]; then
    exit 0
fi
if ! output=$(make test 2>&1); then
    echo "make test failed; fix it before finishing:" >&2
    tail -40 <<<"$output" >&2
    exit 2
fi
exit 0
