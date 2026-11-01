from collections.abc import Iterator
from pathlib import Path

import boto3
import pytest
from conftest import MakeClient, new_project
from moto import mock_aws
from mypy_boto3_s3 import S3Client

from emerick_service.storage import PathEscapeError, S3Storage, contained, relative_path


@pytest.mark.parametrize("path", ["", "/etc/passwd", "../x", "a/../../x", "a\\..\\x", "a\0b"])
def test_relative_path_rejects_escapes(path: str) -> None:
    with pytest.raises(PathEscapeError):
        relative_path(path)


def test_contained_rejects_symlink_out(tmp_path: Path) -> None:
    root = tmp_path / "project"
    root.mkdir()
    (root / "out").symlink_to(tmp_path)
    with pytest.raises(PathEscapeError):
        contained(root, "out/secret")
    assert contained(root, "mix.wav") == (root / "mix.wav").resolve()


def test_files_lists_and_downloads_inside_the_project(make_client: MakeClient, tmp_path: Path) -> None:
    client = make_client()
    project_id = new_project(client)
    project_dir = tmp_path / "data" / "projects" / project_id
    (project_dir / "mix.wav").write_bytes(b"RIFF")
    (tmp_path / "data" / "secret").write_text("no")

    assert [file["path"] for file in client.get(f"/projects/{project_id}/files").json()] == ["mix.wav"]
    download = client.get(f"/projects/{project_id}/files/mix.wav")
    assert download.content == b"RIFF"
    assert download.headers["content-type"] == "audio/x-wav"
    assert client.get(f"/projects/{project_id}/files/..%2Fsecret").status_code in {400, 404}
    assert client.get(f"/projects/{project_id}/files/%2E%2E/%2E%2E/secret").status_code in {400, 404}
    assert client.get(f"/projects/{project_id}/files/nothing.wav").status_code == 404


def test_download_rejects_traversal(make_client: MakeClient, tmp_path: Path) -> None:
    client = make_client()
    project_id = new_project(client)
    (tmp_path / "data" / "projects" / project_id / "link").symlink_to(tmp_path / "data")
    assert client.get(f"/projects/{project_id}/files/link/service.db").status_code == 400


@pytest.fixture
def s3(monkeypatch: pytest.MonkeyPatch) -> Iterator[S3Client]:
    monkeypatch.setenv("AWS_ACCESS_KEY_ID", "x")
    monkeypatch.setenv("AWS_SECRET_ACCESS_KEY", "x")
    monkeypatch.setenv("AWS_DEFAULT_REGION", "us-east-1")
    with mock_aws():
        client: S3Client = boto3.client("s3")  # pyright: ignore[reportUnknownMemberType]
        client.create_bucket(Bucket="songs")
        yield client


def test_s3_checkout_commit_round_trip(s3: S3Client, tmp_path: Path) -> None:
    storage = S3Storage(s3, "songs", "projects/")
    s3.put_object(Bucket="songs", Key="projects/p1/project.erk/project.json", Body=b"{}")
    s3.put_object(Bucket="songs", Key="projects/p1/old.wav", Body=b"old")
    s3.put_object(Bucket="songs", Key="projects/p2/other.wav", Body=b"other")

    project_dir = storage.checkout("p1", tmp_path / "work")
    assert (project_dir / "project.erk" / "project.json").read_bytes() == b"{}"
    assert not (project_dir / "other.wav").exists()

    (project_dir / "old.wav").unlink()
    (project_dir / "mix.wav").write_bytes(b"mix")
    storage.commit("p1", project_dir)

    assert [file.path for file in storage.files("p1")] == ["mix.wav", "project.erk/project.json"]
    assert b"".join(storage.read("p1", "mix.wav")) == b"mix"
    with pytest.raises(FileNotFoundError):
        storage.read("p1", "old.wav")
    with pytest.raises(PathEscapeError):
        storage.read("p1", "../p2/other.wav")

    storage.delete("p1")
    assert storage.files("p1") == []
    assert [file.path for file in storage.files("p2")] == ["other.wav"]
