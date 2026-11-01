from enum import StrEnum
from pathlib import Path

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


class StorageKind(StrEnum):
    LOCAL = "local"
    S3 = "s3"


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="EMERICK_", env_parse_none_str="none")

    host: str = "127.0.0.1"
    port: int = 8080
    public_url: str | None = None
    bin: Path = Path("emerick")
    data_dir: Path = Path("data")
    work_dir: Path = Path("data/sessions")
    storage: StorageKind = StorageKind.LOCAL
    s3_bucket: str | None = None
    s3_prefix: str = "projects/"
    s3_endpoint_url: str | None = None

    max_sessions: int = Field(default=4, ge=1)
    session_idle_seconds: float = 900
    session_max_seconds: float = 4 * 3600
    reaper_interval_seconds: float = 15
    start_timeout_seconds: float = 30
    stop_timeout_seconds: float = 10
    port_min: int = 20000
    port_max: int = 20999

    session_memory_mb: int = 2048
    session_max_file_mb: int = 1024
    session_max_open_files: int = 1024
    project_max_mb: int = 2048

    lua_timeout_ms: int | None = 30_000
    lua_memory_mb: int | None = 256
    max_render_seconds: int | None = 900
    bundled_plugins_only: bool = True

    @property
    def projects_dir(self) -> Path:
        return self.data_dir / "projects"

    @property
    def db_path(self) -> Path:
        return self.data_dir / "service.db"
