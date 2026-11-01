import json
import os
import sys
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from mcp import Client

SERVICE = f"http://127.0.0.1:{os.environ.get('EMERICK_E2E_SERVICE_PORT', '18080')}"


def request(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(SERVICE + path, data, {"Content-Type": "application/json"}, method=method)
    with urllib.request.urlopen(req, timeout=120) as response:
        raw = response.read()
        return response.status, raw if response.headers.get_content_type() != "application/json" else json.loads(raw)


class Service(unittest.TestCase):
    def setUp(self):
        try:
            request("GET", "/health")
        except (urllib.error.URLError, ConnectionError) as error:
            self.skipTest(f"service not answering at {SERVICE}: {error}")

    def test_agent_session_exports_a_mix(self):
        _, project = request("POST", "/projects", {"name": "song"})
        status, session = request("POST", "/sessions", {"project_id": project["id"]})
        self.assertEqual((status, session["status"]), (201, "running"))
        self.assertEqual(session["mcp_url"], f"{SERVICE}/sessions/{session['id']}/mcp")

        client = Client(session["mcp_url"], timeout=120)
        client.initialize()
        limits = client.call("state.get", {"name": "limits"})["structuredContent"]["limits"]
        self.assertTrue(limits["bundledPluginsOnly"], limits)
        self.assertEqual(limits["agentWriteRoot"].rstrip("/"), session["project_dir"], limits)
        track =client.run("track.addInstrument", {"plugin": "org.emerick.gm", "name": "Piano"})["id"]
        clip = client.run("midi.addClip", {"track": track, "at": 0})["id"]
        client.run("midi.addNote", {"clip": clip, "pitch": 60, "start": 0, "length": 960})
        client.run("export.mix", {"path": f"{session['project_dir']}/mix.wav"})
        with self.assertRaises(RuntimeError):
            client.run("export.mix", {"path": "/data/service.db"})

        _, closed = request("DELETE", f"/sessions/{session['id']}")
        self.assertEqual(closed["status"], "closed")
        _, files = request("GET", f"/projects/{project['id']}/files")
        self.assertTrue({"mix.wav", "project.erk/project.json"} <= {file["path"] for file in files}, files)
        _, mix = request("GET", f"/projects/{project['id']}/files/mix.wav")
        self.assertEqual(mix[:4], b"RIFF")

    def test_top_level_mcp_lists_service_tools(self):
        client = Client(f"{SERVICE}/mcp")
        client.initialize()
        names = {tool["name"] for tool in client.list_tools()}
        self.assertTrue({"session.create", "session.close", "project.create"} <= names, names)


if __name__ == "__main__":
    unittest.main()
