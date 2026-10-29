import os
import sys
import unittest
import urllib.error

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from mcp import Client


class Smoke(unittest.TestCase):
    def setUp(self):
        self.client = Client()
        try:
            self.init = self.client.initialize()
        except (urllib.error.URLError, ConnectionError, TimeoutError, RuntimeError) as error:
            self.skipTest(f"MCP server not answering at {self.client.url}: {error}")

    def test_initialize(self):
        self.assertIn("serverInfo", self.init)

    def test_tools(self):
        names = {tool["name"] for tool in self.client.list_tools()}
        self.assertTrue({"actions.list", "actions.run", "lua.eval"} <= names, names)
        self.assertFalse({"ui.tree", "screenshot"} & names, names)

    def test_window_only_action_fails(self):
        result = self.client.call("actions.run", {"id": "view.toggleMixer"})["structuredContent"]
        self.assertIn("headless", result.get("error", ""))

    def test_lua_ui_fails_cleanly(self):
        result = self.client.call("lua.eval", {"code": "return emerick.ui.set('noSuchItem', 1, 'value')"})
        self.assertTrue(result["isError"], result)


if __name__ == "__main__":
    unittest.main()
