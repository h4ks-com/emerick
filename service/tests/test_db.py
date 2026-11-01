import sqlite3
from pathlib import Path

import pytest

from emerick_service.db import Database
from emerick_service.models import SessionStatus


def test_projects_and_sessions_round_trip(tmp_path: Path) -> None:
    db = Database(tmp_path / "service.db")
    project = db.create_project("song")
    assert db.get_project(project.id) == project
    assert db.list_projects() == [project]

    session = db.create_session(project.id)
    db.mark_running(session.id, 20001)
    running = db.get_session(session.id)
    assert running is not None
    assert (running.status, running.port) == (SessionStatus.RUNNING, 20001)
    assert db.list_sessions(live_only=True) == [running]

    db.finish(session.id, SessionStatus.CLOSED, "done")
    closed = db.get_session(session.id)
    assert closed is not None
    assert (closed.status, closed.reason) == (SessionStatus.CLOSED, "done")
    assert db.list_sessions(live_only=True) == []

    db.delete_project(project.id)
    assert db.get_project(project.id) is None
    assert db.get_session(session.id) is None


def test_one_live_session_per_project(tmp_path: Path) -> None:
    db = Database(tmp_path / "service.db")
    project = db.create_project("song")
    first = db.create_session(project.id)
    with pytest.raises(sqlite3.IntegrityError):
        db.create_session(project.id)
    db.finish(first.id, SessionStatus.CLOSED, "done")
    db.create_session(project.id)


def test_restart_fails_live_sessions(tmp_path: Path) -> None:
    db = Database(tmp_path / "service.db")
    session = db.create_session(db.create_project("song").id)
    db.fail_live_sessions("restarted")
    failed = db.get_session(session.id)
    assert failed is not None
    assert (failed.status, failed.reason) == (SessionStatus.FAILED, "restarted")
