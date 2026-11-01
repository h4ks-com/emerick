import sys
import time
from collections.abc import Callable, Iterator
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from emerick_service.app import create_app
from emerick_service.config import Settings

FAKE = Path(__file__).with_name("fake_emerick.py")
type MakeClient = Callable[..., TestClient]


@pytest.fixture
def fake_bin(tmp_path: Path) -> Path:
    wrapper = tmp_path / "emerick"
    wrapper.write_text(f'#!/bin/sh\nexec "{sys.executable}" "{FAKE}" "$@"\n')
    wrapper.chmod(0o755)
    return wrapper


@pytest.fixture
def make_client(tmp_path: Path, fake_bin: Path) -> Iterator[MakeClient]:
    clients: list[TestClient] = []

    def make(**overrides: float | str | Path) -> TestClient:
        settings = Settings.model_validate(
            {
                "bin": fake_bin,
                "data_dir": tmp_path / "data",
                "work_dir": tmp_path / "work",
                "port_min": 23100,
                "port_max": 23199,
                "start_timeout_seconds": 10,
                "stop_timeout_seconds": 2,
                **overrides,
            }
        )
        client = TestClient(create_app(settings))
        client.__enter__()
        clients.append(client)
        return client

    yield make
    for client in clients:
        client.__exit__(None, None, None)


def new_project(client: TestClient, name: str = "song") -> str:
    response = client.post("/projects", json={"name": name})
    assert response.status_code == 201, response.text
    return response.json()["id"]


def start_session(client: TestClient, project_id: str) -> str:
    response = client.post("/sessions", json={"project_id": project_id})
    assert response.status_code == 201, response.text
    return response.json()["id"]


def wait_for_status(client: TestClient, session_id: str, status: str, timeout: float = 10) -> dict[str, str]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        session = client.get(f"/sessions/{session_id}").json()
        if session["status"] == status:
            return session
        time.sleep(0.1)
    raise AssertionError(f"session {session_id} never reached {status}")
