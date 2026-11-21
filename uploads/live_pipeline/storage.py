"""Crash-safe handoffs and a single publisher per run, independent of the GUI."""
import contextlib
import hashlib
import gzip
import json
import os
from pathlib import Path


def read(path):
    path = Path(path)
    raw = gzip.decompress(path.read_bytes()).decode('utf-8') if path.suffix == '.gz' else path.read_text(encoding='utf-8')
    return json.loads(raw)


def atomic(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    raw = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(',', ':')).encode('utf-8')
    if path.suffix == '.gz':
        raw = gzip.compress(raw, mtime=0)
    with temporary.open('wb') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                                     allow_nan=False).encode()).hexdigest()


class RunLockBusy(BlockingIOError):
    """Another process already owns this folder's publisher lock."""


@contextlib.contextmanager
def run_lock(folder):
    """OS-released lock: a stale filename after a crash never prevents recovery."""
    path = Path(folder) / 'publisher.lock'
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as stream:
        if stream.tell() == 0:
            stream.write(b'0')
            stream.flush()
        stream.seek(0)
        if os.name == 'nt':
            import msvcrt
            try:
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as error:
                # Normalise Windows' occupied byte-range lock to the same
                # exception used by the non-blocking POSIX lock below. File
                # permission failures from open() remain ordinary errors.
                if error.errno in (13, 11) or getattr(error, 'winerror', None) == 33:
                    raise RunLockBusy(f'Another process holds {path}') from error
                raise
        else:
            import fcntl
            try:
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise RunLockBusy(f'Another process holds {path}') from error
        try:
            yield
        finally:
            stream.seek(0)
            if os.name == 'nt':
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)
