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
        # The live worker writes its local evidence before it can compare the
        # forecast. Medium compression is lossless and spends less time there.
        raw = gzip.compress(raw, compresslevel=6, mtime=0)
    with temporary.open('wb') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
    if os.name == 'posix' and (path.name in ('publication.json', 'ready-publication.json') or path.parent.name == 'receipts'):
        # Persist the new directory entry as well as the file contents before
        # treating a release or a server acknowledgement as complete on disk.
        directory = os.open(str(path.parent), os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                                     allow_nan=False).encode()).hexdigest()


class RunLockBusy(BlockingIOError):
    """Another process already owns this worker's lock in the run folder."""


@contextlib.contextmanager
def _windows_exclusive_open(path):
    # Windows and WSL can both grant their own locks on a byte in the same
    # file. An exclusive *open* is checked by Windows even when the other
    # process opens the file through /mnt/c, whichever process starts first.
    import ctypes
    from ctypes import wintypes

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                       wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    create.restype = wintypes.HANDLE
    close = kernel.CloseHandle
    close.argtypes = (wintypes.HANDLE,)
    close.restype = wintypes.BOOL

    read_write, no_sharing, open_always, normal_file = 0xC0000000, 0, 4, 0x80
    handle = create(str(path), read_write, no_sharing, None, open_always, normal_file, None)
    if handle == ctypes.c_void_p(-1).value:
        code = ctypes.get_last_error()
        if code in (32, 33):  # Sharing or lock violation.
            raise RunLockBusy(f'Another process holds {path}')
        raise ctypes.WinError(code)
    try:
        yield
    finally:
        close(handle)


@contextlib.contextmanager
def run_lock(folder, filename='publisher.lock'):
    """Hold a named worker's OS lock. Exiting releases it even after a crash."""
    path = Path(folder) / filename
    path.parent.mkdir(parents=True, exist_ok=True)
    if os.name == 'nt':
        with _windows_exclusive_open(path):
            yield
        return

    import fcntl
    try:
        stream = path.open('a+b')
    except PermissionError as error:
        if 'microsoft' in os.uname().release.lower():
            # WSL reports the same error for a Windows exclusive open and an
            # actual permission failure. Either prevents safe publication.
            raise RunLockBusy(f'Cannot open {path}. Check for another Windows worker or missing write permission.') from error
        raise
    with stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RunLockBusy(f'Another process holds {path}') from error
        yield
