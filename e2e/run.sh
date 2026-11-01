#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
NAME=emerick-e2e
SERVICE=emerick-e2e-service
EMERICK_E2E_OUT=$(mktemp -d)
export EMERICK_E2E_OUT
chmod 777 "$EMERICK_E2E_OUT"
docker rm -f $NAME $SERVICE >/dev/null 2>&1 || true
docker run -d --name $NAME -p 127.0.0.1:8888:8888 -v "$EMERICK_E2E_OUT:/e2e-out" emerick-dev app >/dev/null
docker run -d --name $SERVICE -p 127.0.0.1:8080:8080 --read-only --tmpfs /tmp \
    -e EMERICK_LUA_TIMEOUT_MS=none -e EMERICK_LUA_MEMORY_MB=none -e EMERICK_MAX_RENDER_SECONDS=none \
    -e EMERICK_BUNDLED_PLUGINS_ONLY=false emerick-dev >/dev/null
trap 'for c in $NAME $SERVICE; do echo "== $c"; docker logs $c 2>&1 | tail -20; done; docker rm -fv $NAME $SERVICE >/dev/null; rm -rf "$EMERICK_E2E_OUT" 2>/dev/null || true' EXIT
for _ in $(seq 1 30); do
    code=$(curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' \
        -H 'Accept: application/json, text/event-stream' \
        -d '{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"wait","version":"0"}}}' \
        http://127.0.0.1:8888/mcp || true)
    health=$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/health || true)
    [ "$code" = 200 ] && [ "$health" = 200 ] && break
    for c in $NAME $SERVICE; do
        [ "$(docker inspect -f '{{.State.Running}}' $c)" = true ] || { echo "$c exited"; exit 1; }
    done
    sleep 1
done
python3 -m unittest discover -v -s e2e/tests
