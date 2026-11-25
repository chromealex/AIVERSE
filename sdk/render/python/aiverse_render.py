"""Render a loaded AIVERSE model into top-down RGBA8 bytes using the native SDK."""
import ctypes as C
import os
import threading
import weakref


class Renderer:
    def __init__(self, library):
        self.native = native = C.CDLL(os.fspath(library))
        native.aiverse_render_abi_version.argtypes = []
        native.aiverse_render_abi_version.restype = C.c_uint32
        if native.aiverse_render_abi_version() != 1:
            raise RuntimeError("Unsupported AIVERSE renderer ABI")
        native.aiverse_render_open.argtypes = [C.c_char_p]
        native.aiverse_render_open.restype = C.c_void_p
        native.aiverse_render_close.argtypes = [C.c_void_p]
        native.aiverse_render_close.restype = None
        native.aiverse_render_last_error.argtypes = []
        native.aiverse_render_last_error.restype = C.c_char_p
        native.aiverse_render_rgba8.argtypes = [C.c_void_p, C.c_char_p, C.c_int, C.c_int,
                                               C.c_float, C.c_float, C.POINTER(C.c_uint8), C.c_size_t]
        native.aiverse_render_rgba8.restype = C.c_int

    def open(self, path):
        path = os.fsdecode(path).encode("utf-8")
        if b"\0" in path:
            raise ValueError("Invalid model path")
        handle = self.native.aiverse_render_open(path)
        if not handle:
            raise RuntimeError(self.native.aiverse_render_last_error().decode("utf-8", "replace"))
        return Model(self.native, handle)


class Model:
    def __init__(self, native, handle):
        self._native, self._handle = native, handle
        self._lock = threading.Lock()
        self._release = weakref.finalize(self, native.aiverse_render_close, handle)

    def close(self):
        with self._lock:
            self._release()
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def render(self, width=512, height=512, yaw=.55, pitch=.3, backend="cpu"):
        if not (1 <= width <= 4096 and 1 <= height <= 4096):
            raise ValueError("Frame dimensions must be 1..4096")
        if "\0" in backend:
            raise ValueError("Invalid renderer backend")
        with self._lock:
            if not self._handle:
                raise RuntimeError("Model is closed")
            pixels = (C.c_uint8 * (width * height * 4))()
            if self._native.aiverse_render_rgba8(self._handle, backend.encode("ascii"), width, height,
                                                yaw, pitch, pixels, len(pixels)):
                raise RuntimeError(self._native.aiverse_render_last_error().decode("utf-8", "replace"))
            return bytes(pixels)
