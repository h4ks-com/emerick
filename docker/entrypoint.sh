#!/bin/sh
exec /src/build/dev/app/emerick --audio=null --midi=virtual --headless --mcp-port=8888 --mcp-bind-any "$@"
