import os
import shutil
from collections.abc import Iterator
from datetime import UTC, datetime
from pathlib import Path, PurePosixPath
from typing import IO, Protocol

from botocore.exceptions import ClientError
from mypy_boto3_s3 import S3Client

from emerick_service.models import ProjectFile

CHUNK_BYTES = 1 << 16


class PathEscapeError(ValueError):
    pass


def relative_path(path: str) -> PurePosixPath:
    """Turn a path from a request into a relative path that stays inside its root.

    :raises PathEscapeError: when the path is empty, absolute or climbs out with ``..``.
    """
    relative = PurePosixPath(path)
    if not path or "\\" in path or relative.is_absolute() or ".." in relative.parts or "\0" in path:
        raise PathEscapeError(f"{path!r} is not a path inside the project")
    return relative


def contained(root: Path, path: str) -> Path:
    """Resolve a request path under root, following symlinks.

    :raises PathEscapeError: when the resolved path leaves root.
    """
    resolved = (root / relative_path(path)).resolve()
    if not resolved.is_relative_to(root.resolve()):
        raise PathEscapeError(f"{path!r} is not a path inside the project")
    return resolved


def walk(root: Path) -> Iterator[tuple[str, os.stat_result]]:
    for folder, _, names in root.walk():
        for name in names:
            path = folder / name
            if path.is_file() and not path.is_symlink():
                yield path.relative_to(root).as_posix(), path.stat()


def usage_bytes(root: Path) -> int:
    return sum(stat.st_size for _, stat in walk(root))


def write_contained(root: Path, path: str, data: bytes) -> None:
    """Write data to a request path under root, creating its folders.

    :raises PathEscapeError: when path leaves root.
    """
    target = contained(root, path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def file_chunks(file: IO[bytes]) -> Iterator[bytes]:
    while chunk := file.read(CHUNK_BYTES):
        yield chunk


def read_chunks(path: Path, span: tuple[int, int] | None = None) -> Iterator[bytes]:
    with path.open("rb") as file:
        if span is None:
            yield from file_chunks(file)
            return
        first, last = span
        file.seek(first)
        remaining = last - first + 1
        while remaining > 0 and (chunk := file.read(min(CHUNK_BYTES, remaining))):
            remaining -= len(chunk)
            yield chunk


class Storage(Protocol):
    def create(self, project_id: str) -> None: ...

    def delete(self, project_id: str) -> None: ...

    def checkout(self, project_id: str, work_dir: Path) -> Path:
        """Return the folder a session works in, filled with the project's files."""
        ...

    def commit(self, project_id: str, project_dir: Path) -> None:
        """Store the changes a session made in project_dir."""
        ...

    def files(self, project_id: str) -> list[ProjectFile]: ...

    def read(self, project_id: str, path: str, span: tuple[int, int] | None = None) -> Iterator[bytes]:
        """Stream one file of the project, or its bytes first to last of span.

        :raises PathEscapeError: when path leaves the project.
        :raises FileNotFoundError: when the project holds no such file.
        """
        ...

    def size(self, project_id: str, path: str) -> int:
        """Return the bytes of one file of the project.

        :raises PathEscapeError: when path leaves the project.
        :raises FileNotFoundError: when the project holds no such file.
        """
        ...

    def write(self, project_id: str, path: str, data: bytes) -> None:
        """Store one file in the project, replacing any file at path.

        :raises PathEscapeError: when path leaves the project.
        """
        ...

    def usage(self, project_id: str) -> int:
        """Return the bytes the project's files take."""
        ...


class LocalStorage:
    def __init__(self, root: Path) -> None:
        self.root = root

    def _dir(self, project_id: str) -> Path:
        return self.root / project_id

    def create(self, project_id: str) -> None:
        self._dir(project_id).mkdir(parents=True)

    def delete(self, project_id: str) -> None:
        shutil.rmtree(self._dir(project_id), ignore_errors=True)

    def checkout(self, project_id: str, work_dir: Path) -> Path:
        project_dir = self._dir(project_id)
        project_dir.mkdir(parents=True, exist_ok=True)
        return project_dir

    def commit(self, project_id: str, project_dir: Path) -> None:
        pass

    def files(self, project_id: str) -> list[ProjectFile]:
        return sorted(
            (
                ProjectFile(path=path, size=stat.st_size, modified_at=datetime.fromtimestamp(stat.st_mtime, UTC))
                for path, stat in walk(self._dir(project_id))
            ),
            key=lambda file: file.path,
        )

    def _file(self, project_id: str, path: str) -> Path:
        file = contained(self._dir(project_id), path)
        if not file.is_file():
            raise FileNotFoundError(path)
        return file

    def read(self, project_id: str, path: str, span: tuple[int, int] | None = None) -> Iterator[bytes]:
        return read_chunks(self._file(project_id, path), span)

    def size(self, project_id: str, path: str) -> int:
        return self._file(project_id, path).stat().st_size

    def write(self, project_id: str, path: str, data: bytes) -> None:
        write_contained(self._dir(project_id), path, data)

    def usage(self, project_id: str) -> int:
        return usage_bytes(self._dir(project_id))


class S3Storage:
    def __init__(self, client: S3Client, bucket: str, prefix: str) -> None:
        self.client = client
        self.bucket = bucket
        self.prefix = prefix

    def _prefix(self, project_id: str) -> str:
        return f"{self.prefix}{project_id}/"

    def _objects(self, project_id: str) -> dict[str, tuple[int, datetime]]:
        prefix = self._prefix(project_id)
        objects: dict[str, tuple[int, datetime]] = {}
        for page in self.client.get_paginator("list_objects_v2").paginate(Bucket=self.bucket, Prefix=prefix):
            for item in page.get("Contents", []):
                match item:
                    case {"Key": key, "Size": size, "LastModified": modified}:
                        objects[key.removeprefix(prefix)] = (size, modified)
                    case _:
                        pass
        return objects

    def create(self, project_id: str) -> None:
        pass

    def delete(self, project_id: str) -> None:
        prefix = self._prefix(project_id)
        for path in self._objects(project_id):
            self.client.delete_object(Bucket=self.bucket, Key=prefix + path)

    def checkout(self, project_id: str, work_dir: Path) -> Path:
        project_dir = work_dir / "project"
        prefix = self._prefix(project_id)
        for path, (_, modified) in self._objects(project_id).items():
            target = contained(project_dir, path)
            target.parent.mkdir(parents=True, exist_ok=True)
            self.client.download_file(self.bucket, prefix + path, str(target))
            os.utime(target, (modified.timestamp(), modified.timestamp()))
        project_dir.mkdir(parents=True, exist_ok=True)
        return project_dir

    def commit(self, project_id: str, project_dir: Path) -> None:
        prefix = self._prefix(project_id)
        remote = self._objects(project_id)
        local = dict(walk(project_dir))
        for path, stat in local.items():
            known = remote.get(path)
            if known is None or known[0] != stat.st_size or known[1].timestamp() < stat.st_mtime:
                self.client.upload_file(str(project_dir / path), self.bucket, prefix + path)
        for path in remote.keys() - local.keys():
            self.client.delete_object(Bucket=self.bucket, Key=prefix + path)

    def files(self, project_id: str) -> list[ProjectFile]:
        return sorted(
            (
                ProjectFile(path=path, size=size, modified_at=modified)
                for path, (size, modified) in self._objects(project_id).items()
            ),
            key=lambda file: file.path,
        )

    def read(self, project_id: str, path: str, span: tuple[int, int] | None = None) -> Iterator[bytes]:
        key = self._prefix(project_id) + relative_path(path).as_posix()
        try:
            if span:
                body = self.client.get_object(Bucket=self.bucket, Key=key, Range=f"bytes={span[0]}-{span[1]}")["Body"]
            else:
                body = self.client.get_object(Bucket=self.bucket, Key=key)["Body"]
        except self.client.exceptions.NoSuchKey as error:
            raise FileNotFoundError(path) from error
        return body.iter_chunks(CHUNK_BYTES)

    def size(self, project_id: str, path: str) -> int:
        key = self._prefix(project_id) + relative_path(path).as_posix()
        try:
            return self.client.head_object(Bucket=self.bucket, Key=key)["ContentLength"]
        except ClientError as error:
            if error.response.get("Error", {}).get("Code") in {"404", "NoSuchKey"}:
                raise FileNotFoundError(path) from error
            raise

    def write(self, project_id: str, path: str, data: bytes) -> None:
        key = self._prefix(project_id) + relative_path(path).as_posix()
        self.client.put_object(Bucket=self.bucket, Key=key, Body=data)

    def usage(self, project_id: str) -> int:
        return sum(size for size, _ in self._objects(project_id).values())
