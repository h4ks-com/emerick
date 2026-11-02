import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

HELP = """Usage: emerick [options] [-- args...]
  --headless  --script <file>  --mcp-port <port>  --audio <backend>  --midi <backend>
  --profile <folder>  --agent-read-root <folder>  --agent-write-root <folder>
  --lua-timeout-ms <ms>  --lua-memory-mb <mb>  --max-render-seconds <s>  --bundled-plugins-only
"""


def option(name: str) -> str:
    return next(arg.split("=", 1)[1] for arg in sys.argv if arg.startswith(f"--{name}="))


class Handler(BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        message = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        result: dict[str, object] = {}
        if message["method"] == "tools/list":
            result = {"tools": [{"name": "actions.run"}, {"name": "lua.eval"}]}
        if message["method"] == "tools/call":
            action = message["params"]["arguments"]["id"]
            root = Path(option("agent-write-root"))
            with (root / "actions.log").open("a") as log:
                log.write(action + "\n")
            if action == "crash":
                os._exit(3)
            if action == "project.new":
                bundle = Path(message["params"]["arguments"]["params"]["path"])
                bundle.mkdir(parents=True)
                (bundle / "project.json").write_text("{}")
            result = {"structuredContent": {"result": {}}}
        body = json.dumps({"jsonrpc": "2.0", "id": message["id"], "result": result}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args: object) -> None:
        pass


def render() -> None:
    bundle_arg, mix_arg, peaks = sys.argv[sys.argv.index("--") + 1 :]
    bundle, mix = Path(bundle_arg), Path(mix_arg) if mix_arg else None
    root = Path(option("agent-write-root"))
    assert "--headless" not in sys.argv
    assert all(path is None or path.is_relative_to(root) for path in (bundle, mix))
    time.sleep(0.5)
    if "fail" in (bundle / "project.json").read_text():
        sys.exit(1)
    if mix:
        mix.write_bytes(b"ID3 rendered")
    if peaks:
        for audio in (bundle / "audio").glob("*.wav"):
            (audio.parent / ".peaks").mkdir(exist_ok=True)
            (audio.parent / ".peaks" / f"{audio.name}.peaks").write_bytes(b"peaks")


if __name__ == "__main__":
    if "--help" in sys.argv:
        sys.stdout.write(HELP)
        sys.exit(0)
    if any(arg.startswith("--script") for arg in sys.argv):
        render()
        sys.exit(0)
    HTTPServer(("127.0.0.1", int(option("mcp-port"))), Handler).serve_forever()
