import io
import zipfile
from pathlib import Path

import pytest
from conftest import MakeClient, new_project, start_session, wait_for_status

from emerick_service.child import MissingFlagsError, child_args, child_env
from emerick_service.config import Settings

TOOLS_CALL = {"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {"name": "actions.run"}}


def actions(tmp_path: Path, project_id: str) -> list[str]:
    return (tmp_path / "data" / "projects" / project_id / "actions.log").read_text().split()


def test_session_creates_project_proxies_mcp_and_saves_on_close(make_client: MakeClient, tmp_path: Path) -> None:
    client = make_client()
    project_id = new_project(client)
    session = client.post("/sessions", json={"project_id": project_id}).json()
    assert session["status"] == "running"
    assert session["mcp_url"] == f"http://testserver/sessions/{session['id']}/mcp"
    assert session["project_dir"] == str(tmp_path / "data" / "projects" / project_id)

    call = {**TOOLS_CALL, "params": {"name": "actions.run", "arguments": {"id": "track.addAudio"}}}
    reply = client.post(f"/sessions/{session['id']}/mcp", json=call)
    assert reply.status_code == 200
    assert reply.json()["id"] == 7

    closed = client.delete(f"/sessions/{session['id']}").json()
    assert (closed["status"], closed["reason"]) == ("closed", "closed by request")
    assert actions(tmp_path, project_id) == ["project.new", "track.addAudio", "project.save"]
    assert client.post(f"/sessions/{session['id']}/mcp", json=call).status_code == 404


def test_second_session_reopens_the_project(make_client: MakeClient, tmp_path: Path) -> None:
    client = make_client()
    project_id = new_project(client)
    client.delete(f"/sessions/{start_session(client, project_id)}")
    client.delete(f"/sessions/{start_session(client, project_id)}")
    assert actions(tmp_path, project_id) == ["project.new", "project.save", "project.open", "project.save"]


def test_pool_limit_answers_429(make_client: MakeClient) -> None:
    client = make_client(max_sessions=1)
    start_session(client, new_project(client, "one"))
    response = client.post("/sessions", json={"project_id": new_project(client, "two")})
    assert response.status_code == 429
    assert "all 1 sessions are in use" in response.json()["detail"]


def test_one_session_per_project(make_client: MakeClient) -> None:
    client = make_client()
    project_id = new_project(client)
    start_session(client, project_id)
    assert client.post("/sessions", json={"project_id": project_id}).status_code == 409
    assert client.delete(f"/projects/{project_id}").status_code == 409


def test_unknown_project_is_404(make_client: MakeClient) -> None:
    client = make_client()
    assert client.post("/sessions", json={"project_id": "nope"}).status_code == 404


def test_reaper_closes_idle_session_after_saving(make_client: MakeClient, tmp_path: Path) -> None:
    client = make_client(session_idle_seconds=0.3, reaper_interval_seconds=0.1)
    project_id = new_project(client)
    session = wait_for_status(client, start_session(client, project_id), "closed")
    assert session["reason"] == "idle timeout"
    assert actions(tmp_path, project_id)[-1] == "project.save"


def test_reaper_closes_project_over_quota(make_client: MakeClient, tmp_path: Path) -> None:
    client = make_client(project_max_mb=1, reaper_interval_seconds=0.1)
    project_id = new_project(client)
    session_id = start_session(client, project_id)
    (tmp_path / "data" / "projects" / project_id / "big.wav").write_bytes(b"\0" * (2 << 20))
    assert wait_for_status(client, session_id, "closed")["reason"] == "project disk quota exceeded"


def test_crashed_child_marks_session_failed(make_client: MakeClient) -> None:
    client = make_client()
    session_id = start_session(client, new_project(client))
    call = {**TOOLS_CALL, "params": {"name": "actions.run", "arguments": {"id": "crash"}}}
    assert client.post(f"/sessions/{session_id}/mcp", json=call).status_code == 502
    assert wait_for_status(client, session_id, "failed")["reason"] == "emerick exited with code 3"


def test_missing_flags_fail_startup(make_client: MakeClient, tmp_path: Path) -> None:
    silent = tmp_path / "silent"
    silent.write_text("#!/bin/sh\necho '--headless'\n")
    silent.chmod(0o755)
    with pytest.raises(MissingFlagsError, match=r"--bundled-plugins-only.*--mcp-port"):
        make_client(bin=silent)


def test_child_gets_roots_and_limits(tmp_path: Path) -> None:
    args = child_args(Settings(lua_timeout_ms=500), 20001, tmp_path / "profile", tmp_path / "song")
    assert f"--agent-write-root={tmp_path / 'song'}" in args
    assert {"--mcp-port=20001", "--lua-timeout-ms=500", "--bundled-plugins-only"} <= set(args)
    unlimited = child_args(Settings(lua_timeout_ms=None, bundled_plugins_only=False), 1, tmp_path, tmp_path)
    assert not any(arg.startswith(("--lua-timeout-ms", "--bundled-plugins-only")) for arg in unlimited)


def test_child_reads_the_shared_library_folder(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("EMERICK_LIBRARY_DIR", "/data/libraries")
    monkeypatch.delenv("EMERICK_LIBRARY_INSTALL", raising=False)
    env = child_env(tmp_path)
    assert env["EMERICK_LIBRARY_DIR"] == "/data/libraries"
    assert "EMERICK_LIBRARY_INSTALL" not in env


def test_top_level_mcp_lists_tools(make_client: MakeClient) -> None:
    client = make_client()
    headers = {"Accept": "application/json, text/event-stream"}
    init = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "t", "version": "0"}},
    }
    assert client.post("/mcp", json=init, headers=headers).status_code == 200
    listed = client.post("/mcp", json={"jsonrpc": "2.0", "id": 2, "method": "tools/list"}, headers=headers)
    names = {tool["name"] for tool in listed.json()["result"]["tools"]}
    assert names == {
        "project.create",
        "project.list",
        "project.upload",
        "project.bundle",
        "session.create",
        "session.list",
        "session.tools",
        "session.call",
        "session.close",
    }


def test_top_level_mcp_drives_a_session_without_its_own_url(make_client: MakeClient) -> None:
    client = make_client()
    headers = {"Accept": "application/json, text/event-stream"}

    def call(tool: str, arguments: dict[str, object]) -> dict[str, object]:
        body = {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": tool, "arguments": arguments}}
        reply = client.post("/mcp", json=body, headers=headers).json()
        return reply["result"]["structuredContent"]

    project = call("project.create", {"name": "song"})
    session = call("session.create", {"project_id": project["id"]})
    tools = call("session.tools", {"session_id": session["id"]})
    assert tools == {"result": {"tools": [{"name": "actions.run"}, {"name": "lua.eval"}]}}
    ran = call("session.call", {"session_id": session["id"], "tool": "actions.run", "arguments": {"id": "x"}})
    assert ran == {"result": {"structuredContent": {"result": {}}}}
    assert "x" in (Path(str(session["project_dir"])) / "actions.log").read_text().split()
    closed = call("session.close", {"session_id": session["id"]})
    assert closed["status"] == "closed"


def test_upload_stores_a_file_and_refuses_escapes_and_quota(make_client: MakeClient, tmp_path: Path) -> None:
    client = make_client(project_max_mb=1)
    project_id = new_project(client)
    stored = client.put(f"/projects/{project_id}/files/styles/funk.sty", content=b"style bytes")
    assert stored.status_code == 201, stored.text
    assert stored.json()["path"] == "styles/funk.sty"
    assert (tmp_path / "data" / "projects" / project_id / "styles" / "funk.sty").read_bytes() == b"style bytes"
    assert client.get(f"/projects/{project_id}/files/styles/funk.sty").content == b"style bytes"

    assert client.put(f"/projects/{project_id}/files/..%2Fescape.txt", content=b"x").status_code == 400
    assert client.put(f"/projects/{project_id}/files/big.wav", content=bytes(2 << 20)).status_code == 413
    assert client.put("/projects/missing/files/a.txt", content=b"x").status_code == 404


def test_upload_reaches_a_live_session_and_the_mcp_tool(make_client: MakeClient) -> None:
    client = make_client()
    project_id = new_project(client)
    session = client.post("/sessions", json={"project_id": project_id}).json()
    assert client.put(f"/projects/{project_id}/files/voices.csv", content=b"0,0,1,Piano\n").status_code == 201
    assert (Path(session["project_dir"]) / "voices.csv").read_bytes() == b"0,0,1,Piano\n"

    headers = {"Accept": "application/json, text/event-stream"}
    arguments = {"project_id": project_id, "path": "a.sty", "content_base64": "c3R5bGU="}
    body = {
        "jsonrpc": "2.0",
        "id": 4,
        "method": "tools/call",
        "params": {"name": "project.upload", "arguments": arguments},
    }
    reply = client.post("/mcp", json=body, headers=headers).json()
    assert reply["result"]["structuredContent"]["size"] == 5
    assert (Path(session["project_dir"]) / "a.sty").read_bytes() == b"style"


def test_bundle_zips_the_bundle_folder_with_and_without_a_session(make_client: MakeClient) -> None:
    client = make_client()
    project_id = new_project(client)
    client.put(f"/projects/{project_id}/files/song.erk/project.json", content=b"{}")
    client.put(f"/projects/{project_id}/files/song.erk/style.sty", content=b"style")
    client.put(f"/projects/{project_id}/files/other.wav", content=b"x")

    def bundle() -> dict[str, bytes]:
        reply = client.get(f"/projects/{project_id}/bundle", params={"path": "song.erk"})
        assert reply.status_code == 200, reply.text
        with zipfile.ZipFile(io.BytesIO(reply.content)) as zipped:
            return {name: zipped.read(name) for name in zipped.namelist()}

    assert bundle() == {"song.erk/project.json": b"{}", "song.erk/style.sty": b"style"}
    start_session(client, project_id)
    client.put(f"/projects/{project_id}/files/song.erk/voices.csv", content=b"csv")
    assert bundle()["song.erk/voices.csv"] == b"csv"
    assert client.get(f"/projects/{project_id}/bundle", params={"path": "../x"}).status_code == 400
    assert client.get(f"/projects/{project_id}/bundle", params={"path": "missing.erk"}).status_code == 404
