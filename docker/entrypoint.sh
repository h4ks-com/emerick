#!/bin/sh
case "${1-}" in
"") exec emerick-service ;;
--script*) exec emerick --audio=null --midi=virtual "$@" ;;
app) shift; exec emerick --audio=null --midi=virtual --headless --mcp-port=8888 --mcp-bind-any "$@" ;;
esac
exec "$@"
