from typing import Annotated

from mcp.server import MCPServer
from mcp.server.mcpserver import Context
from pydantic import Field
from starlette.requests import Request

from emerick_service.db import Database
from emerick_service.models import Project, SessionView
from emerick_service.sessions import SessionManager

INSTRUCTIONS = (
    "Create a project, start a session on it and drive Emerick through the session's mcp_url. "
    "Close the session with session.close as soon as you are done, so the project is saved and the slot is freed."
)


def base_url(public_url: str | None, request: Request | None) -> str:
    if public_url:
        return public_url.rstrip("/")
    return str(request.base_url).rstrip("/") if request else ""


def session_view(manager: SessionManager, session_id: str, base: str) -> SessionView:
    session = manager.get(session_id)
    child = manager.children.get(session_id)
    return SessionView(
        **session.model_dump(),
        mcp_url=f"{base}/sessions/{session.id}/mcp",
        project_dir=str(child.project_dir) if child else None,
    )


def build_mcp(manager: SessionManager, db: Database) -> MCPServer:
    mcp = MCPServer("emerick-service", instructions=INSTRUCTIONS)

    def base(ctx: Context[None, Request]) -> str:
        return base_url(manager.settings.public_url, ctx.request_context.request)

    @mcp.tool(name="project.create", description="Create an empty project and return its id.")
    def project_create(name: Annotated[str, Field(min_length=1, max_length=100)]) -> Project:
        project = db.create_project(name)
        manager.storage.create(project.id)
        return project

    @mcp.tool(name="project.list", description="List the projects.")
    def project_list() -> list[Project]:
        return db.list_projects()

    @mcp.tool(
        name="session.create",
        description=(
            "Start an Emerick session on a project and return its mcp_url, an MCP server with the whole DAW, "
            "and its project_dir, the only folder it reads and writes, so export there. "
            "Close it with session.close when you are done."
        ),
    )
    async def session_create(project_id: str, ctx: Context[None, Request]) -> SessionView:
        session = await manager.create(project_id)
        return session_view(manager, session.id, base(ctx))

    @mcp.tool(name="session.list", description="List the sessions, live and closed.")
    def session_list(ctx: Context[None, Request]) -> list[SessionView]:
        return [session_view(manager, session.id, base(ctx)) for session in db.list_sessions()]

    @mcp.tool(
        name="session.close",
        description="Save the session's project and stop the session. Always close a session when you are done.",
    )
    async def session_close(session_id: str, ctx: Context[None, Request]) -> SessionView:
        await manager.close(session_id, "closed by request")
        return session_view(manager, session_id, base(ctx))

    return mcp
