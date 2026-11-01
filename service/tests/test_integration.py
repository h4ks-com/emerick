import os
from pathlib import Path

import pytest
from conftest import MakeClient, new_project, start_session

EMERICK_BIN = os.environ.get("EMERICK_BIN")


@pytest.mark.skipif(not EMERICK_BIN, reason="EMERICK_BIN does not point at an emerick binary")
def test_real_session_builds_and_exports_a_mix(make_client: MakeClient, tmp_path: Path) -> None:
    client = make_client(bin=Path(EMERICK_BIN or ""), start_timeout_seconds=60)
    project_id = new_project(client)
    session_id = start_session(client, project_id)
    project_dir = (tmp_path / "data" / "projects" / project_id).resolve()

    def run(action: str, params: dict[str, object]) -> dict[str, object]:
        call = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": "actions.run", "arguments": {"id": action, "params": params}},
        }
        content = client.post(f"/sessions/{session_id}/mcp", json=call).json()["result"]["structuredContent"]
        assert "error" not in content, content
        return content["result"]

    track = run("track.addInstrument", {"plugin": "org.emerick.gm", "name": "Piano"})["id"]
    clip = run("midi.addClip", {"track": track, "at": 0})["id"]
    run("midi.addNote", {"clip": clip, "pitch": 60, "start": 0, "length": 960})
    run("export.mix", {"path": str(project_dir / "mix.wav")})
    outside = client.post(
        f"/sessions/{session_id}/mcp",
        json={
            "jsonrpc": "2.0",
            "id": 2,
            "method": "tools/call",
            "params": {"name": "actions.run", "arguments": {"id": "export.mix", "params": {"path": "/tmp/escape.wav"}}},
        },
    ).json()["result"]["structuredContent"]
    assert "error" in outside

    assert client.delete(f"/sessions/{session_id}").json()["status"] == "closed"
    paths = {file["path"] for file in client.get(f"/projects/{project_id}/files").json()}
    assert {"mix.wav", "project.erk/project.json"} <= paths
