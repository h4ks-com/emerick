#!/bin/sh
case "${1-}" in
--script*) exec /src/build/dev/app/emerick --audio=null --midi=virtual "$@" ;;
esac
exec /src/build/dev/app/emerick --audio=null --midi=virtual --headless --mcp-port=8888 --mcp-bind-any "$@"
