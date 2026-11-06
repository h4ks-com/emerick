import asyncio
import os
import re
import resource
import signal
import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import httpx
from pydantic import BaseModel, JsonValue, TypeAdapter, ValidationError

from emerick_service.config import Settings

REQUIRED_FLAGS = frozenset(
    {
        "--headless",
        "--script",
        "--audio",
        "--midi",
        "--mcp-port",
        "--profile",
        "--agent-read-root",
        "--agent-write-root",
    }
)
MEGABYTE = 1 << 20


class MissingFlagsError(RuntimeError):
    pass


class ChildError(RuntimeError):
    pass


class ActionResult(BaseModel):
    error: str | None = None


class ToolResult(BaseModel):
    structuredContent: ActionResult  # noqa: N815


class RpcError(BaseModel):
    message: str


class RpcReply(BaseModel):
    result: ToolResult | None = None
    error: RpcError | None = None


JSON_REPLY = TypeAdapter(dict[str, JsonValue])


LIBRARY_ENV = ("EMERICK_LIBRARY_DIR", "EMERICK_LIBRARY_INSTALL")


def child_env(home: Path) -> dict[str, str]:
    runtime = home / "runtime"
    runtime.mkdir(mode=0o700, parents=True, exist_ok=True)
    # The session's temporary files, such as the stems a render keeps, go with its work folder when it closes.
    temporary = home / "tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    return {
        "HOME": str(home),
        "TMPDIR": str(temporary),
        "XDG_RUNTIME_DIR": str(runtime),
        "QT_QPA_PLATFORM": "offscreen",
        "QT_QUICK_BACKEND": "software",
        "LC_ALL": "C.UTF-8",
        **{name: os.environ[name] for name in LIBRARY_ENV if name in os.environ},
    }


def sandbox_args(settings: Settings, profile: Path, project_dir: Path) -> list[str]:
    """Return the flags that keep an Emerick run inside project_dir, leaving out the Lua timeout."""
    args = [
        "--audio=null",
        "--midi=virtual",
        f"--profile={profile}",
        f"--agent-read-root={project_dir}",
        f"--agent-write-root={project_dir}",
    ]
    limits = {"--lua-memory-mb": settings.lua_memory_mb, "--max-render-seconds": settings.max_render_seconds}
    args += [f"{flag}={value}" for flag, value in limits.items() if value is not None]
    if settings.bundled_plugins_only:
        args.append("--bundled-plugins-only")
    return args


def child_args(settings: Settings, port: int, profile: Path, project_dir: Path) -> list[str]:
    args = [str(settings.bin), "--headless", f"--mcp-port={port}", *sandbox_args(settings, profile, project_dir)]
    if settings.lua_timeout_ms is not None:
        args.append(f"--lua-timeout-ms={settings.lua_timeout_ms}")
    return args


def check_flags(settings: Settings, home: Path) -> None:
    """Check that the binary knows every flag we pass it.

    :raises MissingFlagsError: naming the flags its ``--help`` leaves out.
    """
    shown = subprocess.run(
        [str(settings.bin), "--help"], env=child_env(home), capture_output=True, text=True, check=False, timeout=60
    )
    known = set(re.findall(r"--[a-z0-9-]+", shown.stdout))
    wanted = {arg.split("=")[0] for arg in child_args(settings, 0, home, home)[1:]} | REQUIRED_FLAGS
    if missing := sorted(wanted - known):
        raise MissingFlagsError(f"{settings.bin} lacks the flags {', '.join(missing)}")


def cap(limit: int, value: int) -> None:
    _, hard = resource.getrlimit(limit)
    capped = value if hard == resource.RLIM_INFINITY else min(value, hard)
    resource.setrlimit(limit, (capped, capped))


def limits(settings: Settings) -> Callable[[], None]:
    def apply() -> None:
        # macOS refuses to lower RLIMIT_DATA, and we only serve sessions on Linux.
        if sys.platform == "linux":
            cap(resource.RLIMIT_DATA, settings.session_memory_mb * MEGABYTE)
        cap(resource.RLIMIT_FSIZE, settings.session_max_file_mb * MEGABYTE)
        cap(resource.RLIMIT_NOFILE, settings.session_max_open_files)
        cap(resource.RLIMIT_CORE, 0)

    return apply


@dataclass
class Child:
    process: asyncio.subprocess.Process
    port: int
    project_dir: Path
    work_dir: Path

    @property
    def mcp_url(self) -> str:
        return f"http://127.0.0.1:{self.port}/mcp"

    async def run_action(self, client: httpx.AsyncClient, action: str, params: dict[str, str]) -> None:
        """Run one registry action over the child's MCP.

        :raises ChildError: when the call or the action fails.
        :raises httpx.HTTPError: when the child does not answer.
        """
        body = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": "actions.run", "arguments": {"id": action, "params": params}},
        }
        response = await client.post(self.mcp_url, json=body, timeout=120)
        try:
            reply = RpcReply.model_validate_json(response.content)
        except ValidationError as error:
            raise ChildError(f"{action}: HTTP {response.status_code} with no JSON-RPC reply") from error
        error = reply.error.message if reply.error else reply.result.structuredContent.error if reply.result else None
        if error is not None:
            raise ChildError(f"{action}: {error}")

    async def rpc(
        self, client: httpx.AsyncClient, method: str, params: dict[str, JsonValue], timeout: float
    ) -> JsonValue:
        """Send one MCP request to the child and return its result, or its error when it has one.

        :raises ChildError: when the child answers with no JSON-RPC reply.
        :raises httpx.HTTPError: when the child does not answer.
        """
        body = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params}
        response = await client.post(self.mcp_url, json=body, timeout=timeout)
        try:
            reply = JSON_REPLY.validate_json(response.content)
        except ValidationError as error:
            raise ChildError(f"{method}: HTTP {response.status_code} with no JSON-RPC reply") from error
        return reply.get("error") or reply.get("result")

    async def wait_ready(self, client: httpx.AsyncClient, timeout: float) -> None:
        """Wait until the child answers MCP.

        :raises ChildError: when it exits or stays silent past timeout.
        """
        ping = {"jsonrpc": "2.0", "id": 0, "method": "ping"}
        async with asyncio.timeout(timeout):
            while self.process.returncode is None:
                try:
                    if (await client.post(self.mcp_url, json=ping, timeout=2)).status_code == 200:
                        return
                except httpx.TransportError:
                    pass
                await asyncio.sleep(0.2)
        raise ChildError(f"emerick exited with code {self.process.returncode} while starting")

    async def stop(self, timeout: float) -> None:
        await stop_process(self.process, timeout)


async def stop_process(process: asyncio.subprocess.Process, timeout: float) -> None:
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            return
        try:
            await asyncio.wait_for(process.wait(), timeout)
        except TimeoutError:
            continue
        return


async def start_child(settings: Settings, port: int, project_dir: Path, work_dir: Path) -> Child:
    profile = work_dir / "profile"
    profile.mkdir(parents=True, exist_ok=True)
    with (work_dir / "emerick.log").open("ab") as log:
        process = await asyncio.create_subprocess_exec(
            *child_args(settings, port, profile, project_dir),
            env=child_env(work_dir),
            cwd=work_dir,
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=subprocess.STDOUT,
            preexec_fn=limits(settings),
            start_new_session=True,
        )
    return Child(process=process, port=port, project_dir=project_dir, work_dir=work_dir)
