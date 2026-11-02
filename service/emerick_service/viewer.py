import asyncio
import hashlib
import logging
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from uuid import uuid4

from emerick_service.child import child_env, limits, sandbox_args, stop_process
from emerick_service.config import Settings
from emerick_service.db import Database
from emerick_service.models import ProjectFile
from emerick_service.sessions import StorageError
from emerick_service.storage import Storage, relative_path, walk

log = logging.getLogger(__name__)
AUDIO_SUFFIXES = frozenset({".mp3", ".ogg", ".opus", ".flac", ".wav"})
RENDERED_MIX = "viewer-mix.mp3"
PEAKS_VIDEO = "viewer-peaks.mp4"
LOG_TAIL_BYTES = 2000
# video.render loads the waveform of every audio clip, and loading one writes its peak file, so we render a
# one beat thumbnail video only for that side effect.
RENDER_SCRIPT = """\
local bundle, mix, peaks = arg[1], arg[2], arg[3]
emerick.run("project.open", {path = bundle})
emerick.run("project.waitReady")
if mix ~= "" then
    emerick.run("export.mix", {path = mix})
end
if peaks ~= "" then
    local ok, error = pcall(emerick.run, "video.render", {path = peaks, width = 128, height = 128, fps = 1, to = 960})
    if not ok then
        print("writing peaks failed: " .. tostring(error))
    end
end
"""

type RenderKey = tuple[str, str, str]


class RenderError(RuntimeError):
    pass


@dataclass(frozen=True)
class MixState:
    path: str | None
    rendering: bool
    error: str | None


def choose_bundle(files: list[ProjectFile], wanted: str | None) -> str:
    """Return the bundle folder named wanted, or else the one whose project.json changed last.

    :raises PathEscapeError: when wanted leaves the project.
    :raises FileNotFoundError: when the project holds no such bundle.
    """
    bundles = {
        PurePosixPath(file.path).parent.as_posix(): file.modified_at
        for file in files
        if PurePosixPath(file.path).name == "project.json" and PurePosixPath(file.path).parent.suffix == ".erk"
    }
    if wanted is not None:
        bundle = relative_path(wanted).as_posix().rstrip("/")
        if bundle not in bundles:
            raise FileNotFoundError(f"the project has no bundle {bundle}")
        return bundle
    if not bundles:
        raise FileNotFoundError("the project has no bundle yet, save it from a session first")
    return max(bundles, key=lambda bundle: bundles[bundle])


def choose_mix(files: list[ProjectFile], bundle: str, rendered_is_current: bool) -> str | None:
    """Return the newest audio export at the project root or in the bundle, skipping a stale rendered mix."""
    stale = None if rendered_is_current else f"{bundle}/{RENDERED_MIX}"
    exports = [
        file
        for file in files
        if PurePosixPath(file.path).suffix.lower() in AUDIO_SUFFIXES
        and PurePosixPath(file.path).parent.as_posix() in {".", bundle}
        and file.path != stale
    ]
    return max(exports, key=lambda file: file.modified_at).path if exports else None


def lacks_peaks(files: list[ProjectFile], bundle: str) -> bool:
    paths = {file.path for file in files}
    audio = PurePosixPath(bundle) / "audio"
    return any(
        PurePosixPath(path).parent == audio
        and PurePosixPath(path).suffix.lower() in AUDIO_SUFFIXES
        and (audio / ".peaks" / f"{PurePosixPath(path).name}.peaks").as_posix() not in paths
        for path in paths
    )


def render_args(settings: Settings, script: Path, profile: Path, project_dir: Path) -> list[str]:
    return [str(settings.bin), f"--script={script}", *sandbox_args(settings, profile, project_dir)]


def read_all(storage: Storage, project_id: str, path: str) -> bytes:
    return b"".join(storage.read(project_id, path))


class Renderer:
    """Renders missing mixes and peak files with one-shot Emerick runs, at most max_renders at once."""

    def __init__(self, settings: Settings, db: Database, storage: Storage) -> None:
        self.settings = settings
        self.db = db
        self.storage = storage
        self._slots = asyncio.Semaphore(settings.max_renders)
        self._jobs: dict[RenderKey, asyncio.Task[None]] = {}
        # We remember every finished key until restart, so a render that fails or writes no peaks never loops.
        self._results: dict[RenderKey, str | None] = {}

    async def mix(self, project_id: str, wanted_bundle: str | None) -> MixState:
        """Find the bundle's mix, and start a render when it has none or its audio clips lack peaks.

        :raises PathEscapeError: when wanted_bundle leaves the project.
        :raises FileNotFoundError: when the project holds no such bundle.
        """
        files = await asyncio.to_thread(self.storage.files, project_id)
        bundle = choose_bundle(files, wanted_bundle)
        project_json = await asyncio.to_thread(read_all, self.storage, project_id, f"{bundle}/project.json")
        digest = hashlib.sha256(project_json).hexdigest()
        mix = choose_mix(files, bundle, self.db.rendered_hash(project_id, bundle) == digest)
        peaks = lacks_peaks(files, bundle)
        key = (project_id, bundle, digest)
        mix_vanished = mix is None and key in self._results and self._results[key] is None
        if (mix is None or peaks) and key not in self._jobs and (key not in self._results or mix_vanished):
            known = {file.path for file in files}
            job = asyncio.create_task(self._render(key, render_mix=mix is None, peaks=peaks, known=known))
            self._jobs[key] = job
        return MixState(path=mix, rendering=key in self._jobs, error=self._results.get(key))

    async def _render(self, key: RenderKey, *, render_mix: bool, peaks: bool, known: set[str]) -> None:
        project_id, bundle, digest = key
        work_dir = self.settings.work_dir / f"render-{uuid4()}"
        result = None
        try:
            async with self._slots:
                work_dir.mkdir(parents=True)
                project_dir = await asyncio.to_thread(self._checkout, project_id, bundle, work_dir)
                mix = project_dir / bundle / RENDERED_MIX if render_mix else None
                await self._run(work_dir, project_dir, bundle, mix, project_dir / PEAKS_VIDEO if peaks else None)
                await asyncio.to_thread(self._store, project_id, project_dir, bundle, mix, known)
                if mix is not None:
                    self.db.set_rendered_hash(project_id, bundle, digest)
        except (*StorageError, RenderError) as error:
            log.warning("rendering %s of project %s failed: %s", bundle, project_id, error)
            result = str(error)
        finally:
            self._results[key] = result
            self._jobs.pop(key, None)
            shutil.rmtree(work_dir, ignore_errors=True)

    def _checkout(self, project_id: str, bundle: str, work_dir: Path) -> Path:
        project_dir = self.storage.checkout(project_id, work_dir)
        if project_dir.is_relative_to(work_dir):
            return project_dir
        # Local storage hands out the live folder, which a session may hold, so we render from a copy.
        copy = work_dir / "project"
        shutil.copytree(project_dir / bundle, copy / bundle, symlinks=True)
        return copy

    async def _run(self, work_dir: Path, project_dir: Path, bundle: str, mix: Path | None, peaks: Path | None) -> None:
        script = work_dir / "render.lua"
        script.write_text(RENDER_SCRIPT)
        args = render_args(self.settings, script, work_dir / "profile", project_dir)
        args += ["--", str(project_dir / bundle), str(mix or ""), str(peaks or "")]
        log_path = work_dir / "emerick.log"
        with log_path.open("ab") as output:
            process = await asyncio.create_subprocess_exec(
                *args,
                env=child_env(work_dir),
                cwd=work_dir,
                stdin=subprocess.DEVNULL,
                stdout=output,
                stderr=subprocess.STDOUT,
                preexec_fn=limits(self.settings),
                start_new_session=True,
            )
        try:
            code = await asyncio.wait_for(process.wait(), self.settings.render_timeout_seconds)
        except TimeoutError as error:
            raise RenderError(f"the render took longer than {self.settings.render_timeout_seconds:g} s") from error
        finally:
            if process.returncode is None:
                await stop_process(process, self.settings.stop_timeout_seconds)
        if code != 0:
            tail = log_path.read_bytes()[-LOG_TAIL_BYTES:].decode(errors="replace").strip()
            raise RenderError(f"emerick exited with code {code}: {tail}")
        if mix is not None and not mix.is_file():
            raise RenderError("the render wrote no mix")

    def _store(self, project_id: str, project_dir: Path, bundle: str, mix: Path | None, known: set[str]) -> None:
        if mix is not None:
            self.storage.write(project_id, f"{bundle}/{RENDERED_MIX}", mix.read_bytes())
        peaks_dir = project_dir / bundle / "audio" / ".peaks"
        for name, _ in walk(peaks_dir) if peaks_dir.is_dir() else ():
            path = f"{bundle}/audio/.peaks/{name}"
            if path not in known:
                self.storage.write(project_id, path, (peaks_dir / name).read_bytes())

    async def close(self) -> None:
        jobs = list(self._jobs.values())
        for job in jobs:
            job.cancel()
        await asyncio.gather(*jobs, return_exceptions=True)
