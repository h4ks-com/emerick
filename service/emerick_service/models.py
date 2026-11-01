from datetime import datetime
from enum import StrEnum
from typing import Literal

from pydantic import BaseModel, Field


class SessionStatus(StrEnum):
    STARTING = "starting"
    RUNNING = "running"
    CLOSED = "closed"
    FAILED = "failed"


LIVE_STATUSES = (SessionStatus.STARTING, SessionStatus.RUNNING)


class ProjectCreate(BaseModel):
    name: str = Field(min_length=1, max_length=100)


class Project(BaseModel):
    id: str
    name: str
    created_at: datetime


class SessionCreate(BaseModel):
    project_id: str


class Session(BaseModel):
    id: str
    project_id: str
    status: SessionStatus
    reason: str | None
    port: int | None
    created_at: datetime
    last_activity: datetime
    closed_at: datetime | None


class SessionView(Session):
    mcp_url: str
    project_dir: str | None


class ProjectFile(BaseModel):
    path: str
    size: int
    modified_at: datetime


class Health(BaseModel):
    status: Literal["ok"] = "ok"
    sessions_live: int
    max_sessions: int
