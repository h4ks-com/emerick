import time
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

import pytest
from conftest import MakeClient, new_project, start_session
from fastapi.testclient import TestClient

from emerick_service.app import RangeNotSatisfiableError, byte_span
from emerick_service.config import Settings
from emerick_service.viewer import render_args


def put(client: TestClient, project_id: str, path: str, content: bytes) -> None:
    assert client.put(f"/projects/{project_id}/files/{path}", content=content).status_code == 201


def wait_for_render(client: TestClient, project_id: str, timeout: float = 10) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if client.get(f"/projects/{project_id}/mix").status_code != 202:
            return
        time.sleep(0.1)
    raise AssertionError("the mix never rendered")


@pytest.fixture
def viewer_dir(tmp_path: Path) -> Path:
    folder = tmp_path / "viewer"
    folder.mkdir()
    (folder / "index.html").write_text("<html>viewer</html>")
    (folder / "emerick-viewer.wasm").write_bytes(b"\0asm" + bytes(4096))
    return folder


def test_viewer_page_redirects_with_the_project_urls_then_serves_the_page(
    make_client: MakeClient, viewer_dir: Path
) -> None:
    client = make_client(viewer_dir=viewer_dir)
    project_id = new_project(client)
    put(client, project_id, "project.erk/project.json", b"{}")
    time.sleep(0.01)
    put(client, project_id, "song.erk/project.json", b"{}")

    page = client.get(f"/projects/{project_id}/viewer/", follow_redirects=False)
    assert page.status_code == 307
    query = {name: values[0] for name, values in parse_qs(urlsplit(page.headers["location"]).query).items()}
    base = f"http://testserver/projects/{project_id}"
    assert query == {
        "project": f"{base}/files/song.erk/project.json",
        "mix": f"{base}/mix?bundle=song.erk",
        "bundle": f"{base}/files/song.erk",
    }
    assert client.get(page.headers["location"]).text == "<html>viewer</html>"

    picked = client.get(f"/projects/{project_id}/viewer/", params={"bundle": "project.erk"}, follow_redirects=False)
    assert "project.erk%2Fproject.json" in picked.headers["location"]
    assert client.get(f"/projects/{project_id}/viewer/", params={"bundle": "x.erk"}).status_code == 404


def test_viewer_assets_have_mime_compression_and_etags(make_client: MakeClient, viewer_dir: Path) -> None:
    client = make_client(viewer_dir=viewer_dir)
    url = f"/projects/{new_project(client)}/viewer/emerick-viewer.wasm"
    raw = viewer_dir.joinpath("emerick-viewer.wasm").read_bytes()

    zipped = client.get(url, headers={"Accept-Encoding": "gzip"})
    assert zipped.headers["content-type"] == "application/wasm"
    assert zipped.headers["content-encoding"] == "gzip"
    assert int(zipped.headers["content-length"]) < len(raw)
    assert zipped.content == raw
    plain = client.get(url, headers={"Accept-Encoding": "identity"})
    assert (plain.headers.get("content-encoding"), plain.content) == (None, raw)
    assert client.get(url, headers={"If-None-Match": zipped.headers["etag"]}).status_code == 304
    assert client.get(f"{url}x").status_code == 404


def test_viewer_not_installed_is_a_clear_404(make_client: MakeClient, tmp_path: Path) -> None:
    client = make_client(viewer_dir=tmp_path / "missing")
    project_id = new_project(client)
    reply = client.get(f"/projects/{project_id}/viewer/")
    assert reply.status_code == 404
    assert "the viewer is not installed" in reply.json()["detail"]
    assert client.get(f"/projects/{project_id}/viewer/app.js").status_code == 404


def test_byte_span() -> None:
    assert byte_span(None, 10) is None
    assert byte_span("bytes=2-5", 10) == (2, 5)
    assert byte_span("bytes=7-", 10) == (7, 9)
    assert byte_span("bytes=-3", 10) == (7, 9)
    assert byte_span("bytes=0-99", 10) == (0, 9)
    assert byte_span("bytes=0-1,4-5", 10) is None
    with pytest.raises(RangeNotSatisfiableError):
        byte_span("bytes=10-", 10)


def test_files_and_mix_answer_range_requests(make_client: MakeClient) -> None:
    client = make_client()
    project_id = new_project(client)
    put(client, project_id, "song.erk/project.json", b"{}")
    put(client, project_id, "song.erk/audio/.peaks/take.wav.peaks", b"0123456789")
    put(client, project_id, "mix.ogg", b"OggS-audio")

    peaks = client.get(
        f"/projects/{project_id}/files/song.erk/audio/.peaks/take.wav.peaks", headers={"Range": "bytes=2-5"}
    )
    assert peaks.status_code == 206
    assert peaks.headers["content-range"] == "bytes 2-5/10"
    assert peaks.headers["content-type"] == "application/octet-stream"
    assert peaks.content == b"2345"

    mix = client.get(f"/projects/{project_id}/mix", headers={"Range": "bytes=4-"})
    assert (mix.status_code, mix.content, mix.headers["content-type"]) == (206, b"-audio", "audio/ogg")
    whole = client.get(f"/projects/{project_id}/mix")
    assert (whole.status_code, whole.headers["accept-ranges"], whole.content) == (200, "bytes", b"OggS-audio")
    past = client.get(f"/projects/{project_id}/mix", headers={"Range": "bytes=50-"})
    assert (past.status_code, past.headers["content-range"]) == (416, "bytes */10")


def test_mix_picks_the_newest_export_or_a_path(make_client: MakeClient) -> None:
    client = make_client()
    project_id = new_project(client)
    put(client, project_id, "song.erk/project.json", b"{}")
    put(client, project_id, "mix.wav", b"old")
    time.sleep(0.01)
    put(client, project_id, "song.erk/master.flac", b"new")
    put(client, project_id, "stems/drums.wav", b"stem")
    assert client.get(f"/projects/{project_id}/mix").content == b"new"
    assert client.get(f"/projects/{project_id}/mix", params={"path": "mix.wav"}).content == b"old"
    assert client.get(f"/projects/{project_id}/mix", params={"path": "none.wav"}).status_code == 404


def test_missing_mix_renders_in_the_background_once(make_client: MakeClient, tmp_path: Path) -> None:
    client = make_client(max_sessions=1)
    project_id = new_project(client)
    put(client, project_id, "song.erk/project.json", b'{"tracks": []}')
    put(client, project_id, "song.erk/audio/take.wav", b"RIFF")

    first = client.get(f"/projects/{project_id}/mix")
    assert (first.status_code, first.headers["retry-after"]) == (202, "5")
    assert client.get("/health").json()["sessions_live"] == 0
    start_session(client, new_project(client, "other"))

    wait_for_render(client, project_id)
    rendered = client.get(f"/projects/{project_id}/mix")
    assert (rendered.status_code, rendered.content) == (200, b"ID3 rendered")
    project_dir = tmp_path / "data" / "projects" / project_id
    assert (project_dir / "song.erk/audio/.peaks/take.wav.peaks").read_bytes() == b"peaks"
    mix_file = project_dir / "song.erk/viewer-mix.mp3"
    modified = mix_file.stat().st_mtime_ns

    assert client.get(f"/projects/{project_id}/mix").status_code == 200
    time.sleep(0.7)
    assert mix_file.stat().st_mtime_ns == modified
    assert not [path for path in (tmp_path / "work").iterdir() if path.name.startswith("render-")]

    mix_file.unlink()
    assert client.get(f"/projects/{project_id}/mix").status_code == 202
    wait_for_render(client, project_id)
    modified = mix_file.stat().st_mtime_ns

    put(client, project_id, "song.erk/project.json", b'{"tracks": [1]}')
    assert client.get(f"/projects/{project_id}/mix").status_code == 202
    wait_for_render(client, project_id)
    assert mix_file.stat().st_mtime_ns != modified


def test_render_reads_the_stored_project_and_leaves_a_live_session_alone(
    make_client: MakeClient, tmp_path: Path
) -> None:
    client = make_client()
    project_id = new_project(client)
    session_id = start_session(client, project_id)
    assert client.get(f"/projects/{project_id}/mix").status_code == 202
    wait_for_render(client, project_id)
    assert client.get(f"/projects/{project_id}/mix").content == b"ID3 rendered"
    project_dir = tmp_path / "data" / "projects" / project_id
    assert (project_dir / "actions.log").read_text().split() == ["project.new"]
    assert client.get(f"/sessions/{session_id}").json()["status"] == "running"


def test_failed_render_answers_404_and_does_not_retry(make_client: MakeClient) -> None:
    client = make_client()
    project_id = new_project(client)
    put(client, project_id, "song.erk/project.json", b"fail")
    assert client.get(f"/projects/{project_id}/mix").status_code == 202
    wait_for_render(client, project_id)
    failed = client.get(f"/projects/{project_id}/mix")
    assert failed.status_code == 404
    assert "emerick exited with code 1" in failed.json()["detail"]


def test_project_without_bundle_has_no_mix(make_client: MakeClient) -> None:
    client = make_client()
    assert client.get(f"/projects/{new_project(client)}/mix").status_code == 404


def test_render_gets_the_session_limits_without_mcp(tmp_path: Path) -> None:
    args = render_args(Settings(), tmp_path / "render.lua", tmp_path / "profile", tmp_path / "project")
    assert f"--script={tmp_path / 'render.lua'}" in args
    assert f"--agent-write-root={tmp_path / 'project'}" in args
    assert {"--max-render-seconds=900", "--lua-memory-mb=256", "--bundled-plugins-only"} <= set(args)
    assert not any(arg.startswith(("--headless", "--mcp-port", "--lua-timeout-ms")) for arg in args)
