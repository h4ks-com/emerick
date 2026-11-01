import base64
from typing import Annotated

from mcp.server import MCPServer
from mcp.server.mcpserver import Context
from pydantic import Field, JsonValue
from starlette.requests import Request

from emerick_service.db import Database
from emerick_service.models import Project, ProjectFile, SessionView
from emerick_service.sessions import SessionManager

INSTRUCTIONS = (
    "Create a project, start a session on it and drive Emerick with session.call, or connect to the session's "
    "mcp_url directly. Keep every file path inside the session's project_dir. "
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
        name="project.upload",
        description=(
            "Write a file, such as a style, sample or voice list, into a project at path, from base64 content. "
            "A live session sees it at once in its project_dir."
        ),
    )
    async def project_upload(project_id: str, path: str, content_base64: str) -> ProjectFile:
        return await manager.upload(project_id, path, base64.b64decode(content_base64, validate=True))

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

    def call_timeout() -> float:
        return float((manager.settings.max_render_seconds or 3600) + 60)

    @mcp.tool(
        name="session.tools",
        description="List the tools of a live session's Emerick, such as actions.run, state.get and lua.eval.",
    )
    async def session_tools(session_id: str) -> JsonValue:
        db.touch(session_id)
        return await manager.running_child(session_id).rpc(manager.client, "tools/list", {}, call_timeout())

    @mcp.tool(
        name="session.call",
        description=(
            "Call one tool of a live session's Emerick with its arguments, such as lua.eval with {code} to run a "
            "whole song script, actions.run with {id, params}, or state.get with {name}, and return its result."
        ),
    )
    async def session_call(session_id: str, tool: str, arguments: dict[str, JsonValue]) -> JsonValue:
        db.touch(session_id)
        child = manager.running_child(session_id)
        return await child.rpc(manager.client, "tools/call", {"name": tool, "arguments": arguments}, call_timeout())

    @mcp.tool(
        name="session.close",
        description="Save the session's project and stop the session. Always close a session when you are done.",
    )
    async def session_close(session_id: str, ctx: Context[None, Request]) -> SessionView:
        await manager.close(session_id, "closed by request")
        return session_view(manager, session_id, base(ctx))

    return mcp
