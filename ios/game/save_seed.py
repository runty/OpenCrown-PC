"""Copy a quiescent disposable save seed into a new, empty attempt directory."""

import hashlib
import os
from pathlib import Path
import stat


def inventory(root, max_entries=4096, max_bytes=256 * 1024 * 1024):
    root = Path(root)
    if not stat.S_ISDIR(root.lstat().st_mode):
        raise ValueError("Save seed must be a directory, without symlinks")
    entries = []
    total = 0
    for path in sorted(root.rglob("*")):
        info = path.lstat()
        relative = path.relative_to(root)
        if not stat.S_ISDIR(info.st_mode) and not stat.S_ISREG(info.st_mode):
            raise ValueError("Save seed contains a symlink or special file")
        entries.append((relative, stat.S_ISDIR(info.st_mode), info.st_size))
        if len(entries) > max_entries:
            raise ValueError("Save seed has too many entries")
        if stat.S_ISREG(info.st_mode):
            total += info.st_size
            if total > max_bytes:
                raise ValueError("Save seed exceeds its byte limit")
    return entries


def copy_seed(source, destination, max_entries=4096, max_bytes=256 * 1024 * 1024):
    """Caller keeps source quiescent. Failure retains the unused partial attempt.

    The destination must be a new empty directory. Never replaces existing save
    files or writes into the source. Returns relative file sizes and SHA-256.
    """
    source, destination = Path(source), Path(destination)
    entries = inventory(source, max_entries, max_bytes)
    if not stat.S_ISDIR(destination.lstat().st_mode) or any(destination.iterdir()):
        raise ValueError("Save seed destination must be an empty real directory")
    source_real, destination_real = source.resolve(), destination.resolve()
    if (
        source_real == destination_real
        or source_real in destination_real.parents
        or destination_real in source_real.parents
    ):
        raise ValueError("Save seed and attempt directories must be separate")
    manifest = {}
    for relative, directory, size in entries:
        origin, target = source / relative, destination / relative
        if directory:
            target.mkdir()
            continue
        with os.fdopen(
            os.open(origin, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK), "rb"
        ) as incoming:
            before = os.fstat(incoming.fileno())
            if not stat.S_ISREG(before.st_mode) or before.st_size != size:
                raise ValueError("Save seed changed before copying")
            digest = hashlib.sha256()
            copied = 0
            with os.fdopen(
                os.open(
                    target, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600
                ),
                "wb",
            ) as outgoing:
                while chunk := incoming.read(min(1024 * 1024, size - copied + 1)):
                    copied += len(chunk)
                    if copied > size:
                        raise ValueError("Save seed grew while copying")
                    digest.update(chunk)
                    outgoing.write(chunk)
                outgoing.flush()
                os.fsync(outgoing.fileno())
            after = os.fstat(incoming.fileno())
            if copied != size or (before.st_size, before.st_mtime_ns) != (
                after.st_size,
                after.st_mtime_ns,
            ):
                raise ValueError("Save seed changed while copying")
        actual = hashlib.sha256(target.read_bytes()).hexdigest()
        if actual != digest.hexdigest():
            raise ValueError("Save seed copy hash mismatch")
        manifest[relative.as_posix()] = {"size": size, "sha256": actual}
    # Recheck the tree structure, including empty directories and late symlinks.
    if inventory(source, max_entries, max_bytes) != entries:
        raise ValueError("Save seed tree changed while copying")
    return manifest
