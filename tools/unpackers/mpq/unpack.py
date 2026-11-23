#!/usr/bin/env python3
"""MPQ reader for AIVERSE's unpacker protocol 1 (Warcraft III maps and classic MPQs).

Requires the MIT-licensed StormLib shared library. Set AIVERSE_STORMLIB to the
absolute path, or install libstorm in a standard loader path. No game assets are
bundled. File discovery uses (listfile), known WC3 map filenames and war3map.imp;
unnamed files in protected MPQs cannot be enumerated without their names.
"""
import ctypes
import ctypes.util
import os
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'lib'))
from aiverseunpack import Entry, run  # noqa: E402

MAX_FILE = 512 * 1024 * 1024
MAX_ENTRIES = 100000
MAX_LISTFILE = 16 * 1024 * 1024
KNOWN = (
    '(listfile)', 'war3map.w3i', 'war3map.w3e', 'war3map.wts', 'war3map.wtg',
    'war3map.wct', 'war3map.j', 'Scripts\\war3map.j', 'war3map.lua',
    'war3map.wpm', 'war3map.shd', 'war3map.doo', 'war3mapUnits.doo',
    'war3map.w3u', 'war3map.w3t', 'war3map.w3b', 'war3map.w3d',
    'war3map.w3a', 'war3map.w3q', 'war3map.w3h', 'war3map.w3r',
    'war3map.w3c', 'war3map.w3s', 'war3map.imp', 'war3map.mmp',
    'war3mapMap.blp', 'war3mapMap.tga', 'war3mapPreview.tga',
    'war3mapPath.tga', 'war3map.w3l', 'war3map.w3grp', 'war3mapMisc.txt', 'war3mapSkin.txt',
    'war3mapExtra.txt', 'war3mapSkin.w3u', 'war3mapSkin.w3a',
    'war3mapSkin.w3b', 'war3mapSkin.w3d', 'war3mapSkin.w3h',
    'war3mapSkin.w3q', 'war3mapSkin.w3t', '(attributes)', '(signature)',
)


def valid_name(name):
    if not name or len(name) > 1024 or name[0] in ('/', '\\') or '\x00' in name:
        return False
    parts = name.replace('\\', '/').split('/')
    return all(p not in ('', '.', '..') and ':' not in p for p in parts)


def probe(path):
    if not os.path.isfile(path):
        return False
    with open(path, 'rb') as stream:
        data = stream.read(4096)
    # MPQ header can follow an HM3W user-data header or 512-byte map preamble.
    return any(data[i:i + 4] in (b'MPQ\x1a', b'MPQ\x1b')
               for i in range(0, min(len(data), 4096) - 3, 512))


def _library():
    names = [os.environ.get('AIVERSE_STORMLIB'), ctypes.util.find_library('storm'),
             'libstorm.dylib', 'libstorm.so', 'libstorm.so.0', 'StormLib.dll',
             '/opt/homebrew/lib/libstorm.dylib', '/usr/local/lib/libstorm.dylib']
    errs = []
    for name in filter(None, names):
        try:
            dll = (ctypes.WinDLL(name) if os.name == 'nt' else ctypes.CDLL(name))
            ptr = ctypes.c_void_p
            u32 = ctypes.c_uint32
            dll.SFileOpenArchive.argtypes = [ctypes.c_char_p, u32, u32, ctypes.POINTER(ptr)]
            dll.SFileOpenArchive.restype = ctypes.c_int
            dll.SFileCloseArchive.argtypes = [ptr]
            dll.SFileCloseArchive.restype = ctypes.c_int
            dll.SFileOpenFileEx.argtypes = [ptr, ctypes.c_char_p, u32, ctypes.POINTER(ptr)]
            dll.SFileOpenFileEx.restype = ctypes.c_int
            dll.SFileGetFileSize.argtypes = [ptr, ctypes.POINTER(u32)]
            dll.SFileGetFileSize.restype = u32
            dll.SFileReadFile.argtypes = [ptr, ptr, u32, ctypes.POINTER(u32), ptr]
            dll.SFileReadFile.restype = ctypes.c_int
            dll.SFileCloseFile.argtypes = [ptr]
            dll.SFileCloseFile.restype = ctypes.c_int
            return dll
        except (OSError, AttributeError) as ex:
            errs.append(str(ex))
    raise RuntimeError('StormLib shared library not found. Install StormLib (MIT) or set '
                       'AIVERSE_STORMLIB=/absolute/path/to/libstorm.dylib '
                       '(Linux: libstorm.so; Windows: StormLib.dll). ' + '; '.join(errs[:2]))


class Archive:
    def __init__(self, path):
        self.dll = _library()
        self.handle = ctypes.c_void_p()
        # STREAM_FLAG_READ_ONLY (StormLib 0x100), never mutate imported assets.
        if not self.dll.SFileOpenArchive(os.fsencode(path), 0, 0x100, ctypes.byref(self.handle)):
            raise ValueError('StormLib could not open MPQ: ' + path)

    def close(self):
        if self.handle and self.handle.value:
            self.dll.SFileCloseArchive(self.handle)
            self.handle = ctypes.c_void_p()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def _open(self, name):
        handle = ctypes.c_void_p()
        if not self.dll.SFileOpenFileEx(self.handle, name.encode('utf-8'), 0,
                                        ctypes.byref(handle)):
            return None
        return handle

    def size(self, name):
        handle = self._open(name)
        if handle is None:
            return None
        try:
            high = ctypes.c_uint32()
            low = self.dll.SFileGetFileSize(handle, ctypes.byref(high))
            size = (high.value << 32) | low
            if size > MAX_FILE:
                raise ValueError('MPQ entry too large: ' + name)
            return size
        finally:
            self.dll.SFileCloseFile(handle)

    def read(self, name, max_size=MAX_FILE):
        handle = self._open(name)
        if handle is None:
            raise FileNotFoundError('MPQ entry missing: ' + name)
        try:
            high = ctypes.c_uint32()
            low = self.dll.SFileGetFileSize(handle, ctypes.byref(high))
            size = (high.value << 32) | low
            if size > max_size:
                raise ValueError('MPQ entry exceeds safety limit: ' + name)
            chunks = []
            remaining = size
            while remaining:
                count = min(remaining, 1 << 20)
                buffer = ctypes.create_string_buffer(count)
                got = ctypes.c_uint32()
                if not self.dll.SFileReadFile(handle, buffer, count, ctypes.byref(got), None):
                    raise ValueError('MPQ decompression failed: ' + name)
                if got.value != count:
                    raise ValueError('MPQ entry truncated: ' + name)
                chunks.append(buffer.raw)
                remaining -= count
            return b''.join(chunks)
        finally:
            self.dll.SFileCloseFile(handle)

    def names(self):
        names = {}
        def add(name):
            name = name.strip().replace('/', '\\')
            if valid_name(name):
                names.setdefault(name.casefold(), name)
                if len(names) > MAX_ENTRIES:
                    raise ValueError('MPQ file count exceeds safety limit')
        for name in KNOWN:
            add(name)
        try:
            for name in self.read('(listfile)', MAX_LISTFILE).decode('utf-8-sig', 'replace').splitlines():
                add(name)
        except FileNotFoundError:
            pass
        try:
            imp = self.read('war3map.imp', MAX_LISTFILE)
            if len(imp) < 8:
                raise ValueError('truncated war3map.imp')
            version, count = struct.unpack_from('<II', imp)
            if version not in (0, 1) or count > MAX_ENTRIES:
                raise ValueError('invalid war3map.imp header')
            offset = 8
            for _ in range(count):
                if offset >= len(imp):
                    raise ValueError('truncated war3map.imp entry')
                flag = imp[offset]
                offset += 1
                end = imp.find(b'\0', offset)
                if end < 0 or end - offset > 1024:
                    raise ValueError('invalid war3map.imp filename')
                name = imp[offset:end].decode('utf-8', 'replace')
                add(name)
                if flag in (0, 5):
                    add('war3mapImported\\' + name)
                offset = end + 1
        except FileNotFoundError:
            pass
        # Protected maps sometimes omit (listfile); these are named known-path lookups only.
        return sorted((name for name in names.values() if self.size(name) is not None),
                      key=str.casefold)


def entries(path):
    if not probe(path):
        raise ValueError('not an MPQ archive')
    with Archive(path) as archive:
        for name in archive.names():
            size = archive.size(name)
            yield Entry(name, size, lambda name=name: archive.read(name))


if __name__ == '__main__':
    run('mpq', '1.0.0', probe, entries)
