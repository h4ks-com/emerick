#!/bin/sh
exec /src/build/dev/app/emerick --audio=null --midi=virtual --mcp-port=7777 --mcp-bind-any "$@"
