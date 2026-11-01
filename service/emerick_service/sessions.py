import asyncio
import logging
import shutil
import socket
import sqlite3
from pathlib import Path

import httpx
from botocore.exceptions import BotoCoreError, ClientError

from emerick_service.child import Child, ChildError, start_child
from emerick_service.config import Settings
from emerick_service.db import Database, now
from emerick_service.models import Session, SessionStatus
from emerick_service.storage import Storage, usage_bytes

log = logging.getLogger(__name__)
BUNDLE = "project.erk"
MEGABYTE = 1 << 20
StorageError = (OSError, BotoCoreError, ClientError)


class PoolFullError(RuntimeError):
    pass


class ProjectBusyError(RuntimeError):
    pass


class UnknownProjectError(LookupError):
    pass


class UnknownSessionError(LookupError):
    pass


class SessionStartError(RuntimeError):
    pass


def port_free(port: int) -> bool:
    with socket.socket() as probe:
        try:
            probe.bind(("127.0.0.1", port))
        except OSError:
            return False
        return True


class SessionManager:
    def __init__(self, settings: Settings, db: Database, storage: Storage, client: httpx.AsyncClient) -> None:
        self.settings = settings
        self.db = db
        self.storage = storage
        self.client = client
        self.children: dict[str, Child] = {}
        self._ports: dict[str, int] = {}
        self._closing: set[str] = set()
        self._watchers: set[asyncio.Task[None]] = set()
        self._admission = asyncio.Lock()

    def _pick_port(self) -> int:
        taken = set(self._ports.values())
        for port in range(self.settings.port_min, self.settings.port_max + 1):
            if port not in taken and port_free(port):
                return port
        raise PoolFullError("no free port left in the configured range")

    async def _admit(self, project_id: str) -> tuple[Session, int]:
        if self.db.get_project(project_id) is None:
            raise UnknownProjectError(project_id)
        async with self._admission:
            if len(self.db.list_sessions(live_only=True)) >= self.settings.max_sessions:
                raise PoolFullError(
                    f"all {self.settings.max_sessions} sessions are in use, close one with DELETE /sessions/{{id}}"
                )
            try:
                session = self.db.create_session(project_id)
            except sqlite3.IntegrityError as error:
                raise ProjectBusyError(f"project {project_id} already has a live session") from error
            try:
                port = self._pick_port()
            except PoolFullError:
                self.db.finish(session.id, SessionStatus.FAILED, "no free port")
                raise
            self._ports[session.id] = port
            return session, port

    async def create(self, project_id: str) -> Session:
        """Start a headless Emerick on the project.

        :raises UnknownProjectError: when there is no such project.
        :raises PoolFullError: when every session is in use.
        :raises ProjectBusyError: when the project already has a live session.
        :raises SessionStartError: when the child does not start.
        """
        session, port = await self._admit(project_id)
        work_dir = self.settings.work_dir / session.id
        try:
            work_dir.mkdir(parents=True)
            project_dir = await asyncio.to_thread(self.storage.checkout, project_id, work_dir)
            child = await start_child(self.settings, port, project_dir, work_dir)
            self.children[session.id] = child
            await child.wait_ready(self.client, self.settings.start_timeout_seconds)
            await self._open_project(child)
        except (*StorageError, ChildError, httpx.HTTPError, TimeoutError) as error:
            await self._discard(session.id, work_dir)
            self.db.finish(session.id, SessionStatus.FAILED, f"start failed: {error}")
            raise SessionStartError(f"start failed: {error}") from error
        self.db.mark_running(session.id, port)
        watcher = asyncio.create_task(self._watch(session.id, child))
        self._watchers.add(watcher)
        watcher.add_done_callback(self._watchers.discard)
        return self.get(session.id)

    async def _open_project(self, child: Child) -> None:
        bundle = child.project_dir / BUNDLE
        if (bundle / "project.json").is_file():
            await child.run_action(self.client, "project.open", {"path": str(bundle)})
        else:
            await child.run_action(self.client, "project.new", {"path": str(bundle)})

    async def _discard(self, session_id: str, work_dir: Path) -> None:
        if child := self.children.pop(session_id, None):
            await child.stop(self.settings.stop_timeout_seconds)
        self._ports.pop(session_id, None)
        shutil.rmtree(work_dir, ignore_errors=True)

    async def _watch(self, session_id: str, child: Child) -> None:
        code = await child.process.wait()
        if session_id in self._closing or self.children.get(session_id) is not child:
            return
        self._closing.add(session_id)
        reason = f"emerick exited with code {code}"
        log.warning("session %s: %s", session_id, reason)
        await self._commit_and_finish(session_id, child, SessionStatus.FAILED, reason)

    async def _commit_and_finish(self, session_id: str, child: Child, status: SessionStatus, reason: str) -> None:
        session = self.get(session_id)
        try:
            await asyncio.to_thread(self.storage.commit, session.project_id, child.project_dir)
        except StorageError as error:
            log.exception("session %s: keeping %s, storing it failed", session_id, child.work_dir)
            self.db.finish(session_id, SessionStatus.FAILED, f"{reason}; storing the project failed: {error}")
        else:
            self.db.finish(session_id, status, reason)
            shutil.rmtree(child.work_dir, ignore_errors=True)
        self.children.pop(session_id, None)
        self._ports.pop(session_id, None)
        self._closing.discard(session_id)

    def get(self, session_id: str) -> Session:
        """:raises UnknownSessionError: when there is no such session."""
        session = self.db.get_session(session_id)
        if session is None:
            raise UnknownSessionError(session_id)
        return session

    def running_child(self, session_id: str) -> Child:
        """:raises UnknownSessionError: when the session is not running."""
        child = self.children.get(session_id)
        if child is None or session_id in self._closing or self.get(session_id).status != SessionStatus.RUNNING:
            raise UnknownSessionError(session_id)
        return child

    async def save(self, session_id: str) -> None:
        """Save the project and store it.

        :raises UnknownSessionError: when the session is not running.
        :raises ChildError: when the app cannot save.
        """
        child = self.running_child(session_id)
        await child.run_action(self.client, "project.save", {})
        await asyncio.to_thread(self.storage.commit, self.get(session_id).project_id, child.project_dir)

    async def close(self, session_id: str, reason: str) -> Session:
        """Save the project, stop the child and store the project.

        :raises UnknownSessionError: when there is no such session.
        """
        child = self.children.get(session_id)
        if child is None or session_id in self._closing:
            return self.get(session_id)
        self._closing.add(session_id)
        try:
            await child.run_action(self.client, "project.save", {})
        except (ChildError, httpx.HTTPError) as error:
            log.warning("session %s: saving before close failed: %s", session_id, error)
        await child.stop(self.settings.stop_timeout_seconds)
        await self._commit_and_finish(session_id, child, SessionStatus.CLOSED, reason)
        return self.get(session_id)

    def expiry_reason(self, session: Session, usage: int) -> str | None:
        at = now()
        if (at - session.last_activity).total_seconds() > self.settings.session_idle_seconds:
            return "idle timeout"
        if (at - session.created_at).total_seconds() > self.settings.session_max_seconds:
            return "session time limit"
        if usage > self.settings.project_max_mb * MEGABYTE:
            return "project disk quota exceeded"
        return None

    async def reap_once(self) -> None:
        for session in self.db.list_sessions(live_only=True):
            child = self.children.get(session.id)
            if child is None or session.status != SessionStatus.RUNNING:
                continue
            usage = await asyncio.to_thread(usage_bytes, child.project_dir)
            if reason := self.expiry_reason(session, usage):
                log.info("session %s: closing, %s", session.id, reason)
                await self.close(session.id, reason)

    async def run_reaper(self) -> None:
        while True:
            await asyncio.sleep(self.settings.reaper_interval_seconds)
            await self.reap_once()

    async def close_all(self, reason: str) -> None:
        await asyncio.gather(*(self.close(session_id, reason) for session_id in list(self.children)))
