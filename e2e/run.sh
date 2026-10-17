#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
NAME=emerick-e2e
docker build -t emerick-dev -f docker/Dockerfile .
docker rm -f $NAME >/dev/null 2>&1 || true
docker run -d --name $NAME -p 127.0.0.1:7777:7777 emerick-dev >/dev/null
trap 'docker logs $NAME 2>&1 | tail -20; docker rm -f $NAME >/dev/null' EXIT
for _ in $(seq 1 30); do
    code=$(curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' \
        -H 'Accept: application/json, text/event-stream' \
        -d '{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"wait","version":"0"}}}' \
        http://127.0.0.1:7777/mcp || true)
    [ "$code" = 200 ] && break
    [ "$(docker inspect -f '{{.State.Running}}' $NAME)" = true ] || { echo "container exited"; exit 1; }
    sleep 1
done
python3 -m unittest discover -v -s e2e/tests
