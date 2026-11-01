import sqlite3
import threading
from datetime import UTC, datetime
from pathlib import Path
from uuid import uuid4

from emerick_service.models import LIVE_STATUSES, Project, Session, SessionStatus

SCHEMA = """
CREATE TABLE IF NOT EXISTS projects (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    status TEXT NOT NULL,
    reason TEXT,
    port INTEGER,
    created_at TEXT NOT NULL,
    last_activity TEXT NOT NULL,
    closed_at TEXT
);
CREATE UNIQUE INDEX IF NOT EXISTS one_live_session_per_project
    ON sessions(project_id) WHERE status IN ('starting', 'running');
"""


def now() -> datetime:
    return datetime.now(UTC)


class Database:
    def __init__(self, path: Path) -> None:
        self._lock = threading.Lock()
        self._db = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
        self._db.row_factory = sqlite3.Row
        self._db.execute("PRAGMA foreign_keys = ON")
        self._db.executescript(SCHEMA)

    def close(self) -> None:
        self._db.close()

    def _execute(self, sql: str, params: tuple[str | int | None, ...] = ()) -> list[sqlite3.Row]:
        with self._lock:
            return self._db.execute(sql, params).fetchall()

    def create_project(self, name: str) -> Project:
        project = Project(id=str(uuid4()), name=name, created_at=now())
        self._execute(
            "INSERT INTO projects VALUES (?, ?, ?)", (project.id, project.name, project.created_at.isoformat())
        )
        return project

    def list_projects(self) -> list[Project]:
        return [
            Project.model_validate(dict(row)) for row in self._execute("SELECT * FROM projects ORDER BY created_at")
        ]

    def get_project(self, project_id: str) -> Project | None:
        rows = self._execute("SELECT * FROM projects WHERE id = ?", (project_id,))
        return Project.model_validate(dict(rows[0])) if rows else None

    def delete_project(self, project_id: str) -> None:
        self._execute("DELETE FROM projects WHERE id = ?", (project_id,))

    def create_session(self, project_id: str) -> Session:
        """Insert a starting session.

        :raises sqlite3.IntegrityError: when the project already has a live session.
        """
        at = now()
        session = Session(
            id=str(uuid4()),
            project_id=project_id,
            status=SessionStatus.STARTING,
            reason=None,
            port=None,
            created_at=at,
            last_activity=at,
            closed_at=None,
        )
        self._execute(
            "INSERT INTO sessions (id, project_id, status, created_at, last_activity) VALUES (?, ?, ?, ?, ?)",
            (session.id, project_id, session.status, at.isoformat(), at.isoformat()),
        )
        return session

    def list_sessions(self, *, live_only: bool = False) -> list[Session]:
        rows = self._execute("SELECT * FROM sessions ORDER BY created_at")
        sessions = [Session.model_validate(dict(row)) for row in rows]
        return [session for session in sessions if session.status in LIVE_STATUSES] if live_only else sessions

    def get_session(self, session_id: str) -> Session | None:
        rows = self._execute("SELECT * FROM sessions WHERE id = ?", (session_id,))
        return Session.model_validate(dict(rows[0])) if rows else None

    def mark_running(self, session_id: str, port: int) -> None:
        self._execute(
            "UPDATE sessions SET status = ?, port = ? WHERE id = ?", (SessionStatus.RUNNING, port, session_id)
        )

    def touch(self, session_id: str) -> None:
        self._execute("UPDATE sessions SET last_activity = ? WHERE id = ?", (now().isoformat(), session_id))

    def finish(self, session_id: str, status: SessionStatus, reason: str) -> None:
        self._execute(
            "UPDATE sessions SET status = ?, reason = ?, closed_at = ? WHERE id = ?",
            (status, reason, now().isoformat(), session_id),
        )

    def fail_live_sessions(self, reason: str) -> None:
        for session in self.list_sessions(live_only=True):
            self.finish(session.id, SessionStatus.FAILED, reason)
