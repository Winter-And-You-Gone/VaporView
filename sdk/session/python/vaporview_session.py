"""Read and export VaporView Sessions via the C ABI using the Python standard library."""

from __future__ import annotations

import ctypes as ct
import json
import operator
import os
import sys
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Callable, Generic, Optional, TypeVar
import weakref


UINT64_MAX = (1 << 64) - 1
T = TypeVar("T")


class Status(IntEnum):
    OK = 0
    MISSING = 1
    INVALID_ARGUMENT = 2
    CORRUPT = 3
    IO_ERROR = 4
    CANCELLED = 5
    INTERNAL_ERROR = 6


class SessionError(RuntimeError):
    def __init__(self, status: int, message: str, records: int = 0):
        self.status = status
        self.message = message
        self.records = records
        name = Status(status).name if status in Status._value2member_map_ else str(status)
        super().__init__(f"{name}: {message}" if message else name)


@dataclass(frozen=True)
class Result(Generic[T]):
    value: T
    warning: str = ""


@dataclass(frozen=True)
class VisitResult:
    records: int
    cancelled: bool = False
    message: str = ""


@dataclass(frozen=True)
class Record:
    ordinal: int
    timestamp_us: int
    sequence: Optional[int]
    fields: dict


@dataclass(frozen=True)
class Waveform:
    record: Record
    h1: tuple
    h2: tuple
    raw: tuple
    harmonic: tuple


class _Record(ct.Structure):
    _fields_ = [
        ("ordinal", ct.c_uint64), ("timestamp_us", ct.c_uint64),
        ("sequence", ct.c_uint64), ("has_sequence", ct.c_uint32),
        ("fields_json", ct.c_char_p),
    ]


class _WaveformView(ct.Structure):
    _fields_ = [
        ("record", _Record),
        ("h1", ct.POINTER(ct.c_double)), ("h1_count", ct.c_uint64),
        ("h2", ct.POINTER(ct.c_double)), ("h2_count", ct.c_uint64),
        ("raw", ct.POINTER(ct.c_float)), ("raw_count", ct.c_uint64),
        ("harmonic", ct.POINTER(ct.c_float)), ("harmonic_count", ct.c_uint64),
    ]


_Visitor = ct.CFUNCTYPE(ct.c_int32, ct.POINTER(_Record), ct.c_void_p)


def _load_library(library):
    if library is None:
        names = {
            "win32": ("bin", "vaporview_session_sdk.dll"),
            "darwin": ("lib", "libvaporview_session_sdk.dylib"),
        }
        subdir, name = names.get(sys.platform, ("lib", "libvaporview_session_sdk.so"))
        library = Path(__file__).resolve().parent.parent / subdir / name
    library = Path(library).resolve()
    if sys.platform == "win32":
        with os.add_dll_directory(str(library.parent)):
            api = ct.CDLL(str(library))
    else:
        api = ct.CDLL(str(library))

    api.vv_session_abi_version.argtypes = []
    api.vv_session_abi_version.restype = ct.c_uint32
    abi = api.vv_session_abi_version()
    if abi != 1:
        raise RuntimeError(f"Expected VaporView Session SDK ABI 1, found {abi}")

    handle = ct.c_void_p
    out = ct.POINTER(handle)
    text = ct.c_char_p
    uint64 = ct.c_uint64
    count = ct.POINTER(uint64)
    status = ct.c_int32
    signatures = {
        "version": (text, []),
        "free_string": (None, [handle]),
        "open": (status, [text, out, out]),
        "close": (None, [handle]),
        "metadata": (status, [handle, out, out]),
        "categories": (status, [handle, out, out]),
        "visit": (status, [handle, text, uint64, uint64, _Visitor, handle, count, out]),
        "read_waveform": (status, [handle, uint64, out, out]),
        "waveform_data": (status, [handle, ct.POINTER(_WaveformView)]),
        "free_waveform": (None, [handle]),
        "export": (status, [handle, text, uint64, uint64, text, count, out]),
    }
    for name, (restype, argtypes) in signatures.items():
        function = getattr(api, f"vv_session_{name}")
        function.restype, function.argtypes = restype, argtypes
    return api


def _utf8(value):
    value = os.fspath(value)
    if not isinstance(value, str) or "\0" in value:
        raise ValueError("Expected text without NUL characters")
    return value.encode("utf-8")


def _uint64(value):
    number = operator.index(value)
    if not 0 <= number <= UINT64_MAX:
        raise ValueError("Expected an unsigned 64-bit integer")
    return number


def _record(record):
    return Record(record.ordinal, record.timestamp_us,
                  record.sequence if record.has_sequence else None, json.loads(record.fields_json))


def _samples(pointer, count):
    return tuple(pointer[:count]) if count else ()


class Session:
    """An opened Session. Serialize calls on each instance, including close.

    Use a context manager or call close explicitly. Pass library= to override the
    library next to the installed module. Use Python 3.8 or newer.
    """

    def __init__(self, directory, *, library=None):
        self._api = _load_library(library)
        self._in_visitor = False
        self._handle = ct.c_void_p()
        message = ct.c_void_p()
        status = self._api.vv_session_open(_utf8(directory), ct.byref(self._handle), ct.byref(message))
        self._finalize = weakref.finalize(self, self._api.vv_session_close, self._handle) if self._handle else None
        try:
            self.open_warning = self._check(status, message)
        except BaseException:
            self.close()
            raise

    @property
    def version(self):
        return self._api.vv_session_version().decode("utf-8")

    def _read_string(self, pointer):
        try:
            return ct.string_at(pointer).decode("utf-8") if pointer else ""
        finally:
            self._api.vv_session_free_string(pointer)

    def _check(self, status, message, records=0):
        text = self._read_string(message)
        if status != Status.OK:
            raise SessionError(status, text, records)
        return text

    def _opened(self):
        if self._in_visitor:
            raise RuntimeError("The visitor cannot reenter or close its Session")
        if not self._handle:
            raise RuntimeError("Session is closed")
        return self._handle

    def close(self):
        if self._in_visitor:
            raise RuntimeError("The visitor cannot reenter or close its Session")
        if self._finalize is not None:
            self._finalize()
        self._handle = ct.c_void_p()

    def __enter__(self):
        self._opened()
        return self

    def __exit__(self, exc_type, exc, traceback):
        self.close()

    def _json(self, function):
        output, message = ct.c_void_p(), ct.c_void_p()
        status = function(self._opened(), ct.byref(output), ct.byref(message))
        try:
            text = self._read_string(output)
        finally:
            warning = self._check(status, message)
        return Result(json.loads(text), warning)

    def metadata(self) -> Result[dict]:
        return self._json(self._api.vv_session_metadata)

    def categories(self) -> Result[list]:
        return self._json(self._api.vv_session_categories)

    def visit(self, category: str, visitor: Callable[[Record], bool],
              first_us: int = 0, last_us: int = UINT64_MAX) -> VisitResult:
        """Visit inclusive Unix microsecond bounds. A false return cancels.

        Each Record is copied and can outlive the callback. records counts visits
        that continued. A visitor exception is raised again in the calling Python code.
        """
        if not callable(visitor):
            raise TypeError("visitor must be callable")
        callback_error = None

        @_Visitor
        def callback(record, _):
            nonlocal callback_error
            self._in_visitor = True
            try:
                return int(bool(visitor(_record(record.contents))))
            except BaseException as error:
                callback_error = error
                return 0
            finally:
                self._in_visitor = False

        records, message = ct.c_uint64(), ct.c_void_p()
        status = self._api.vv_session_visit(self._opened(), _utf8(category), _uint64(first_us), _uint64(last_us),
                                            callback, None, ct.byref(records), ct.byref(message))
        text = self._read_string(message)
        if callback_error is not None:
            raise callback_error
        if status not in (Status.OK, Status.CANCELLED):
            raise SessionError(status, text, records.value)
        return VisitResult(records.value, status == Status.CANCELLED, text)

    def read_waveform(self, ordinal: int) -> Result[Waveform]:
        """Copy waveform samples into Python tuples owned by the returned Waveform."""
        frame, message = ct.c_void_p(), ct.c_void_p()
        status = self._api.vv_session_read_waveform(self._opened(), _uint64(ordinal),
                                                     ct.byref(frame), ct.byref(message))
        try:
            warning = self._check(status, message)
            view = _WaveformView()
            status = self._api.vv_session_waveform_data(frame, ct.byref(view))
            if status != Status.OK:
                raise SessionError(status, "Reading the waveform view failed")
            return Result(Waveform(_record(view.record), _samples(view.h1, view.h1_count),
                                   _samples(view.h2, view.h2_count), _samples(view.raw, view.raw_count),
                                   _samples(view.harmonic, view.harmonic_count)), warning)
        finally:
            self._api.vv_session_free_waveform(frame)

    def export(self, category: str, directory, first_us: int = 0, last_us: int = UINT64_MAX) -> Result[int]:
        """Export a category and return the count and any successful-operation warning."""
        records, message = ct.c_uint64(), ct.c_void_p()
        status = self._api.vv_session_export(self._opened(), _utf8(category), _uint64(first_us), _uint64(last_us),
                                              _utf8(directory), ct.byref(records), ct.byref(message))
        return Result(records.value, self._check(status, message, records.value))
