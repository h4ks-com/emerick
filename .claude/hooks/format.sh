#!/usr/bin/env bash
# PostToolUse: format the C++ file an agent just wrote, so reviews never see style noise.
file=$(jq -r '.tool_input.file_path // empty')
case "$file" in
    *.cpp|*.hpp) clang-format -i "$file" ;;
esac
exit 0
