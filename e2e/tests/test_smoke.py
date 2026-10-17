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
        self.assertTrue({"ui.tree", "actions.list", "lua.eval"} <= names, names)


if __name__ == "__main__":
    unittest.main()
