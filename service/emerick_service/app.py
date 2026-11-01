import asyncio
import contextlib
import mimetypes
from collections.abc import AsyncGenerator, Callable
from contextlib import asynccontextmanager
from pathlib import PurePosixPath
from urllib.parse import quote

import boto3
import httpx
from fastapi import FastAPI, Request, Response
from fastapi.responses import JSONResponse, StreamingResponse
from mcp.server.transport_security import TransportSecuritySettings
from mypy_boto3_s3 import S3Client
from starlette.background import BackgroundTask
from starlette.routing import Mount

from emerick_service.child import ChildError, check_flags
from emerick_service.config import Settings, StorageKind
from emerick_service.db import Database
from emerick_service.mcp_tools import base_url, build_mcp, session_view
from emerick_service.models import (
    LIVE_STATUSES,
    Health,
    Project,
    ProjectCreate,
    ProjectFile,
    SessionCreate,
    SessionView,
)
from emerick_service.sessions import (
    PoolFullError,
    ProjectBusyError,
    QuotaExceededError,
    SessionManager,
    SessionStartError,
    UnknownProjectError,
    UnknownSessionError,
)
from emerick_service.storage import LocalStorage, PathEscapeError, S3Storage, Storage, file_chunks

FORWARDED_REQUEST_HEADERS = ("content-type", "accept", "mcp-session-id", "mcp-protocol-version", "last-event-id")
FORWARDED_RESPONSE_HEADERS = ("content-type", "mcp-session-id", "cache-control")
ERROR_STATUS: dict[type[Exception], int] = {
    UnknownProjectError: 404,
    UnknownSessionError: 404,
    FileNotFoundError: 404,
    PathEscapeError: 400,
    ProjectBusyError: 409,
    QuotaExceededError: 413,
    PoolFullError: 429,
    SessionStartError: 502,
    ChildError: 502,
    httpx.HTTPError: 502,
}


def make_storage(settings: Settings) -> Storage:
    if settings.storage == StorageKind.S3:
        if not settings.s3_bucket:
            raise ValueError("EMERICK_S3_BUCKET is required with EMERICK_STORAGE=s3")
        # The stubs only type the services they install, so the client overloads stay partly unknown.
        client: S3Client = boto3.client("s3", endpoint_url=settings.s3_endpoint_url)  # pyright: ignore[reportUnknownMemberType]
        return S3Storage(client, settings.s3_bucket, settings.s3_prefix)
    return LocalStorage(settings.projects_dir)


def error_handler(status: int) -> Callable[[Request, Exception], Response]:
    def handle(_: Request, error: Exception) -> Response:
        return JSONResponse({"detail": str(error)}, status_code=status)

    return handle


def create_app(settings: Settings, storage: Storage | None = None) -> FastAPI:
    settings.data_dir.mkdir(parents=True, exist_ok=True)
    settings.work_dir.mkdir(parents=True, exist_ok=True)
    db = Database(settings.db_path)
    client = httpx.AsyncClient(timeout=httpx.Timeout(10, read=None))
    manager = SessionManager(settings, db, storage or make_storage(settings), client)
    mcp = build_mcp(manager, db)
    mcp_app = mcp.streamable_http_app(
        json_response=True,
        stateless_http=True,
        transport_security=TransportSecuritySettings(enable_dns_rebinding_protection=False),
    )

    @asynccontextmanager
    async def lifespan(_: FastAPI) -> AsyncGenerator[None]:
        check_flags(settings, settings.work_dir)
        db.fail_live_sessions("the service restarted")
        reaper = asyncio.create_task(manager.run_reaper())
        async with mcp.session_manager.run():
            yield
        reaper.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await reaper
        await manager.close_all("the service stopped")
        await client.aclose()
        db.close()

    app = FastAPI(title="Emerick service", lifespan=lifespan)
    app.state.manager = manager
    for error, status in ERROR_STATUS.items():
        app.add_exception_handler(error, error_handler(status))

    def view(request: Request, session_id: str) -> SessionView:
        return session_view(manager, session_id, base_url(settings.public_url, request))

    def project_or_404(project_id: str) -> Project:
        project = db.get_project(project_id)
        if project is None:
            raise UnknownProjectError(f"no project {project_id}")
        return project

    @app.get("/health")
    def health() -> Health:
        return Health(sessions_live=len(db.list_sessions(live_only=True)), max_sessions=settings.max_sessions)

    @app.post("/projects", status_code=201)
    def create_project(body: ProjectCreate) -> Project:
        project = db.create_project(body.name)
        manager.storage.create(project.id)
        return project

    @app.get("/projects")
    def list_projects() -> list[Project]:
        return db.list_projects()

    @app.get("/projects/{project_id}")
    def get_project(project_id: str) -> Project:
        return project_or_404(project_id)

    @app.delete("/projects/{project_id}", status_code=204)
    async def delete_project(project_id: str) -> None:
        project_or_404(project_id)
        if any(session.project_id == project_id for session in db.list_sessions(live_only=True)):
            raise ProjectBusyError(f"project {project_id} has a live session, close it first")
        await asyncio.to_thread(manager.storage.delete, project_id)
        db.delete_project(project_id)

    @app.get("/projects/{project_id}/files")
    async def list_files(project_id: str) -> list[ProjectFile]:
        project_or_404(project_id)
        return await asyncio.to_thread(manager.storage.files, project_id)

    @app.get("/projects/{project_id}/files/{path:path}")
    async def download_file(project_id: str, path: str) -> StreamingResponse:
        project_or_404(project_id)
        chunks = await asyncio.to_thread(manager.storage.read, project_id, path)
        name = PurePosixPath(path).name
        return StreamingResponse(
            chunks,
            media_type=mimetypes.guess_type(name)[0] or "application/octet-stream",
            headers={"Content-Disposition": f"attachment; filename*=utf-8''{quote(name)}"},
        )

    @app.get("/projects/{project_id}/bundle")
    async def download_bundle(project_id: str, path: str) -> StreamingResponse:
        archive = await manager.bundle_zip(project_id, path)
        name = f"{PurePosixPath(path).name}.zip"
        return StreamingResponse(
            file_chunks(archive),
            media_type="application/zip",
            headers={"Content-Disposition": f"attachment; filename*=utf-8''{quote(name)}"},
            background=BackgroundTask(archive.close),
        )

    @app.put("/projects/{project_id}/files/{path:path}", status_code=201)
    async def upload_file(request: Request, project_id: str, path: str) -> ProjectFile:
        return await manager.upload(project_id, path, await request.body())

    @app.post("/sessions", status_code=201)
    async def create_session(request: Request, body: SessionCreate) -> SessionView:
        session = await manager.create(body.project_id)
        return view(request, session.id)

    @app.get("/sessions")
    def list_sessions(request: Request, live: bool = False) -> list[SessionView]:
        return [view(request, session.id) for session in db.list_sessions(live_only=live)]

    @app.get("/sessions/{session_id}")
    def get_session(request: Request, session_id: str) -> SessionView:
        return view(request, session_id)

    @app.post("/sessions/{session_id}/save")
    async def save_session(request: Request, session_id: str) -> SessionView:
        await manager.save(session_id)
        return view(request, session_id)

    @app.delete("/sessions/{session_id}")
    async def close_session(request: Request, session_id: str) -> SessionView:
        if manager.get(session_id).status in LIVE_STATUSES:
            await manager.close(session_id, "closed by request")
        return view(request, session_id)

    @app.api_route("/sessions/{session_id}/mcp", methods=["GET", "POST", "DELETE"])
    async def proxy_mcp(request: Request, session_id: str) -> StreamingResponse:
        child = manager.running_child(session_id)
        db.touch(session_id)
        headers = {name: value for name, value in request.headers.items() if name in FORWARDED_REQUEST_HEADERS}
        upstream = client.build_request(request.method, child.mcp_url, headers=headers, content=await request.body())
        response = await client.send(upstream, stream=True)
        return StreamingResponse(
            response.aiter_raw(),
            status_code=response.status_code,
            headers={name: value for name, value in response.headers.items() if name in FORWARDED_RESPONSE_HEADERS},
            background=BackgroundTask(response.aclose),
        )

    app.router.routes.append(Mount("/", app=mcp_app))
    return app
