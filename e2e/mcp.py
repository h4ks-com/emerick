import json
import urllib.error
import urllib.request

URL = "http://127.0.0.1:8888/mcp"


class Client:
    def __init__(self, url=URL, timeout=10):
        self.url = url
        self.timeout = timeout
        self.next_id = 1
        self.session = None

    def rpc(self, method, params=None):
        body = {"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params or {}}
        self.next_id += 1
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.session:
            headers["Mcp-Session-Id"] = self.session
        request = urllib.request.Request(self.url, json.dumps(body).encode(), headers)
        with urllib.request.urlopen(request, timeout=self.timeout) as response:
            self.session = response.headers.get("Mcp-Session-Id", self.session)
            reply = json.loads(response.read())
        if "error" in reply:
            raise RuntimeError(reply["error"])
        return reply["result"]

    def initialize(self):
        return self.rpc("initialize", {
            "protocolVersion": "2025-06-18",
            "capabilities": {},
            "clientInfo": {"name": "emerick-e2e", "version": "0"},
        })

    def list_tools(self):
        return self.rpc("tools/list")["tools"]

    def call(self, name, arguments=None):
        return self.rpc("tools/call", {"name": name, "arguments": arguments or {}})

    def run(self, action, params=None):
        result = self.call("actions.run", {"id": action, "params": params or {}})["structuredContent"]
        if "error" in result:
            raise RuntimeError(f"{action}: {result['error']}")
        return result["result"]
