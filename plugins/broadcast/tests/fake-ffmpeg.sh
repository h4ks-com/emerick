#!/bin/sh
# Stands in for ffmpeg in the broadcast tests: records its arguments, then copies stdin to a file and to stdout, dies at
# once, or never reads stdin, as FAKE_FFMPEG_MODE asks.
printf '%s\n' "$@" >"$FAKE_FFMPEG_DIR/args"
case "$FAKE_FFMPEG_MODE" in
die)
    echo "fake encoder failed" >&2
    exit 1
    ;;
stall) exec sleep 60 ;;
esac
exec tee "$FAKE_FFMPEG_DIR/stdin.raw"
