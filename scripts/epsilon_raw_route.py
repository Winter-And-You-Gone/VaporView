#!/usr/bin/env python3
"""Parse VaporView EPSILON FDILink raw data, export route CSV, and draw an HTML map.

The parser follows VaporView's existing raw DAT and EPSILON FDILink decoding
rules from RawDataParserWindow.cpp / data_collector.cpp:

* unified raw DAT: session_*/raw/epsilon.dat, magic VVRAWDAT, source_id = 1
* legacy raw DAT: session_*/sensors/epsilon_raw.dat, magic VVEPSRAW
* raw FDILink stream: consecutive frames beginning with 0xFC and ending 0xFD

Only standard-library modules are used so the script can run in a fresh Python
environment.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import struct
import sys
import tempfile
import webbrowser
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from string import Template
from typing import Iterable, Iterator, Optional, Sequence


if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


FRAME_HEAD = 0xFC
FRAME_TAIL = 0xFD

UNIFIED_RAW_MAGIC = b"VVRAWDAT"
UNIFIED_RAW_HEADER_SIZE = 20
UNIFIED_RAW_RECORD_MARKER = 0x44525756
UNIFIED_RAW_RECORD_HEADER_SIZE = 36
RAW_SOURCE_EPSILON = 1

LEGACY_EPSILON_MAGIC = b"VVEPSRAW"
LEGACY_EPSILON_RECORD_MARKER = 0x524D5549
LEGACY_EPSILON_RECORD_HEADER_SIZE = 20

EARTH_RADIUS_METERS = 6_371_000.0
TILE_SIZE = 256
DEFAULT_TIANDITU_KEY = "5a0d3293c900281a37417b2e4d4d3676"

PACKET_NAMES = {
    0x40: "IMU",
    0x41: "AHRS",
    0x42: "INS_GPS",
    0x50: "SYS_STATE",
    0x51: "UNIX_TIME",
    0x52: "FORMATTED_TIME",
    0x59: "RAW_GNSS",
    0x5A: "SATELLITES",
    0x5C: "GEODETIC_POS",
    0x5D: "ECEF_POS",
    0xF0: "MAVLINK_TUNNEL",
}

FIX_NAMES = {
    0: "NO_GPS",
    1: "NO_FIX",
    2: "2D",
    3: "3D",
    4: "DGPS",
    5: "RTK_FLOAT",
    6: "RTK_FIXED",
    7: "STATIC",
    8: "PPP",
    9: "RTK_DUAL",
}

FIX_NAME_TO_CODE = {name: code for code, name in FIX_NAMES.items()}

CSV_FIELDS = [
    "point_index",
    "source_point_index",
    "source_file",
    "raw_format",
    "record_sequence",
    "host_timestamp_us",
    "host_time_utc",
    "packet_id_hex",
    "packet_name",
    "position_source",
    "serial_number",
    "utc_unix_s",
    "utc_microseconds",
    "epsilon_time_utc",
    "latitude_deg",
    "longitude_deg",
    "height_m",
    "segment_distance_m",
    "cumulative_distance_m",
    "gnss_fix_code",
    "gnss_fix",
    "gnss_satellites",
    "hdop",
    "vdop",
    "hacc_m",
    "vacc_m",
    "lat_std_m",
    "lon_std_m",
    "height_std_m",
    "diff_age_s",
    "vel_n_mps",
    "vel_e_mps",
    "vel_d_mps",
    "ned_n_m",
    "ned_e_m",
    "ned_d_m",
    "roll_deg",
    "pitch_deg",
    "yaw_deg",
    "system_status_bits",
    "filter_status_bits",
    "update_status_bits",
    "record_offset",
    "payload_offset",
]


class EpsilonRouteError(RuntimeError):
    """Raised for input or parsing errors that should be shown to the user."""


@dataclass
class RawRecord:
    source_file: Path
    raw_format: str
    sequence: int
    host_timestamp_us: int
    record_type: int
    flags: int
    payload: bytes
    record_offset: int = -1
    payload_offset: int = -1


@dataclass
class FrameInfo:
    ok: bool
    packet_id: int
    serial_number: int
    payload_size: int
    payload: bytes
    crc8_ok: bool
    crc16_ok: bool
    tail_ok: bool
    size_ok: bool
    error: str = ""


@dataclass
class DecodeState:
    utc_unix_s: Optional[int] = None
    utc_microseconds: Optional[int] = None
    gnss_fix_code: Optional[int] = None
    gnss_satellites: Optional[int] = None
    hdop: Optional[float] = None
    vdop: Optional[float] = None
    hacc_m: Optional[float] = None
    vacc_m: Optional[float] = None
    lat_std_m: Optional[float] = None
    lon_std_m: Optional[float] = None
    height_std_m: Optional[float] = None
    diff_age_s: Optional[float] = None
    vel_n_mps: Optional[float] = None
    vel_e_mps: Optional[float] = None
    vel_d_mps: Optional[float] = None
    ned_n_m: Optional[float] = None
    ned_e_m: Optional[float] = None
    ned_d_m: Optional[float] = None
    roll_deg: Optional[float] = None
    pitch_deg: Optional[float] = None
    yaw_deg: Optional[float] = None
    system_status_bits: Optional[int] = None
    filter_status_bits: Optional[int] = None
    update_status_bits: Optional[int] = None


@dataclass
class EpsilonPoint:
    point_index: int = 0
    source_point_index: int = 0
    source_file: str = ""
    raw_format: str = ""
    record_sequence: int = 0
    host_timestamp_us: Optional[int] = None
    host_time_utc: str = ""
    packet_id_hex: str = ""
    packet_name: str = ""
    position_source: str = ""
    serial_number: Optional[int] = None
    utc_unix_s: Optional[int] = None
    utc_microseconds: Optional[int] = None
    epsilon_time_utc: str = ""
    latitude_deg: Optional[float] = None
    longitude_deg: Optional[float] = None
    height_m: Optional[float] = None
    segment_distance_m: float = 0.0
    cumulative_distance_m: float = 0.0
    gnss_fix_code: Optional[int] = None
    gnss_fix: str = ""
    gnss_satellites: Optional[int] = None
    hdop: Optional[float] = None
    vdop: Optional[float] = None
    hacc_m: Optional[float] = None
    vacc_m: Optional[float] = None
    lat_std_m: Optional[float] = None
    lon_std_m: Optional[float] = None
    height_std_m: Optional[float] = None
    diff_age_s: Optional[float] = None
    vel_n_mps: Optional[float] = None
    vel_e_mps: Optional[float] = None
    vel_d_mps: Optional[float] = None
    ned_n_m: Optional[float] = None
    ned_e_m: Optional[float] = None
    ned_d_m: Optional[float] = None
    roll_deg: Optional[float] = None
    pitch_deg: Optional[float] = None
    yaw_deg: Optional[float] = None
    system_status_bits: Optional[int] = None
    filter_status_bits: Optional[int] = None
    update_status_bits: Optional[int] = None
    record_offset: int = -1
    payload_offset: int = -1


@dataclass
class ParseStats:
    input_files: int = 0
    raw_records: int = 0
    epsilon_records: int = 0
    valid_frames: int = 0
    bad_frames: int = 0
    coordinate_candidates: int = 0
    selected_source_candidates: int = 0
    filtered_points: int = 0
    exported_points: int = 0
    auto_position_source: str = ""


def packet_name(packet_id: int) -> str:
    name = PACKET_NAMES.get(packet_id, "UNKNOWN")
    return f"0x{packet_id:02X} {name}"


def read_u16_le(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def read_u32_le(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_i64_le(data: bytes, offset: int) -> int:
    return struct.unpack_from("<q", data, offset)[0]


def read_float_le(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def read_double_le(data: bytes, offset: int) -> float:
    return struct.unpack_from("<d", data, offset)[0]


def rad_to_deg(value: float) -> float:
    return value * 180.0 / math.pi


def fdilink_crc8(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 0x01:
                crc = (crc >> 1) ^ 0x8C
            else:
                crc >>= 1
            crc &= 0xFF
    return crc


def fdilink_crc16(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def parse_fdilink_frame(frame: bytes) -> FrameInfo:
    if len(frame) < 8:
        return FrameInfo(False, 0, 0, 0, b"", False, False, False, False, "frame shorter than 8 bytes")

    packet_id = frame[1]
    payload_size = frame[2]
    serial_number = frame[3]
    expected_size = payload_size + 8
    size_ok = len(frame) == expected_size
    tail_ok = size_ok and frame[-1] == FRAME_TAIL
    crc8_ok = fdilink_crc8(frame[:4]) == frame[4]
    received_crc16 = (frame[5] << 8) | frame[6]
    payload = frame[7 : 7 + payload_size] if len(frame) >= 7 + payload_size else b""
    crc16_ok = size_ok and fdilink_crc16(payload) == received_crc16
    head_ok = frame[0] == FRAME_HEAD
    ok = head_ok and size_ok and tail_ok and crc8_ok and crc16_ok
    errors: list[str] = []
    if not head_ok:
        errors.append("bad head")
    if not size_ok:
        errors.append(f"bad size: got {len(frame)}, expected {expected_size}")
    if not tail_ok:
        errors.append("bad tail")
    if not crc8_ok:
        errors.append("bad crc8")
    if not crc16_ok:
        errors.append("bad crc16")
    return FrameInfo(ok, packet_id, serial_number, payload_size, payload, crc8_ok, crc16_ok, tail_ok, size_ok, "; ".join(errors))


def iter_raw_fdilink_stream(path: Path) -> Iterator[RawRecord]:
    data = path.read_bytes()
    offset = 0
    sequence = 0
    while offset + 8 <= len(data):
        try:
            head_offset = data.index(bytes([FRAME_HEAD]), offset)
        except ValueError:
            break
        if head_offset + 8 > len(data):
            break
        payload_size = data[head_offset + 2]
        frame_size = payload_size + 8
        if frame_size <= 0 or frame_size > 4096:
            offset = head_offset + 1
            continue
        if head_offset + frame_size > len(data):
            break
        frame = data[head_offset : head_offset + frame_size]
        if frame[-1] != FRAME_TAIL:
            offset = head_offset + 1
            continue
        yield RawRecord(
            source_file=path,
            raw_format="fdilink_stream",
            sequence=sequence,
            host_timestamp_us=0,
            record_type=frame[1],
            flags=frame[3],
            payload=frame,
            record_offset=head_offset,
            payload_offset=head_offset,
        )
        sequence += 1
        offset = head_offset + frame_size


def iter_unified_raw_dat(path: Path) -> Iterator[RawRecord]:
    with path.open("rb") as file:
        header = file.read(UNIFIED_RAW_HEADER_SIZE)
        if len(header) != UNIFIED_RAW_HEADER_SIZE:
            raise EpsilonRouteError(f"{path} is too short for a unified raw DAT header")
        magic, version, header_size, source_id, _reserved = struct.unpack("<8sIIHH", header)
        if magic != UNIFIED_RAW_MAGIC:
            raise EpsilonRouteError(f"{path} does not start with {UNIFIED_RAW_MAGIC!r}")
        if version < 1:
            raise EpsilonRouteError(f"{path} has unsupported unified raw DAT version {version}")
        if header_size < UNIFIED_RAW_HEADER_SIZE:
            raise EpsilonRouteError(f"{path} has invalid raw DAT header_size {header_size}")
        if header_size > UNIFIED_RAW_HEADER_SIZE:
            file.seek(header_size)
        if source_id != RAW_SOURCE_EPSILON:
            raise EpsilonRouteError(f"{path} source_id is {source_id}, expected EPSILON source_id 1")

        while True:
            record_offset = file.tell()
            first = file.read(8)
            if not first:
                break
            if len(first) != 8:
                raise EpsilonRouteError(f"{path} ended inside a raw record header at offset {record_offset}")
            marker, record_header_size = struct.unpack("<II", first)
            if marker != UNIFIED_RAW_RECORD_MARKER:
                raise EpsilonRouteError(f"{path} has bad record marker 0x{marker:08X} at offset {record_offset}")
            if record_header_size < UNIFIED_RAW_RECORD_HEADER_SIZE:
                raise EpsilonRouteError(
                    f"{path} has invalid record header_size {record_header_size} at offset {record_offset}"
                )
            rest = file.read(record_header_size - 8)
            if len(rest) != record_header_size - 8:
                raise EpsilonRouteError(f"{path} ended inside a raw record header at offset {record_offset}")
            record_header = first + rest
            (
                _marker,
                _header_size,
                host_timestamp_us,
                payload_size,
                record_source_id,
                record_type,
                flags,
                sequence,
            ) = struct.unpack_from("<IIQIHHIQ", record_header, 0)
            payload_offset = file.tell()
            payload = file.read(payload_size)
            if len(payload) != payload_size:
                raise EpsilonRouteError(f"{path} ended inside payload at offset {payload_offset}")
            if record_source_id != RAW_SOURCE_EPSILON:
                continue
            yield RawRecord(
                source_file=path,
                raw_format=f"unified_v{version}",
                sequence=sequence,
                host_timestamp_us=host_timestamp_us,
                record_type=record_type,
                flags=flags,
                payload=payload,
                record_offset=record_offset,
                payload_offset=payload_offset,
            )


def iter_legacy_epsilon_raw_dat(path: Path) -> Iterator[RawRecord]:
    with path.open("rb") as file:
        header = file.read(16)
        if len(header) != 16:
            raise EpsilonRouteError(f"{path} is too short for a legacy EPSILON raw header")
        magic, version, header_size = struct.unpack("<8sII", header)
        if magic != LEGACY_EPSILON_MAGIC:
            raise EpsilonRouteError(f"{path} does not start with {LEGACY_EPSILON_MAGIC!r}")
        if version < 1:
            raise EpsilonRouteError(f"{path} has unsupported legacy EPSILON raw version {version}")
        if header_size < 16:
            raise EpsilonRouteError(f"{path} has invalid legacy header_size {header_size}")
        if header_size > 16:
            file.seek(header_size)

        sequence = 0
        while True:
            record_offset = file.tell()
            header_bytes = file.read(LEGACY_EPSILON_RECORD_HEADER_SIZE)
            if not header_bytes:
                break
            if len(header_bytes) != LEGACY_EPSILON_RECORD_HEADER_SIZE:
                raise EpsilonRouteError(f"{path} ended inside a legacy record header at offset {record_offset}")
            marker, payload_size, host_timestamp_us, frame_tag, reserved = struct.unpack("<IIQB3s", header_bytes)
            if marker != LEGACY_EPSILON_RECORD_MARKER:
                raise EpsilonRouteError(f"{path} has bad legacy record marker 0x{marker:08X} at offset {record_offset}")
            payload_offset = file.tell()
            payload = file.read(payload_size)
            if len(payload) != payload_size:
                raise EpsilonRouteError(f"{path} ended inside payload at offset {payload_offset}")
            yield RawRecord(
                source_file=path,
                raw_format=f"legacy_epsilon_v{version}",
                sequence=sequence,
                host_timestamp_us=host_timestamp_us,
                record_type=frame_tag,
                flags=reserved[0],
                payload=payload,
                record_offset=record_offset,
                payload_offset=payload_offset,
            )
            sequence += 1


def resolve_input_files(input_path: Path) -> list[Path]:
    if input_path.is_file():
        return [input_path]
    if not input_path.is_dir():
        raise EpsilonRouteError(f"input path does not exist: {input_path}")

    candidates = [
        input_path / "raw" / "epsilon.dat",
        input_path / "sensors" / "epsilon_raw.dat",
        input_path / "epsilon.dat",
        input_path / "epsilon_raw.dat",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return [candidate]
    raise EpsilonRouteError(
        f"no EPSILON raw file found under {input_path}; expected raw/epsilon.dat or sensors/epsilon_raw.dat"
    )


def iter_records(path: Path) -> Iterator[RawRecord]:
    with path.open("rb") as file:
        magic = file.read(8)
    if magic == UNIFIED_RAW_MAGIC:
        yield from iter_unified_raw_dat(path)
    elif magic == LEGACY_EPSILON_MAGIC:
        yield from iter_legacy_epsilon_raw_dat(path)
    else:
        yield from iter_raw_fdilink_stream(path)


def is_finite(value: Optional[float]) -> bool:
    return value is not None and math.isfinite(value)


def valid_lat_lon(latitude: Optional[float], longitude: Optional[float]) -> bool:
    return (
        is_finite(latitude)
        and is_finite(longitude)
        and -90.0 <= float(latitude) <= 90.0
        and -180.0 <= float(longitude) <= 180.0
        and not (abs(float(latitude)) < 1e-8 and abs(float(longitude)) < 1e-8)
    )


def timestamp_us_to_iso(timestamp_us: Optional[int]) -> str:
    if not timestamp_us:
        return ""
    seconds, micros = divmod(int(timestamp_us), 1_000_000)
    return datetime.fromtimestamp(seconds, timezone.utc).replace(microsecond=micros).isoformat().replace("+00:00", "Z")


def epsilon_time_to_iso(utc_unix_s: Optional[int], utc_microseconds: Optional[int]) -> str:
    if utc_unix_s is None:
        return ""
    timestamp_us = int(utc_unix_s) * 1_000_000 + int(utc_microseconds or 0)
    return timestamp_us_to_iso(timestamp_us)


def fix_name(fix_code: Optional[int]) -> str:
    if fix_code is None:
        return ""
    return FIX_NAMES.get(fix_code, "UNKNOWN")


def haversine_distance_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    lat1_rad = math.radians(lat1)
    lon1_rad = math.radians(lon1)
    lat2_rad = math.radians(lat2)
    lon2_rad = math.radians(lon2)
    d_lat = lat2_rad - lat1_rad
    d_lon = lon2_rad - lon1_rad
    a = math.sin(d_lat * 0.5) ** 2 + math.cos(lat1_rad) * math.cos(lat2_rad) * math.sin(d_lon * 0.5) ** 2
    c = 2.0 * math.atan2(math.sqrt(a), math.sqrt(max(0.0, 1.0 - a)))
    return EARTH_RADIUS_METERS * c


def new_point(record: RawRecord, frame: FrameInfo, state: DecodeState, position_source: str) -> EpsilonPoint:
    return EpsilonPoint(
        source_file=str(record.source_file),
        raw_format=record.raw_format,
        record_sequence=record.sequence,
        host_timestamp_us=record.host_timestamp_us or None,
        host_time_utc=timestamp_us_to_iso(record.host_timestamp_us),
        packet_id_hex=f"0x{frame.packet_id:02X}",
        packet_name=packet_name(frame.packet_id),
        position_source=position_source,
        serial_number=frame.serial_number,
        utc_unix_s=state.utc_unix_s,
        utc_microseconds=state.utc_microseconds,
        epsilon_time_utc=epsilon_time_to_iso(state.utc_unix_s, state.utc_microseconds),
        gnss_fix_code=state.gnss_fix_code,
        gnss_fix=fix_name(state.gnss_fix_code),
        gnss_satellites=state.gnss_satellites,
        hdop=state.hdop,
        vdop=state.vdop,
        hacc_m=state.hacc_m,
        vacc_m=state.vacc_m,
        lat_std_m=state.lat_std_m,
        lon_std_m=state.lon_std_m,
        height_std_m=state.height_std_m,
        diff_age_s=state.diff_age_s,
        vel_n_mps=state.vel_n_mps,
        vel_e_mps=state.vel_e_mps,
        vel_d_mps=state.vel_d_mps,
        ned_n_m=state.ned_n_m,
        ned_e_m=state.ned_e_m,
        ned_d_m=state.ned_d_m,
        roll_deg=state.roll_deg,
        pitch_deg=state.pitch_deg,
        yaw_deg=state.yaw_deg,
        system_status_bits=state.system_status_bits,
        filter_status_bits=state.filter_status_bits,
        update_status_bits=state.update_status_bits,
        record_offset=record.record_offset,
        payload_offset=record.payload_offset,
    )


def decode_coordinate_candidates(records: Iterable[RawRecord], allow_bad_crc: bool, stats: ParseStats) -> list[EpsilonPoint]:
    state = DecodeState()
    points: list[EpsilonPoint] = []

    for record in records:
        stats.raw_records += 1
        stats.epsilon_records += 1
        frame = parse_fdilink_frame(record.payload)
        if frame.ok:
            stats.valid_frames += 1
        else:
            stats.bad_frames += 1
            if not allow_bad_crc:
                continue

        payload = frame.payload
        packet_id = frame.packet_id
        payload_size = len(payload)

        if packet_id == 0x42 and payload_size >= 72:
            state.ned_n_m = read_float_le(payload, 24)
            state.ned_e_m = read_float_le(payload, 28)
            state.ned_d_m = read_float_le(payload, 32)
            state.vel_n_mps = read_float_le(payload, 36)
            state.vel_e_mps = read_float_le(payload, 40)
            state.vel_d_mps = read_float_le(payload, 44)
            continue

        if packet_id == 0x50 and payload_size >= 14:
            state.system_status_bits = read_u16_le(payload, 0)
            state.filter_status_bits = read_u16_le(payload, 2)
            state.update_status_bits = read_u16_le(payload, 4)
            state.utc_unix_s = read_u32_le(payload, 6)
            state.utc_microseconds = read_u32_le(payload, 10)
            state.gnss_fix_code = (state.filter_status_bits >> 4) & 0x0F
            if payload_size >= 50:
                state.vel_n_mps = read_float_le(payload, 38)
                state.vel_e_mps = read_float_le(payload, 42)
                state.vel_d_mps = read_float_le(payload, 46)
            if payload_size >= 78:
                state.roll_deg = rad_to_deg(read_float_le(payload, 66))
                state.pitch_deg = rad_to_deg(read_float_le(payload, 70))
                state.yaw_deg = rad_to_deg(read_float_le(payload, 74))
            if payload_size >= 102:
                state.lat_std_m = read_float_le(payload, 90)
                state.lon_std_m = read_float_le(payload, 94)
                state.height_std_m = read_float_le(payload, 98)
            if payload_size >= 38:
                point = new_point(record, frame, state, "system_state")
                point.latitude_deg = rad_to_deg(read_double_le(payload, 14))
                point.longitude_deg = rad_to_deg(read_double_le(payload, 22))
                point.height_m = read_double_le(payload, 30)
                points.append(point)
            continue

        if packet_id == 0x51 and payload_size >= 8:
            state.utc_unix_s = read_u32_le(payload, 0)
            state.utc_microseconds = read_u32_le(payload, 4)
            continue

        if packet_id == 0x52 and payload_size >= 14:
            utc = formatted_time_to_unix_us(payload)
            if utc is not None:
                state.utc_unix_s = utc // 1_000_000
                state.utc_microseconds = utc % 1_000_000
            continue

        if packet_id == 0x59 and payload_size >= 74:
            state.utc_unix_s = read_u32_le(payload, 0)
            state.utc_microseconds = read_u32_le(payload, 4)
            state.lat_std_m = read_float_le(payload, 44)
            state.lon_std_m = read_float_le(payload, 48)
            state.height_std_m = read_float_le(payload, 52)
            state.diff_age_s = read_float_le(payload, 64)
            raw_gnss_status = read_u16_le(payload, 72)
            state.gnss_fix_code = raw_gnss_status & 0x0F
            continue

        if packet_id == 0x5A and payload_size >= 9:
            state.hdop = read_float_le(payload, 0)
            state.vdop = read_float_le(payload, 4)
            state.gnss_satellites = payload[8]
            continue

        if packet_id == 0x5C and payload_size >= 32:
            point = new_point(record, frame, state, "geodetic_pos")
            point.latitude_deg = rad_to_deg(read_double_le(payload, 0))
            point.longitude_deg = rad_to_deg(read_double_le(payload, 8))
            point.height_m = read_double_le(payload, 16)
            point.hacc_m = read_float_le(payload, 24)
            point.vacc_m = read_float_le(payload, 28)
            state.hacc_m = point.hacc_m
            state.vacc_m = point.vacc_m
            points.append(point)
            continue

        if packet_id == 0x5D and payload_size >= 24:
            continue

    stats.coordinate_candidates = len(points)
    return points


def formatted_time_to_unix_us(payload: bytes) -> Optional[int]:
    try:
        microseconds = read_u32_le(payload, 0)
        year = read_u16_le(payload, 4)
        month = payload[8]
        day = payload[9]
        hour = payload[11]
        minute = payload[12]
        second = payload[13]
        dt = datetime(year, month, day, hour, minute, second, microseconds, tzinfo=timezone.utc)
        return int(dt.timestamp() * 1_000_000)
    except (ValueError, OverflowError):
        return None


def select_position_source(points: Sequence[EpsilonPoint], requested: str, stats: ParseStats) -> list[EpsilonPoint]:
    if requested == "auto":
        has_geodetic = any(point.position_source == "geodetic_pos" for point in points)
        requested = "geodetic" if has_geodetic else "system"
        stats.auto_position_source = requested

    if requested == "geodetic":
        selected = [point for point in points if point.position_source == "geodetic_pos"]
    elif requested == "system":
        selected = [point for point in points if point.position_source == "system_state"]
    elif requested == "both":
        selected = list(points)
    else:
        raise EpsilonRouteError(f"unsupported position source: {requested}")

    for index, point in enumerate(selected, start=1):
        point.source_point_index = index
    stats.selected_source_candidates = len(selected)
    return selected


def parse_index_ranges(values: Sequence[str], index_base: int, label: str) -> set[int]:
    result: set[int] = set()
    for value in values:
        for part in value.split(","):
            token = part.strip()
            if not token:
                continue
            if "-" in token:
                start_text, end_text = token.split("-", 1)
                start = cli_index_to_one_based(start_text.strip(), index_base, label)
                end = cli_index_to_one_based(end_text.strip(), index_base, label)
                if end < start:
                    start, end = end, start
                result.update(range(start, end + 1))
            else:
                result.add(cli_index_to_one_based(token, index_base, label))
    return result


def cli_index_to_one_based(value: str | int, index_base: int, label: str) -> int:
    try:
        index = int(value)
    except ValueError as exc:
        raise EpsilonRouteError(f"{label} must be an integer, got {value!r}") from exc
    if index_base == 0:
        index += 1
    if index <= 0:
        raise EpsilonRouteError(f"{label} must resolve to a positive 1-based index, got {value!r}")
    return index


def parse_fix_code(value: Optional[str]) -> Optional[int]:
    if value is None or value == "":
        return None
    text = value.strip().upper()
    if text in FIX_NAME_TO_CODE:
        return FIX_NAME_TO_CODE[text]
    try:
        code = int(text)
    except ValueError as exc:
        choices = ", ".join(FIX_NAME_TO_CODE)
        raise EpsilonRouteError(f"--min-fix must be a code or one of: {choices}") from exc
    if code < 0:
        raise EpsilonRouteError("--min-fix cannot be negative")
    return code


def apply_filters(
    points: Sequence[EpsilonPoint],
    include_indices: set[int],
    exclude_indices: set[int],
    min_fix_code: Optional[int],
    max_hacc: Optional[float],
    max_vacc: Optional[float],
    max_segment_m: Optional[float],
    stats: ParseStats,
) -> list[EpsilonPoint]:
    filtered: list[EpsilonPoint] = []
    for point in points:
        if include_indices and point.source_point_index not in include_indices:
            continue
        if point.source_point_index in exclude_indices:
            continue
        if not valid_lat_lon(point.latitude_deg, point.longitude_deg):
            continue
        if min_fix_code is not None:
            if point.gnss_fix_code is None or point.gnss_fix_code < min_fix_code:
                continue
        if max_hacc is not None and is_finite(point.hacc_m) and float(point.hacc_m) > max_hacc:
            continue
        if max_vacc is not None and is_finite(point.vacc_m) and float(point.vacc_m) > max_vacc:
            continue
        filtered.append(point)

    if max_segment_m is not None and filtered:
        jump_filtered: list[EpsilonPoint] = [filtered[0]]
        for point in filtered[1:]:
            previous = jump_filtered[-1]
            distance = haversine_distance_m(
                float(previous.latitude_deg),
                float(previous.longitude_deg),
                float(point.latitude_deg),
                float(point.longitude_deg),
            )
            if distance <= max_segment_m:
                jump_filtered.append(point)
        filtered = jump_filtered

    stats.filtered_points = stats.selected_source_candidates - len(filtered)
    return filtered


def assign_route_distances(points: Sequence[EpsilonPoint]) -> None:
    cumulative = 0.0
    previous: Optional[EpsilonPoint] = None
    for index, point in enumerate(points, start=1):
        point.point_index = index
        if previous is None:
            point.segment_distance_m = 0.0
        else:
            point.segment_distance_m = haversine_distance_m(
                float(previous.latitude_deg),
                float(previous.longitude_deg),
                float(point.latitude_deg),
                float(point.longitude_deg),
            )
        cumulative += point.segment_distance_m
        point.cumulative_distance_m = cumulative
        previous = point


def route_distance_between(points: Sequence[EpsilonPoint], start_index: int, end_index: int) -> float:
    if not points:
        raise EpsilonRouteError("no route points are available after filtering")
    if start_index > len(points) or end_index > len(points):
        raise EpsilonRouteError(f"distance index out of range: route has {len(points)} points")
    lo = min(start_index, end_index)
    hi = max(start_index, end_index)
    if lo == hi:
        return 0.0
    return points[hi - 1].cumulative_distance_m - points[lo - 1].cumulative_distance_m


def csv_value(value: object) -> str:
    if value is None:
        return ""
    if isinstance(value, float):
        if not math.isfinite(value):
            return ""
        return f"{value:.10g}"
    return str(value)


def export_csv(points: Sequence[EpsilonPoint], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8-sig") as file:
        writer = csv.writer(file)
        writer.writerow(CSV_FIELDS)
        for point in points:
            writer.writerow([csv_value(getattr(point, field)) for field in CSV_FIELDS])


def clamp_latitude(latitude: float) -> float:
    return max(-85.05112878, min(85.05112878, latitude))


def lat_lon_to_world(latitude: float, longitude: float, zoom: int) -> tuple[float, float]:
    lat = math.radians(clamp_latitude(latitude))
    world_size = TILE_SIZE * (2**zoom)
    x = (longitude + 180.0) / 360.0 * world_size
    y = (0.5 - math.log((1.0 + math.sin(lat)) / (1.0 - math.sin(lat))) / (4.0 * math.pi)) * world_size
    return x, y


def choose_zoom(points: Sequence[EpsilonPoint], width: int, height: int, min_zoom: int, max_zoom: int) -> int:
    if len(points) <= 1:
        return min(max_zoom, 17)
    latitudes = [float(point.latitude_deg) for point in points]
    longitudes = [float(point.longitude_deg) for point in points]
    for zoom in range(max_zoom, min_zoom - 1, -1):
        pixels = [lat_lon_to_world(lat, lon, zoom) for lat, lon in zip(latitudes, longitudes)]
        xs = [pixel[0] for pixel in pixels]
        ys = [pixel[1] for pixel in pixels]
        if max(xs) - min(xs) <= max(64, width - 120) and max(ys) - min(ys) <= max(64, height - 120):
            return zoom
    return min_zoom


def html_escape(value: object) -> str:
    text = str(value)
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
        .replace("'", "&#39;")
    )


def json_for_html(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")


def map_point_to_json(point: EpsilonPoint) -> dict[str, object]:
    return {
        "point_index": point.point_index,
        "source_point_index": point.source_point_index,
        "latitude_deg": point.latitude_deg,
        "longitude_deg": point.longitude_deg,
        "height_m": point.height_m,
        "host_time_utc": point.host_time_utc,
        "epsilon_time_utc": point.epsilon_time_utc,
        "packet_name": point.packet_name,
        "position_source": point.position_source,
        "gnss_fix": point.gnss_fix,
        "gnss_fix_code": point.gnss_fix_code,
        "gnss_satellites": point.gnss_satellites,
        "hacc_m": point.hacc_m,
        "vacc_m": point.vacc_m,
    }


def write_map_html(
    points: Sequence[EpsilonPoint],
    path: Path,
    title: str,
    start_index: int,
    end_index: int,
    width: int,
    height: int,
    zoom: Optional[int],
    min_zoom: int,
    max_zoom: int,
    map_provider: str,
    tianditu_key: str,
) -> None:
    if not points:
        raise EpsilonRouteError("cannot draw map because no route points remain after filtering")

    zoom_value = zoom if zoom is not None else choose_zoom(points, width, height, min_zoom, max_zoom)
    latitudes = [float(point.latitude_deg) for point in points]
    longitudes = [float(point.longitude_deg) for point in points]
    center_lat = (min(latitudes) + max(latitudes)) / 2.0
    center_lon = (min(longitudes) + max(longitudes)) / 2.0
    route_distance = route_distance_between(points, start_index, end_index)
    total_distance = points[-1].cumulative_distance_m if points else 0.0
    points_json = json_for_html([map_point_to_json(point) for point in points])
    initial_json = json_for_html({
        "width": width,
        "height": height,
        "zoom": zoom_value,
        "minZoom": min_zoom,
        "maxZoom": max_zoom,
        "centerLat": center_lat,
        "centerLon": center_lon,
        "startIndex": start_index,
        "endIndex": end_index,
        "provider": map_provider,
        "tiandituKey": tianditu_key,
    })

    path.parent.mkdir(parents=True, exist_ok=True)
    html_template = Template("""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <title>${title}</title>
<style>
    :root {
      --bg: #faf9f5;
      --surface: #fffdf8;
      --surface-2: #f7f3ea;
      --ink: #141413;
      --muted: #68645d;
      --line: #e8e6dc;
      --line-soft: rgba(20,20,19,.13);
      --accent: #d97757;
      --accent-deep: #a84d35;
      --accent-soft: #fff1eb;
      --blue: #6a9bcc;
      --blue-soft: #e9f2fb;
      --ease: cubic-bezier(.32,.72,0,1);
    }
    * { box-sizing: border-box; }
    [hidden] { display:none!important; }
    body {
      position:relative; isolation:isolate;
      margin:0; min-height:100dvh; overflow-x:hidden;
      color:var(--ink); background:var(--bg);
      font-family:"Satoshi","Plus Jakarta Sans","Aptos","Microsoft YaHei",ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
      -webkit-font-smoothing:antialiased; text-rendering:optimizeLegibility;
    }
    body::before {
      position:fixed; inset:-8%; z-index:0; content:""; pointer-events:none;
      background:radial-gradient(circle at 12% 14%,rgba(217,119,87,.12),transparent 28%),radial-gradient(circle at 84% 18%,rgba(106,155,204,.13),transparent 28%),linear-gradient(180deg,#faf9f5 0%,#f4efe7 54%,#e8e6dc 100%);
      background-size:120% 120%; animation:bgAurora 18s ease-in-out infinite alternate;
    }
    body::after {
      position:fixed; inset:0; z-index:1; content:""; pointer-events:none; opacity:.56;
      background-image:radial-gradient(circle at 1px 1px,rgba(35,30,24,.07) 1px,transparent 0),linear-gradient(rgba(35,30,24,.026) 1px,transparent 1px),linear-gradient(90deg,rgba(35,30,24,.024) 1px,transparent 1px);
      background-size:18px 18px,64px 64px,64px 64px;
      mask-image:linear-gradient(to bottom,#000 0%,#000 72%,transparent 100%);
      animation:gridDrift 28s linear infinite;
    }
    .ambient-bg {
      position:fixed; inset:0; z-index:2; overflow:hidden; pointer-events:none; mix-blend-mode:multiply;
    }
    .ambient-bg::before {
      position:absolute; inset:-22%; content:""; opacity:.30;
      background:conic-gradient(from 82deg at 18% 22%,transparent 0deg,rgba(217,119,87,.20) 42deg,transparent 94deg,rgba(106,155,204,.16) 152deg,transparent 220deg,rgba(168,77,53,.13) 290deg,transparent 360deg),radial-gradient(ellipse at 50% 50%,rgba(255,253,248,.56),transparent 58%);
      filter:blur(42px); transform:translate3d(0,0,0) rotate(0deg) scale(1.02); animation:veilSweep 26s ease-in-out infinite alternate;
    }
    .ambient-bg::after {
      position:absolute; inset:0; content:""; opacity:.24;
      background-image:radial-gradient(circle at 18% 22%,rgba(217,119,87,.22) 0 1px,transparent 2px),radial-gradient(circle at 78% 28%,rgba(106,155,204,.20) 0 1px,transparent 2px),radial-gradient(circle at 46% 72%,rgba(168,77,53,.16) 0 1px,transparent 2px);
      background-size:118px 118px,154px 154px,202px 202px; animation:sparkDrift 36s linear infinite;
    }
    .ambient-bg span {
      position:absolute; width:clamp(260px,30vw,560px); aspect-ratio:1; border-radius:999px; opacity:.36;
      filter:blur(34px); transform:translate3d(0,0,0); animation:orbDrift 14s var(--ease) infinite alternate;
    }
    .ambient-bg span:nth-child(1){ left:-10vw; top:6vh; background:radial-gradient(circle,rgba(217,119,87,.40),rgba(217,119,87,0) 68%); }
    .ambient-bg span:nth-child(2){ right:-9vw; top:1vh; width:clamp(260px,34vw,620px); background:radial-gradient(circle,rgba(106,155,204,.36),rgba(106,155,204,0) 70%); animation-duration:18s; animation-delay:-5s; }
    .ambient-bg span:nth-child(3){ left:26vw; bottom:-18vh; width:clamp(280px,38vw,680px); background:radial-gradient(circle,rgba(168,77,53,.25),rgba(168,77,53,0) 72%); animation-duration:21s; animation-delay:-9s; }
    .page {
      position:relative; z-index:5; width:min(100%,1480px); margin:0 auto;
    }
    ::selection { background:rgba(217,119,87,.16); color:var(--ink); }
    :focus-visible { outline:3px solid rgba(217,119,87,.32); outline-offset:3px; }
    header {
      padding:20px clamp(18px,2.6vw,44px); background:var(--surface); border-bottom:1px solid var(--line);
    }
    h1 { margin:0 0 6px; font-size:21px; color:var(--ink); font-weight:780; letter-spacing:-.03em; }
    h3 { margin:14px 0 8px; font-size:15px; color:var(--ink); font-weight:740; }
    .summary { display:flex; flex-wrap:wrap; gap:10px 20px; font-size:14px; color:var(--muted); line-height:1.6; }
    .layout { display:grid; grid-template-columns:minmax(0,1fr) 390px; gap:14px; padding:14px clamp(12px,2.2vw,36px) 24px; }
    .panel { background:var(--surface); border:1px solid var(--line); border-radius:16px; padding:14px; box-shadow:0 14px 38px rgba(20,20,19,.07); }
    .controls { display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-bottom:10px; }
    .controls .wide { grid-column:1/-1; }
    label { display:flex; flex-direction:column; gap:4px; font-size:12px; color:var(--muted); font-weight:620; }
    input, select, button { border-radius:10px; border:1px solid var(--line); background:var(--surface); color:var(--ink); padding:8px 10px; font:inherit; }
    input:focus, select:focus { outline:none; border-color:var(--accent); box-shadow:0 0 0 3px rgba(217,119,87,.18); }
    button { cursor:pointer; background:var(--surface-2); border-color:var(--line); transition:transform .32s var(--ease),box-shadow .32s var(--ease),border-color .32s var(--ease),background .32s var(--ease); }
    button:hover { background:var(--line); border-color:var(--muted); }
    button:active { transform:translateY(1px); }
    button.primary { background:linear-gradient(180deg,#e28767,#a84d35); border-color:rgba(168,77,53,.68); color:#fff; font-weight:720; box-shadow:0 8px 22px rgba(217,119,87,.18); }
    button.primary:hover { box-shadow:0 12px 28px rgba(217,119,87,.24); }
    .button-row { display:flex; flex-wrap:wrap; gap:8px; }
    .hint { color:var(--muted); font-size:12px; line-height:1.55; }
    .metric { margin:10px 0; padding:12px; border-radius:12px; background:var(--surface-2); border:1px solid var(--line); line-height:1.8; font-size:13px; }
    .metric strong { color:var(--accent); font-weight:780; }
    .error { color:var(--accent-deep); font-weight:600; }
    .zoom-ctrl { position:absolute; right:12px; top:12px; display:flex; flex-direction:column; align-items:center; gap:2px; z-index:10; }
    .zoom-ctrl button { width:34px; height:34px; border-radius:10px; border:1px solid var(--line); background:var(--surface); color:var(--muted); font-size:18px; line-height:1; cursor:pointer; display:flex; align-items:center; justify-content:center; box-shadow:0 2px 8px rgba(20,20,19,.08); padding:0; }
    .zoom-ctrl button:hover { background:var(--surface-2); border-color:var(--accent); color:var(--accent); }
    .zoom-ctrl .zoom-level { font-size:11px; color:var(--muted); font-weight:700; line-height:1.8; }
    #map { position:relative; width:${width}px; height:${height}px; max-width:100%; overflow:hidden; background:var(--surface-2); border:1px solid var(--line); border-radius:14px; box-shadow:0 14px 38px rgba(20,20,19,.07); }
    .tile { position:absolute; width:256px; height:256px; user-select:none; pointer-events:none; }
    svg { position:absolute; inset:0; width:100%; height:100%; overflow:hidden; cursor:crosshair; }
    .route-all { fill:none; stroke:rgba(104,98,93,.42); stroke-width:4; stroke-linejoin:round; stroke-linecap:round; }
    .route-selected { fill:none; stroke:var(--accent); stroke-width:5; stroke-linejoin:round; stroke-linecap:round; filter:drop-shadow(0 2px 4px rgba(217,119,87,.2)); }
    .point { fill:var(--blue); stroke:#fff; stroke-width:1.5; opacity:.80; }
    .start { fill:#16a34a; stroke:#fff; stroke-width:2; filter:drop-shadow(0 2px 4px rgba(22,163,74,.24)); }
    .end { fill:var(--accent-deep); stroke:#fff; stroke-width:2; filter:drop-shadow(0 2px 4px rgba(168,77,53,.24)); }
    .nearest { fill:#f59e0b; stroke:#fff; stroke-width:2; filter:drop-shadow(0 2px 4px rgba(245,158,11,.24)); }
    .attrib { position:absolute; left:8px; bottom:6px; font-size:12px; background:rgba(255,253,248,.92); color:var(--muted); padding:3px 8px; border-radius:8px; border:1px solid var(--line); }
    table { width:100%; border-collapse:collapse; font-size:12px; }
    th, td { padding:6px 5px; border-bottom:1px solid var(--line); text-align:left; white-space:nowrap; }
    th { color:var(--muted); font-weight:660; position:sticky; top:0; background:var(--surface-2); }
    tbody tr:hover { background:var(--accent-soft); }
    .table-wrap { max-height:420px; overflow:auto; border:1px solid var(--line); border-radius:10px; }
    .small-button { padding:4px 6px; font-size:11px; border-radius:8px; background:var(--surface-2); border-color:var(--line); }
    .small-button:hover { background:var(--line); }
    @keyframes bgAurora {
      0%{transform:translate3d(-1.4%,0,0) scale(1);background-position:0% 0%}
      50%{transform:translate3d(1.2%,-1%,0) scale(1.025);background-position:50% 32%}
      100%{transform:translate3d(0,1.2%,0) scale(1.04);background-position:100% 60%}
    }
    @keyframes gridDrift {
      from{background-position:0 0,0 0,0 0}
      to{background-position:18px 18px,64px 64px,64px 64px}
    }
    @keyframes orbDrift {
      0%{transform:translate3d(0,0,0) scale(1)}
      33%{transform:translate3d(5vw,-3vh,0) scale(1.10)}
      66%{transform:translate3d(-2vw,4vh,0) scale(.96)}
      100%{transform:translate3d(4vw,3vh,0) scale(1.04)}
    }
    @keyframes veilSweep {
      0%{transform:translate3d(-3vw,-1vh,0) rotate(-8deg) scale(1.02)}
      50%{transform:translate3d(2vw,2vh,0) rotate(7deg) scale(1.08)}
      100%{transform:translate3d(4vw,-2vh,0) rotate(13deg) scale(1.04)}
    }
    @keyframes sparkDrift {
      from{background-position:0 0,0 0,0 0}
      to{background-position:118px 236px,-154px 154px,202px -202px}
    }
    @media (max-width:1180px) {
      .layout { grid-template-columns:1fr; }
      #map { width:100%; }
    }
    @media (prefers-reduced-motion:reduce) {
      body::before, body::after, .ambient-bg::before, .ambient-bg::after, .ambient-bg span { animation:none!important; }
    }
  </style>
	      .layout { grid-template-columns: 1fr; }
	      #map { width: 100%; }
	    }
	  </style>
</head>
<body>
<div class="ambient-bg" aria-hidden="true"><span></span><span></span><span></span></div>
<div class="page">
  <header>
    <h1>${title}</h1>
    <div class="summary">
      <span>原始点数: ${point_count}</span>
      <span>初始总轨迹距离: ${total_distance} m</span>
      <span>初始 #${start_index} 到 #${end_index}: ${route_distance} m</span>
      <span>默认底图: 天地图矢量</span>
    </div>
  </header>
  <main class="layout">
    <section class="panel">
<div id="map">
	        <div id="tiles"></div>
	        <div class="zoom-ctrl">
	          <button id="zoomInBtn" title="放大">+</button>
	          <span class="zoom-level" id="zoomLabel">${zoom_value}</span>
	          <button id="zoomOutBtn" title="缩小">&minus;</button>
	        </div>
	        <svg viewBox="0 0 ${width} ${height}" aria-label="EPSILON route">
          <polyline id="routeAll" class="route-all" points="" />
          <polyline id="routeSelected" class="route-selected" points="" />
          <g id="markers"></g>
        </svg>
        <div id="attrib" class="attrib">天地图</div>
      </div>
      <p class="hint">提示：地图点选会选择距离鼠标最近的当前可见轨迹点。点很多时只绘制抽样小点，但最近点选择会在全部可见点中查找。</p>
    </section>
    <aside class="panel">
      <div class="controls">
        <label>底图
          <select id="provider">
            <option value="tianditu_vec">天地图矢量</option>
            <option value="tianditu_img">天地图卫星</option>
            <option value="osm">OpenStreetMap</option>
          </select>
        </label>
        <label>点选动作
          <select id="clickMode">
            <option value="start">点地图设为起点</option>
            <option value="end">点地图设为终点</option>
            <option value="filter">点地图加入过滤</option>
          </select>
        </label>
        <label>起点 point_index
          <input id="startIndex" type="number" min="1" step="1" />
        </label>
        <label>终点 point_index
          <input id="endIndex" type="number" min="1" step="1" />
        </label>
        <label class="wide">过滤点 point_index，支持逗号和范围，如 12,20-25
          <input id="filterIndices" placeholder="例如：12,20-25" />
        </label>
        <label class="wide">天地图 Key
          <input id="tiandituKey" />
        </label>
      </div>
      <div class="button-row">
        <button id="applyButton" class="primary">应用过滤并重算</button>
        <button id="fitButton">适应当前轨迹</button>
        <button id="resetButton">重置过滤</button>
      </div>
      <div id="metrics" class="metric"></div>
      <div class="hint">路线距离按过滤后的轨迹顺序逐段累加，不是起终点直线距离，也不是道路导航距离。</div>
      <h3>当前轨迹点</h3>
      <div class="table-wrap">
        <table>
          <thead><tr><th>#</th><th>经纬度</th><th>fix</th><th>操作</th></tr></thead>
          <tbody id="pointTable"></tbody>
        </table>
      </div>
      <p id="tableNote" class="hint"></p>
    </aside>
  </main>
  <script>
    const POINTS = ${points_json};
    const INITIAL = ${initial_json};
    const TILE_SIZE = 256;
    const MAX_TABLE_ROWS = 500;
    const tilesEl = document.getElementById("tiles");
    const routeAllEl = document.getElementById("routeAll");
    const routeSelectedEl = document.getElementById("routeSelected");
    const markersEl = document.getElementById("markers");
    const providerEl = document.getElementById("provider");
    const clickModeEl = document.getElementById("clickMode");
    const startEl = document.getElementById("startIndex");
    const endEl = document.getElementById("endIndex");
    const filterEl = document.getElementById("filterIndices");
    const keyEl = document.getElementById("tiandituKey");
    const metricsEl = document.getElementById("metrics");
    const tableEl = document.getElementById("pointTable");
    const tableNoteEl = document.getElementById("tableNote");
    const attribEl = document.getElementById("attrib");
    let state = {
      zoom: INITIAL.zoom,
      centerLat: INITIAL.centerLat,
      centerLon: INITIAL.centerLon,
      centerWorld: null,
      excluded: new Set(),
      active: [],
      nearestIndex: null
    };

    providerEl.value = INITIAL.provider;
    startEl.value = INITIAL.startIndex;
    endEl.value = INITIAL.endIndex;
    keyEl.value = INITIAL.tiandituKey;

    function finiteNumber(value) {
      return typeof value === "number" && Number.isFinite(value);
    }

    function fmt(value, digits) {
      return finiteNumber(value) ? value.toFixed(digits) : "";
    }

    function clampLatitude(latitude) {
      return Math.max(-85.05112878, Math.min(85.05112878, latitude));
    }

    function worldSize(zoom) {
      return TILE_SIZE * Math.pow(2, zoom);
    }

    function latLonToWorld(latitude, longitude, zoom) {
      const lat = clampLatitude(latitude) * Math.PI / 180.0;
      const size = worldSize(zoom);
      const x = (longitude + 180.0) / 360.0 * size;
      const y = (0.5 - Math.log((1.0 + Math.sin(lat)) / (1.0 - Math.sin(lat))) / (4.0 * Math.PI)) * size;
      return { x, y };
    }

    function pointToScreen(point) {
      const world = latLonToWorld(point.latitude_deg, point.longitude_deg, state.zoom);
      return {
        x: world.x - state.centerWorld.x + INITIAL.width / 2,
        y: world.y - state.centerWorld.y + INITIAL.height / 2
      };
    }

    function haversine(a, b) {
      const r = 6371000.0;
      const lat1 = a.latitude_deg * Math.PI / 180.0;
      const lat2 = b.latitude_deg * Math.PI / 180.0;
      const dLat = lat2 - lat1;
      const dLon = (b.longitude_deg - a.longitude_deg) * Math.PI / 180.0;
      const h = Math.sin(dLat / 2) ** 2 + Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLon / 2) ** 2;
      return r * 2 * Math.atan2(Math.sqrt(h), Math.sqrt(Math.max(0, 1 - h)));
    }

    function parseIndexSet(text) {
      const result = new Set();
      for (const rawPart of text.split(",")) {
        const part = rawPart.trim();
        if (!part) continue;
        if (part.includes("-")) {
          const pieces = part.split("-", 2);
          let start = Number.parseInt(pieces[0].trim(), 10);
          let end = Number.parseInt(pieces[1].trim(), 10);
          if (!Number.isFinite(start) || !Number.isFinite(end)) continue;
          if (end < start) [start, end] = [end, start];
          for (let i = start; i <= end; ++i) result.add(i);
        } else {
          const value = Number.parseInt(part, 10);
          if (Number.isFinite(value)) result.add(value);
        }
      }
      return result;
    }

    function setToText(setValue) {
      return Array.from(setValue).sort((a, b) => a - b).join(",");
    }

    function currentActivePoints() {
      return POINTS.filter(point => !state.excluded.has(point.point_index));
    }

    function chooseZoomFor(points) {
      if (points.length <= 1) return Math.min(INITIAL.maxZoom, 17);
      for (let zoom = INITIAL.maxZoom; zoom >= INITIAL.minZoom; --zoom) {
        const pixels = points.map(point => latLonToWorld(point.latitude_deg, point.longitude_deg, zoom));
        const xs = pixels.map(pixel => pixel.x);
        const ys = pixels.map(pixel => pixel.y);
        if (Math.max(...xs) - Math.min(...xs) <= Math.max(64, INITIAL.width - 120) &&
            Math.max(...ys) - Math.min(...ys) <= Math.max(64, INITIAL.height - 120)) {
          return zoom;
        }
      }
      return INITIAL.minZoom;
    }

    function fitTo(points) {
      if (!points.length) return;
      state.zoom = chooseZoomFor(points);
      const minLat = Math.min(...points.map(point => point.latitude_deg));
      const maxLat = Math.max(...points.map(point => point.latitude_deg));
      const minLon = Math.min(...points.map(point => point.longitude_deg));
      const maxLon = Math.max(...points.map(point => point.longitude_deg));
      state.centerLat = (minLat + maxLat) / 2;
      state.centerLon = (minLon + maxLon) / 2;
      state.centerWorld = latLonToWorld(state.centerLat, state.centerLon, state.zoom);
    }

    function providerLayers(provider) {
      if (provider === "tianditu_img") {
        return [
          { endpoint: "img_w", layer: "img", suffix: "img" },
          { endpoint: "cia_w", layer: "cia", suffix: "cia" }
        ];
      }
      if (provider === "tianditu_vec") {
        return [
          { endpoint: "vec_w", layer: "vec", suffix: "vec" },
          { endpoint: "cva_w", layer: "cva", suffix: "cva" }
        ];
      }
      return [{ endpoint: "", layer: "", suffix: "osm" }];
    }

    function tileUrl(provider, zoom, tileX, tileY, layer) {
      const worldTiles = Math.pow(2, zoom);
      const wrappedX = ((tileX % worldTiles) + worldTiles) % worldTiles;
      if (provider === "osm") {
        return "https://tile.openstreetmap.org/" + zoom + "/" + wrappedX + "/" + tileY + ".png";
      }
      const key = encodeURIComponent(keyEl.value.trim());
      const shardSeed = Math.abs(tileX * 31 + tileY * 17 + layer.suffix.length * 13) % 8;
      const host = "https://t" + shardSeed + ".tianditu.gov.cn/" + layer.endpoint + "/wmts";
      return host + "?SERVICE=WMTS&REQUEST=GetTile&VERSION=1.0.0&LAYER=" + layer.layer +
        "&STYLE=default&TILEMATRIXSET=w&FORMAT=tiles&TILECOL=" + wrappedX +
        "&TILEROW=" + tileY + "&TILEMATRIX=" + zoom + "&tk=" + key;
    }

    function renderTiles() {
      tilesEl.replaceChildren();
      const provider = providerEl.value;
      const layers = providerLayers(provider);
      const center = state.centerWorld;
      const topLeftX = center.x - INITIAL.width / 2;
      const topLeftY = center.y - INITIAL.height / 2;
      const worldTiles = Math.pow(2, state.zoom);
      const minTileX = Math.floor(topLeftX / TILE_SIZE);
      const maxTileX = Math.floor((topLeftX + INITIAL.width) / TILE_SIZE);
      const minTileY = Math.max(0, Math.floor(topLeftY / TILE_SIZE));
      const maxTileY = Math.min(worldTiles - 1, Math.floor((topLeftY + INITIAL.height) / TILE_SIZE));
      for (let tileX = minTileX; tileX <= maxTileX; ++tileX) {
        for (let tileY = minTileY; tileY <= maxTileY; ++tileY) {
          for (const layer of layers) {
            const img = document.createElement("img");
            img.className = "tile";
            img.src = tileUrl(provider, state.zoom, tileX, tileY, layer);
            img.alt = "";
            img.style.left = (tileX * TILE_SIZE - topLeftX).toFixed(2) + "px";
            img.style.top = (tileY * TILE_SIZE - topLeftY).toFixed(2) + "px";
            tilesEl.appendChild(img);
          }
        }
      }
      attribEl.textContent = provider === "osm" ? "© OpenStreetMap contributors" : "底图数据 © 天地图";
    }

    function polyline(points) {
      return points.map(point => {
        const screen = pointToScreen(point);
        return screen.x.toFixed(2) + "," + screen.y.toFixed(2);
      }).join(" ");
    }

    function routeInfo(active, startIndex, endIndex) {
      const startPos = active.findIndex(point => point.point_index === startIndex);
      const endPos = active.findIndex(point => point.point_index === endIndex);
      if (startPos < 0 || endPos < 0) {
        return { ok: false, message: "起点或终点已被过滤，或点号不存在。" };
      }
      const lo = Math.min(startPos, endPos);
      const hi = Math.max(startPos, endPos);
      let distance = 0;
      for (let i = lo + 1; i <= hi; ++i) distance += haversine(active[i - 1], active[i]);
      let total = 0;
      for (let i = 1; i < active.length; ++i) total += haversine(active[i - 1], active[i]);
      return { ok: true, distance, total, segment: active.slice(lo, hi + 1), start: active[startPos], end: active[endPos] };
    }

    function addCircle(point, className, radius, titleText) {
      const screen = pointToScreen(point);
      const circle = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      circle.setAttribute("class", className);
      circle.setAttribute("cx", screen.x.toFixed(2));
      circle.setAttribute("cy", screen.y.toFixed(2));
      circle.setAttribute("r", radius);
      const title = document.createElementNS("http://www.w3.org/2000/svg", "title");
      title.textContent = titleText;
      circle.appendChild(title);
      markersEl.appendChild(circle);
    }

    function renderMarkers(active, info) {
      markersEl.replaceChildren();
      const step = Math.max(1, Math.floor(active.length / 400));
      for (let i = 0; i < active.length; i += step) {
        const point = active[i];
        addCircle(point, "point", "2", "#" + point.point_index + " " + fmt(point.latitude_deg, 8) + ", " + fmt(point.longitude_deg, 8));
      }
      if (info.ok) {
        addCircle(info.start, "start", "6", "起点 #" + info.start.point_index);
        addCircle(info.end, "end", "6", "终点 #" + info.end.point_index);
      }
      if (state.nearestIndex !== null) {
        const nearest = active.find(point => point.point_index === state.nearestIndex);
        if (nearest) addCircle(nearest, "nearest", "5", "最近选中 #" + nearest.point_index);
      }
    }

    function renderTable(active) {
      tableEl.replaceChildren();
      const shown = active.slice(0, MAX_TABLE_ROWS);
      for (const point of shown) {
        const tr = document.createElement("tr");
        tr.innerHTML =
          "<td>" + point.point_index + "</td>" +
          "<td>" + fmt(point.latitude_deg, 6) + "<br>" + fmt(point.longitude_deg, 6) + "</td>" +
          "<td>" + (point.gnss_fix || "") + "</td>" +
          "<td>" +
          "<button class='small-button' data-action='start' data-index='" + point.point_index + "'>起</button> " +
          "<button class='small-button' data-action='end' data-index='" + point.point_index + "'>终</button> " +
          "<button class='small-button' data-action='filter' data-index='" + point.point_index + "'>滤</button>" +
          "</td>";
        tableEl.appendChild(tr);
      }
      tableNoteEl.textContent = active.length > MAX_TABLE_ROWS
        ? "仅显示前 " + MAX_TABLE_ROWS + " 个当前可见点；更远点可直接输入 point_index。"
        : "当前可见点已全部显示。";
    }

    function render() {
      document.getElementById("zoomLabel").textContent = state.zoom;
      state.excluded = parseIndexSet(filterEl.value);
      state.active = currentActivePoints();
      if (!state.centerWorld) fitTo(state.active.length ? state.active : POINTS);
      const startIndex = Number.parseInt(startEl.value, 10);
      const endIndex = Number.parseInt(endEl.value, 10);
      const info = routeInfo(state.active, startIndex, endIndex);
      routeAllEl.setAttribute("points", polyline(state.active));
      routeSelectedEl.setAttribute("points", info.ok ? polyline(info.segment) : "");
      renderTiles();
      renderMarkers(state.active, info);
      renderTable(state.active);
      if (!info.ok) {
        metricsEl.innerHTML = "<div class='error'>" + info.message + "</div>" +
          "<div>当前可见点数: <strong>" + state.active.length + "</strong></div>" +
          "<div>已过滤点数: <strong>" + state.excluded.size + "</strong></div>";
        return;
      }
      metricsEl.innerHTML =
        "<div>当前可见点数: <strong>" + state.active.length + "</strong></div>" +
        "<div>已过滤点数: <strong>" + state.excluded.size + "</strong></div>" +
        "<div>当前总路线距离: <strong>" + info.total.toFixed(3) + " m</strong></div>" +
        "<div>#" + startIndex + " 到 #" + endIndex + " 路线距离: <strong>" + info.distance.toFixed(3) + " m</strong></div>" +
        "<div>Zoom: <strong>" + state.zoom + "</strong></div>";
    }

    document.getElementById("applyButton").addEventListener("click", () => {
      state.nearestIndex = null;
      render();
    });
    document.getElementById("fitButton").addEventListener("click", () => {
      state.excluded = parseIndexSet(filterEl.value);
      const active = currentActivePoints();
      fitTo(active.length ? active : POINTS);
      render();
    });
    document.getElementById("resetButton").addEventListener("click", () => {
      filterEl.value = "";
      startEl.value = INITIAL.startIndex;
      endEl.value = INITIAL.endIndex;
      state.nearestIndex = null;
      fitTo(POINTS);
      render();
    });
    providerEl.addEventListener("change", renderTiles);
    keyEl.addEventListener("change", renderTiles);
    function zoomTo(zoom) { state.zoom = Math.max(INITIAL.minZoom, Math.min(INITIAL.maxZoom, zoom)); if (state.centerLat != null) state.centerWorld = latLonToWorld(state.centerLat, state.centerLon, state.zoom); render(); }
    document.getElementById("zoomInBtn").addEventListener("click", () => zoomTo(state.zoom + 1));
    document.getElementById("zoomOutBtn").addEventListener("click", () => zoomTo(state.zoom - 1));
    const mapContainer = document.getElementById("map");
    mapContainer.addEventListener("wheel", event => { event.preventDefault(); const dz = event.deltaY < 0 ? 1 : -1; zoomTo(state.zoom + dz); }, { passive: false });
    tableEl.addEventListener("click", event => {
      const button = event.target.closest("button[data-action]");
      if (!button) return;
      const index = Number.parseInt(button.dataset.index, 10);
      if (button.dataset.action === "start") startEl.value = index;
      if (button.dataset.action === "end") endEl.value = index;
      if (button.dataset.action === "filter") {
        const setValue = parseIndexSet(filterEl.value);
        setValue.add(index);
        filterEl.value = setToText(setValue);
      }
      state.nearestIndex = index;
      render();
    });
    document.querySelector("svg").addEventListener("click", event => {
      const rect = event.currentTarget.getBoundingClientRect();
      const x = (event.clientX - rect.left) * INITIAL.width / rect.width;
      const y = (event.clientY - rect.top) * INITIAL.height / rect.height;
      let best = null;
      let bestDistance = Infinity;
      for (const point of state.active) {
        const screen = pointToScreen(point);
        const distance = Math.hypot(screen.x - x, screen.y - y);
        if (distance < bestDistance) {
          bestDistance = distance;
          best = point;
        }
      }
      if (!best) return;
      state.nearestIndex = best.point_index;
      if (clickModeEl.value === "start") startEl.value = best.point_index;
      if (clickModeEl.value === "end") endEl.value = best.point_index;
      if (clickModeEl.value === "filter") {
        const setValue = parseIndexSet(filterEl.value);
        setValue.add(best.point_index);
        filterEl.value = setToText(setValue);
      }
      render();
    });

    providerEl.value = INITIAL.provider || "tianditu_vec";
    fitTo(POINTS);
    render();
  </script>
</div>
</body>
</html>
""")
    path.write_text(
        html_template.substitute(
            title=html_escape(title),
            width=width,
            height=height,
            zoom_value=zoom_value,
            point_count=len(points),
            total_distance=f"{total_distance:.3f}",
            route_distance=f"{route_distance:.3f}",
            start_index=start_index,
            end_index=end_index,
            points_json=points_json,
            initial_json=initial_json,
        ),
        encoding="utf-8",
    )


def browser_app_html() -> str:
    app = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <title>EPSILON 原始轨迹浏览器解析器</title>
<style>
    :root {
      --bg: #faf9f5;
      --surface: #fffdf8;
      --surface-2: #f7f3ea;
      --ink: #141413;
      --muted: #68645d;
      --line: #e8e6dc;
      --line-soft: rgba(20,20,19,.13);
      --accent: #d97757;
      --accent-deep: #a84d35;
      --accent-soft: #fff1eb;
      --blue: #6a9bcc;
      --blue-soft: #e9f2fb;
      --ease: cubic-bezier(.32,.72,0,1);
    }
    * { box-sizing: border-box; }
    [hidden] { display:none!important; }
    body {
      position:relative; isolation:isolate;
      margin:0; min-height:100dvh; overflow-x:hidden;
      color:var(--ink); background:var(--bg);
      font-family:"Satoshi","Plus Jakarta Sans","Aptos","Microsoft YaHei",ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
      -webkit-font-smoothing:antialiased; text-rendering:optimizeLegibility;
    }
    body::before {
      position:fixed; inset:-8%; z-index:0; content:""; pointer-events:none;
      background:radial-gradient(circle at 12% 14%,rgba(217,119,87,.12),transparent 28%),radial-gradient(circle at 84% 18%,rgba(106,155,204,.13),transparent 28%),linear-gradient(180deg,#faf9f5 0%,#f4efe7 54%,#e8e6dc 100%);
      background-size:120% 120%; animation:bgAurora 18s ease-in-out infinite alternate;
    }
    body::after {
      position:fixed; inset:0; z-index:1; content:""; pointer-events:none; opacity:.56;
      background-image:radial-gradient(circle at 1px 1px,rgba(35,30,24,.07) 1px,transparent 0),linear-gradient(rgba(35,30,24,.026) 1px,transparent 1px),linear-gradient(90deg,rgba(35,30,24,.024) 1px,transparent 1px);
      background-size:18px 18px,64px 64px,64px 64px;
      mask-image:linear-gradient(to bottom,#000 0%,#000 72%,transparent 100%);
      animation:gridDrift 28s linear infinite;
    }
    .page {
      position:relative; z-index:5; width:min(100%,1480px); margin:0 auto; padding:0;
    }
    ::selection { background:rgba(217,119,87,.16); color:var(--ink); }
    :focus-visible { outline:3px solid rgba(217,119,87,.32); outline-offset:3px; }
    header {
      padding:20px clamp(18px,2.6vw,44px); background:var(--surface); border-bottom:1px solid var(--line);
    }
    h1 { margin:0 0 6px; font-size:21px; color:var(--ink); font-weight:780; letter-spacing:-.03em; }
    h3 { margin:14px 0 8px; font-size:15px; color:var(--ink); font-weight:740; }
    .summary { display:flex; flex-wrap:wrap; gap:10px 20px; font-size:14px; color:var(--muted); line-height:1.6; }
    .layout { display:grid; grid-template-columns:minmax(0,1fr) 420px; gap:16px; padding:16px clamp(12px,2.2vw,36px) 24px; }
    .panel { background:var(--surface); border:1px solid var(--line); border-radius:16px; padding:14px; box-shadow:var(--soft-shadow,0 14px 38px rgba(20,20,19,.07)); }
    .controls { display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-bottom:10px; }
    .controls .wide { grid-column:1/-1; }
    label { display:flex; flex-direction:column; gap:4px; font-size:12px; color:var(--muted); font-weight:620; }
    input, select, button { border-radius:10px; border:1px solid var(--line); background:var(--surface); color:var(--ink); padding:8px 10px; font:inherit; }
    input:focus, select:focus { outline:none; border-color:var(--accent); box-shadow:0 0 0 3px rgba(217,119,87,.18); }
    input[type=file] { padding:7px 9px; }
    button { cursor:pointer; background:var(--surface-2); border-color:var(--line); transition:transform .32s var(--ease),box-shadow .32s var(--ease),border-color .32s var(--ease),background .32s var(--ease); }
    button:hover { background:var(--line); border-color:var(--muted); }
    button:active { transform:translateY(1px); }
    button.primary { background:linear-gradient(180deg,#e28767,#a84d35); border-color:rgba(168,77,53,.68); color:#fff; font-weight:720; box-shadow:0 8px 22px rgba(217,119,87,.18); }
    button.primary:hover { box-shadow:0 12px 28px rgba(217,119,87,.24); }
    .button-row { display:flex; flex-wrap:wrap; gap:8px; }
    .hint { color:var(--muted); font-size:12px; line-height:1.55; }
    .metric { margin:10px 0; padding:12px; border-radius:12px; background:var(--surface-2); border:1px solid var(--line); line-height:1.8; font-size:13px; }
    .metric strong { color:var(--accent); font-weight:780; }
    .error { color:var(--accent-deep); font-weight:600; }
    .ok { color:#16a34a; font-weight:600; }
    #map { position:relative; width:100%; height:680px; overflow:hidden; background:var(--surface-2); border:1px solid var(--line); border-radius:14px; box-shadow:var(--soft-shadow,0 14px 38px rgba(20,20,19,.07)); }
    .tile { position:absolute; width:256px; height:256px; user-select:none; pointer-events:none; }
    svg { position:absolute; inset:0; width:100%; height:100%; overflow:hidden; cursor:crosshair; }
    .route-all { fill:none; stroke:rgba(104,98,93,.42); stroke-width:4; stroke-linejoin:round; stroke-linecap:round; }
    .route-selected { fill:none; stroke:var(--accent); stroke-width:5; stroke-linejoin:round; stroke-linecap:round; filter:drop-shadow(0 2px 4px rgba(217,119,87,.2)); }
    .point { fill:var(--blue); stroke:#fff; stroke-width:1.5; opacity:.80; }
    .start { fill:#16a34a; stroke:#fff; stroke-width:2; filter:drop-shadow(0 2px 4px rgba(22,163,74,.24)); }
    .end { fill:var(--accent-deep); stroke:#fff; stroke-width:2; filter:drop-shadow(0 2px 4px rgba(168,77,53,.24)); }
    .nearest { fill:#f59e0b; stroke:#fff; stroke-width:2; filter:drop-shadow(0 2px 4px rgba(245,158,11,.24)); }
    .zoom-ctrl { position:absolute; right:12px; top:12px; display:flex; flex-direction:column; align-items:center; gap:2px; z-index:10; }
    .zoom-ctrl button { width:34px; height:34px; border-radius:10px; border:1px solid var(--line); background:var(--surface); color:var(--muted); font-size:18px; line-height:1; cursor:pointer; display:flex; align-items:center; justify-content:center; box-shadow:0 2px 8px rgba(20,20,19,.08); padding:0; }
    .zoom-ctrl button:hover { background:var(--surface-2); border-color:var(--accent); color:var(--accent); }
    .zoom-ctrl .zoom-level { font-size:11px; color:var(--muted); font-weight:700; line-height:1.8; }
    .attrib { position:absolute; left:8px; bottom:6px; font-size:12px; background:rgba(255,253,248,.92); color:var(--muted); padding:3px 8px; border-radius:8px; border:1px solid var(--line); }
    table { width:100%; border-collapse:collapse; font-size:12px; }
    th, td { padding:6px 5px; border-bottom:1px solid var(--line); text-align:left; white-space:nowrap; }
    th { color:var(--muted); font-weight:660; position:sticky; top:0; background:var(--surface-2); }
    tbody tr:hover { background:var(--accent-soft); }
	    .table-wrap { max-height:350px; overflow:auto; border:1px solid var(--line); border-radius:10px; }
	    .small-button { padding:4px 6px; font-size:11px; border-radius:8px; background:var(--surface-2); border-color:var(--line); }
	    .small-button:hover { background:var(--line); }
	.loading-overlay {
	  position:absolute; inset:0; z-index:20; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:16px;
	  background:rgba(250,249,245,.78); backdrop-filter:blur(4px); -webkit-backdrop-filter:blur(4px);
	  border-radius:14px;
	  opacity:0; pointer-events:none; transition:opacity .3s var(--ease);
	}
	.loading-overlay.is-active { opacity:1; pointer-events:auto; }
	.loading-spinner {
	  width:36px; height:36px; border-radius:50%;
	  border:3px solid var(--line); border-top-color:var(--accent);
	  animation:spin .7s linear infinite;
	}
	.loading-overlay p { margin:0; color:var(--muted); font-size:14px; font-weight:620; }
	@keyframes spin { to { transform:rotate(360deg); } }
    @keyframes bgAurora {
      0%{transform:translate3d(-1.4%,0,0) scale(1);background-position:0% 0%}
      50%{transform:translate3d(1.2%,-1%,0) scale(1.025);background-position:50% 32%}
      100%{transform:translate3d(0,1.2%,0) scale(1.04);background-position:100% 60%}
    }
    @keyframes gridDrift {
      from{background-position:0 0,0 0,0 0}
      to{background-position:18px 18px,64px 64px,64px 64px}
    }
    @media (max-width:1180px) { .layout { grid-template-columns:1fr; } }
    @media (prefers-reduced-motion:reduce) {
      body::before, body::after { animation:none!important; }
    }
  </style>
</head>
<body>
<div class="ambient-bg" aria-hidden="true"><span></span><span></span><span></span></div>
<div class="page">
  <header>
    <h1>EPSILON 原始轨迹浏览器解析器</h1>
    <div class="summary">
      <span>无参数运行脚本会打开本页</span>
      <span>可直接选择 session 文件夹或 epsilon.dat</span>
      <span>默认底图: 天地图矢量</span>
    </div>
  </header>
  <main class="layout">
    <section class="panel">
<div id="map">
<div class="loading-overlay" id="loadingOverlay"><div class="loading-spinner"></div><p>正在解析轨迹数据&#x2026;</p></div>
	        <div id="tiles"></div>
	        <div class="zoom-ctrl">
	          <button id="zoomInBtn" title="放大">+</button>
	          <span class="zoom-level" id="zoomLabel">16</span>
	          <button id="zoomOutBtn" title="缩小">&minus;</button>
	        </div>
	        <svg id="mapSvg" viewBox="0 0 1100 680" aria-label="EPSILON route">
          <polyline id="routeAll" class="route-all" points="" />
          <polyline id="routeSelected" class="route-selected" points="" />
          <g id="markers"></g>
        </svg>
        <div id="attrib" class="attrib">天地图</div>
      </div>
      <p class="hint">先在右侧选择 session 文件夹或 EPSILON DAT 文件。地图点选会选择距离鼠标最近的当前可见轨迹点。</p>
    </section>
    <aside class="panel">
      <div class="controls">
        <label class="wide">选择 session 文件夹（推荐）
          <input id="sessionInput" type="file" webkitdirectory multiple />
        </label>
        <label class="wide">或直接选择 DAT 文件
          <input id="datInput" type="file" accept=".dat,application/octet-stream" />
        </label>
        <label>轨迹点来源
          <select id="positionSource">
            <option value="auto">auto：优先 GEODETIC_POS</option>
            <option value="geodetic">只用 0x5C GEODETIC_POS</option>
            <option value="system">只用 0x50 SYS_STATE</option>
            <option value="both">两类坐标都用</option>
          </select>
        </label>
        <label>底图
          <select id="provider">
            <option value="tianditu_vec">天地图矢量</option>
            <option value="tianditu_img">天地图卫星</option>
            <option value="osm">OpenStreetMap</option>
          </select>
        </label>
        <label>点选动作
          <select id="clickMode">
            <option value="start">点地图设为起点</option>
            <option value="end">点地图设为终点</option>
            <option value="filter">点地图加入过滤</option>
          </select>
        </label>
        <label>起点 point_index
          <input id="startIndex" type="number" min="1" step="1" />
        </label>
        <label>终点 point_index
          <input id="endIndex" type="number" min="1" step="1" />
        </label>
        <label class="wide">过滤点 point_index，支持逗号和范围，如 12,20-25
          <input id="filterIndices" placeholder="例如：12,20-25" />
        </label>
        <label class="wide">天地图 Key
          <input id="tiandituKey" value="__TIANDITU_KEY__" />
        </label>
      </div>
      <div class="button-row">
        <button id="applyButton" class="primary">应用过滤并重算</button>
        <button id="fitButton">适应当前轨迹</button>
        <button id="resetButton">重置过滤</button>
        <button id="exportCsvButton">导出当前 CSV</button>
      </div>
      <div id="status" class="metric">等待选择数据。</div>
      <div id="metrics" class="metric"></div>
      <div class="hint">路线距离按过滤后的轨迹顺序逐段累加，不是起终点直线距离，也不是道路导航距离。</div>
      <h3>当前轨迹点</h3>
      <div class="table-wrap">
        <table>
          <thead><tr><th>#</th><th>经纬度</th><th>fix</th><th>操作</th></tr></thead>
          <tbody id="pointTable"></tbody>
        </table>
      </div>
      <p id="tableNote" class="hint"></p>
    </aside>
  </main>
  <script>
    const DEFAULT_KEY = "__TIANDITU_KEY__";
    const TILE_SIZE = 256;
    const MAX_TABLE_ROWS = 500;
    const EARTH_RADIUS_METERS = 6371000.0;
    const FRAME_HEAD = 0xFC;
    const FRAME_TAIL = 0xFD;
    const RAW_SOURCE_EPSILON = 1;
    const UNIFIED_RAW_RECORD_MARKER = 0x44525756;
    const LEGACY_EPSILON_RECORD_MARKER = 0x524D5549;
    const FIX_NAMES = {
      0: "NO_GPS", 1: "NO_FIX", 2: "2D", 3: "3D", 4: "DGPS",
      5: "RTK_FLOAT", 6: "RTK_FIXED", 7: "STATIC", 8: "PPP", 9: "RTK_DUAL"
    };
    const PACKET_NAMES = {
      0x40: "IMU", 0x41: "AHRS", 0x42: "INS_GPS", 0x50: "SYS_STATE",
      0x51: "UNIX_TIME", 0x52: "FORMATTED_TIME", 0x59: "RAW_GNSS",
      0x5A: "SATELLITES", 0x5C: "GEODETIC_POS", 0x5D: "ECEF_POS"
    };
    const CSV_FIELDS = [
      "point_index","source_point_index","source_file","raw_format","record_sequence",
      "host_timestamp_us","host_time_utc","packet_id_hex","packet_name","position_source",
      "serial_number","utc_unix_s","utc_microseconds","epsilon_time_utc","latitude_deg",
      "longitude_deg","height_m","segment_distance_m","cumulative_distance_m","gnss_fix_code",
      "gnss_fix","gnss_satellites","hdop","vdop","hacc_m","vacc_m","lat_std_m","lon_std_m",
      "height_std_m","diff_age_s","vel_n_mps","vel_e_mps","vel_d_mps","ned_n_m","ned_e_m",
      "ned_d_m","roll_deg","pitch_deg","yaw_deg","system_status_bits","filter_status_bits",
      "update_status_bits","record_offset","payload_offset"
    ];

    const tilesEl = document.getElementById("tiles");
    const svgEl = document.getElementById("mapSvg");
    const routeAllEl = document.getElementById("routeAll");
    const routeSelectedEl = document.getElementById("routeSelected");
    const markersEl = document.getElementById("markers");
    const providerEl = document.getElementById("provider");
    const clickModeEl = document.getElementById("clickMode");
    const startEl = document.getElementById("startIndex");
    const endEl = document.getElementById("endIndex");
    const filterEl = document.getElementById("filterIndices");
    const keyEl = document.getElementById("tiandituKey");
    const metricsEl = document.getElementById("metrics");
    const statusEl = document.getElementById("status");
    const tableEl = document.getElementById("pointTable");
    const tableNoteEl = document.getElementById("tableNote");
    const attribEl = document.getElementById("attrib");
    const positionSourceEl = document.getElementById("positionSource");
    let points = [];
    let stats = {};
    let state = {
      zoom: 16,
      minZoom: 1,
      maxZoom: 18,
      centerWorld: null,
      excluded: new Set(),
      active: [],
      nearestIndex: null
    };

    function setStatus(message, ok = true) {
      statusEl.innerHTML = "<span class='" + (ok ? "ok" : "error") + "'>" + escapeHtml(message) + "</span>";
    }

    function escapeHtml(text) {
      return String(text).replace(/[&<>"']/g, ch => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[ch]));
    }

    function readU16LE(data, offset) {
      return data.getUint16(offset, true);
    }

    function readU32LE(data, offset) {
      return data.getUint32(offset, true);
    }

    function readI64LE(data, offset) {
      return Number(data.getBigInt64(offset, true));
    }

    function readFloatLE(data, offset) {
      return data.getFloat32(offset, true);
    }

    function readDoubleLE(data, offset) {
      return data.getFloat64(offset, true);
    }

    function radToDeg(value) {
      return value * 180.0 / Math.PI;
    }

    function crc8(bytes, start, length) {
      let crc = 0;
      for (let i = 0; i < length; ++i) {
        crc ^= bytes[start + i];
        for (let bit = 0; bit < 8; ++bit) {
          crc = (crc & 0x01) ? ((crc >> 1) ^ 0x8C) : (crc >> 1);
          crc &= 0xFF;
        }
      }
      return crc;
    }

    function crc16(bytes, start, length) {
      let crc = 0;
      for (let i = 0; i < length; ++i) {
        crc ^= bytes[start + i] << 8;
        for (let bit = 0; bit < 8; ++bit) {
          crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
          crc &= 0xFFFF;
        }
      }
      return crc;
    }

    function parseFrame(payload) {
      if (payload.byteLength < 8) return null;
      const bytes = new Uint8Array(payload);
      const packetId = bytes[1];
      const payloadSize = bytes[2];
      const expectedSize = payloadSize + 8;
      if (bytes[0] !== FRAME_HEAD || payload.byteLength !== expectedSize || bytes[payload.byteLength - 1] !== FRAME_TAIL) {
        return null;
      }
      if (crc8(bytes, 0, 4) !== bytes[4]) return null;
      const receivedCrc16 = (bytes[5] << 8) | bytes[6];
      if (crc16(bytes, 7, payloadSize) !== receivedCrc16) return null;
      return {
        packetId,
        serialNumber: bytes[3],
        payloadSize,
        payloadView: new DataView(payload.buffer, payload.byteOffset + 7, payloadSize)
      };
    }

    function packetName(packetId) {
      return "0x" + packetId.toString(16).toUpperCase().padStart(2, "0") + " " + (PACKET_NAMES[packetId] || "UNKNOWN");
    }

    function timestampUsToIso(timestampUs) {
      if (!timestampUs) return "";
      const ms = Math.floor(timestampUs / 1000);
      const extraMicros = timestampUs % 1000;
      return new Date(ms).toISOString().replace("Z", String(extraMicros).padStart(3, "0") + "Z");
    }

    function epsilonTimeToIso(utcUnixS, utcMicros) {
      if (utcUnixS === null || utcUnixS === undefined) return "";
      return timestampUsToIso(utcUnixS * 1000000 + (utcMicros || 0));
    }

    function validLatLon(lat, lon) {
      return Number.isFinite(lat) && Number.isFinite(lon) &&
        lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180 &&
        !(Math.abs(lat) < 1e-8 && Math.abs(lon) < 1e-8);
    }

    function iterUnifiedRaw(fileName, bytes) {
      const data = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      if (bytes.byteLength < 20) throw new Error(fileName + " 文件头过短");
      const version = readU32LE(data, 8);
      const headerSize = readU32LE(data, 12);
      const sourceId = readU16LE(data, 16);
      if (sourceId !== RAW_SOURCE_EPSILON) throw new Error(fileName + " source_id 不是 EPSILON");
      let offset = headerSize;
      const records = [];
      while (offset + 36 <= bytes.byteLength) {
        const marker = readU32LE(data, offset);
        const headerSizeRecord = readU32LE(data, offset + 4);
        if (marker !== UNIFIED_RAW_RECORD_MARKER) throw new Error(fileName + " raw record marker 错误，offset=" + offset);
        const hostTimestampUs = Number(data.getBigUint64(offset + 8, true));
        const payloadSize = readU32LE(data, offset + 16);
        const recordSourceId = readU16LE(data, offset + 20);
        const recordType = readU16LE(data, offset + 22);
        const flags = readU32LE(data, offset + 24);
        const sequence = Number(data.getBigUint64(offset + 28, true));
        const payloadOffset = offset + headerSizeRecord;
        if (payloadOffset + payloadSize > bytes.byteLength) throw new Error(fileName + " payload 不完整");
        if (recordSourceId === RAW_SOURCE_EPSILON) {
          records.push({
            sourceFile: fileName,
            rawFormat: "unified_v" + version,
            sequence,
            hostTimestampUs,
            recordType,
            flags,
            payload: bytes.slice(payloadOffset, payloadOffset + payloadSize),
            recordOffset: offset,
            payloadOffset
          });
        }
        offset = payloadOffset + payloadSize;
      }
      return records;
    }

    function iterLegacyRaw(fileName, bytes) {
      const data = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      const version = readU32LE(data, 8);
      const headerSize = readU32LE(data, 12);
      let offset = headerSize;
      let sequence = 0;
      const records = [];
      while (offset + 20 <= bytes.byteLength) {
        const marker = readU32LE(data, offset);
        if (marker !== LEGACY_EPSILON_RECORD_MARKER) throw new Error(fileName + " legacy marker 错误，offset=" + offset);
        const payloadSize = readU32LE(data, offset + 4);
        const hostTimestampUs = Number(data.getBigUint64(offset + 8, true));
        const frameTag = bytes[offset + 16];
        const serial = bytes[offset + 17];
        const payloadOffset = offset + 20;
        if (payloadOffset + payloadSize > bytes.byteLength) throw new Error(fileName + " payload 不完整");
        records.push({
          sourceFile: fileName,
          rawFormat: "legacy_epsilon_v" + version,
          sequence,
          hostTimestampUs,
          recordType: frameTag,
          flags: serial,
          payload: bytes.slice(payloadOffset, payloadOffset + payloadSize),
          recordOffset: offset,
          payloadOffset
        });
        offset = payloadOffset + payloadSize;
        sequence += 1;
      }
      return records;
    }

    function iterFdilinkStream(fileName, bytes) {
      const records = [];
      let offset = 0;
      let sequence = 0;
      while (offset + 8 <= bytes.byteLength) {
        while (offset < bytes.byteLength && bytes[offset] !== FRAME_HEAD) offset++;
        if (offset + 8 > bytes.byteLength) break;
        const payloadSize = bytes[offset + 2];
        const frameSize = payloadSize + 8;
        if (offset + frameSize > bytes.byteLength) break;
        const payload = bytes.slice(offset, offset + frameSize);
        if (payload[payload.byteLength - 1] === FRAME_TAIL) {
          records.push({
            sourceFile: fileName,
            rawFormat: "fdilink_stream",
            sequence,
            hostTimestampUs: 0,
            recordType: bytes[offset + 1],
            flags: bytes[offset + 3],
            payload,
            recordOffset: offset,
            payloadOffset: offset
          });
          sequence += 1;
          offset += frameSize;
        } else {
          offset += 1;
        }
      }
      return records;
    }

    function parseRecordsFromBytes(fileName, bytes) {
      const magic = new TextDecoder("ascii").decode(bytes.slice(0, 8));
      if (magic === "VVRAWDAT") return iterUnifiedRaw(fileName, bytes);
      if (magic === "VVEPSRAW") return iterLegacyRaw(fileName, bytes);
      return iterFdilinkStream(fileName, bytes);
    }

    function newPoint(record, frame, stateSnapshot, source) {
      return {
        point_index: 0,
        source_point_index: 0,
        source_file: record.sourceFile,
        raw_format: record.rawFormat,
        record_sequence: record.sequence,
        host_timestamp_us: record.hostTimestampUs || "",
        host_time_utc: timestampUsToIso(record.hostTimestampUs),
        packet_id_hex: "0x" + frame.packetId.toString(16).toUpperCase().padStart(2, "0"),
        packet_name: packetName(frame.packetId),
        position_source: source,
        serial_number: frame.serialNumber,
        utc_unix_s: stateSnapshot.utc_unix_s ?? "",
        utc_microseconds: stateSnapshot.utc_microseconds ?? "",
        epsilon_time_utc: epsilonTimeToIso(stateSnapshot.utc_unix_s, stateSnapshot.utc_microseconds),
        latitude_deg: null,
        longitude_deg: null,
        height_m: null,
        segment_distance_m: 0,
        cumulative_distance_m: 0,
        gnss_fix_code: stateSnapshot.gnss_fix_code ?? "",
        gnss_fix: stateSnapshot.gnss_fix_code === null || stateSnapshot.gnss_fix_code === undefined ? "" : (FIX_NAMES[stateSnapshot.gnss_fix_code] || "UNKNOWN"),
        gnss_satellites: stateSnapshot.gnss_satellites ?? "",
        hdop: stateSnapshot.hdop ?? "",
        vdop: stateSnapshot.vdop ?? "",
        hacc_m: stateSnapshot.hacc_m ?? "",
        vacc_m: stateSnapshot.vacc_m ?? "",
        lat_std_m: stateSnapshot.lat_std_m ?? "",
        lon_std_m: stateSnapshot.lon_std_m ?? "",
        height_std_m: stateSnapshot.height_std_m ?? "",
        diff_age_s: stateSnapshot.diff_age_s ?? "",
        vel_n_mps: stateSnapshot.vel_n_mps ?? "",
        vel_e_mps: stateSnapshot.vel_e_mps ?? "",
        vel_d_mps: stateSnapshot.vel_d_mps ?? "",
        ned_n_m: stateSnapshot.ned_n_m ?? "",
        ned_e_m: stateSnapshot.ned_e_m ?? "",
        ned_d_m: stateSnapshot.ned_d_m ?? "",
        roll_deg: stateSnapshot.roll_deg ?? "",
        pitch_deg: stateSnapshot.pitch_deg ?? "",
        yaw_deg: stateSnapshot.yaw_deg ?? "",
        system_status_bits: stateSnapshot.system_status_bits ?? "",
        filter_status_bits: stateSnapshot.filter_status_bits ?? "",
        update_status_bits: stateSnapshot.update_status_bits ?? "",
        record_offset: record.recordOffset,
        payload_offset: record.payloadOffset
      };
    }

    function decodePoints(records, requestedSource) {
      const stateSnapshot = {};
      const candidates = [];
      let validFrames = 0;
      let badFrames = 0;
      for (const record of records) {
        const frame = parseFrame(record.payload);
        if (!frame) {
          badFrames += 1;
          continue;
        }
        validFrames += 1;
        const p = frame.payloadView;
        const packetId = frame.packetId;
        const size = frame.payloadSize;
        if (packetId === 0x42 && size >= 72) {
          stateSnapshot.ned_n_m = readFloatLE(p, 24);
          stateSnapshot.ned_e_m = readFloatLE(p, 28);
          stateSnapshot.ned_d_m = readFloatLE(p, 32);
          stateSnapshot.vel_n_mps = readFloatLE(p, 36);
          stateSnapshot.vel_e_mps = readFloatLE(p, 40);
          stateSnapshot.vel_d_mps = readFloatLE(p, 44);
        } else if (packetId === 0x50 && size >= 14) {
          stateSnapshot.system_status_bits = readU16LE(p, 0);
          stateSnapshot.filter_status_bits = readU16LE(p, 2);
          stateSnapshot.update_status_bits = readU16LE(p, 4);
          stateSnapshot.utc_unix_s = readU32LE(p, 6);
          stateSnapshot.utc_microseconds = readU32LE(p, 10);
          stateSnapshot.gnss_fix_code = (stateSnapshot.filter_status_bits >> 4) & 0x0F;
          if (size >= 50) {
            stateSnapshot.vel_n_mps = readFloatLE(p, 38);
            stateSnapshot.vel_e_mps = readFloatLE(p, 42);
            stateSnapshot.vel_d_mps = readFloatLE(p, 46);
          }
          if (size >= 78) {
            stateSnapshot.roll_deg = radToDeg(readFloatLE(p, 66));
            stateSnapshot.pitch_deg = radToDeg(readFloatLE(p, 70));
            stateSnapshot.yaw_deg = radToDeg(readFloatLE(p, 74));
          }
          if (size >= 102) {
            stateSnapshot.lat_std_m = readFloatLE(p, 90);
            stateSnapshot.lon_std_m = readFloatLE(p, 94);
            stateSnapshot.height_std_m = readFloatLE(p, 98);
          }
          if (size >= 38) {
            const point = newPoint(record, frame, stateSnapshot, "system_state");
            point.latitude_deg = radToDeg(readDoubleLE(p, 14));
            point.longitude_deg = radToDeg(readDoubleLE(p, 22));
            point.height_m = readDoubleLE(p, 30);
            candidates.push(point);
          }
        } else if (packetId === 0x51 && size >= 8) {
          stateSnapshot.utc_unix_s = readU32LE(p, 0);
          stateSnapshot.utc_microseconds = readU32LE(p, 4);
        } else if (packetId === 0x59 && size >= 74) {
          stateSnapshot.utc_unix_s = readU32LE(p, 0);
          stateSnapshot.utc_microseconds = readU32LE(p, 4);
          stateSnapshot.lat_std_m = readFloatLE(p, 44);
          stateSnapshot.lon_std_m = readFloatLE(p, 48);
          stateSnapshot.height_std_m = readFloatLE(p, 52);
          stateSnapshot.diff_age_s = readFloatLE(p, 64);
          const rawGnssStatus = readU16LE(p, 72);
          stateSnapshot.gnss_fix_code = rawGnssStatus & 0x0F;
        } else if (packetId === 0x5A && size >= 9) {
          stateSnapshot.hdop = readFloatLE(p, 0);
          stateSnapshot.vdop = readFloatLE(p, 4);
          stateSnapshot.gnss_satellites = new Uint8Array(p.buffer, p.byteOffset, p.byteLength)[8];
        } else if (packetId === 0x5C && size >= 32) {
          const point = newPoint(record, frame, stateSnapshot, "geodetic_pos");
          point.latitude_deg = radToDeg(readDoubleLE(p, 0));
          point.longitude_deg = radToDeg(readDoubleLE(p, 8));
          point.height_m = readDoubleLE(p, 16);
          point.hacc_m = readFloatLE(p, 24);
          point.vacc_m = readFloatLE(p, 28);
          stateSnapshot.hacc_m = point.hacc_m;
          stateSnapshot.vacc_m = point.vacc_m;
          candidates.push(point);
        }
      }
      let source = requestedSource;
      if (source === "auto") {
        source = candidates.some(point => point.position_source === "geodetic_pos") ? "geodetic" : "system";
      }
      let selected = candidates;
      if (source === "geodetic") selected = candidates.filter(point => point.position_source === "geodetic_pos");
      if (source === "system") selected = candidates.filter(point => point.position_source === "system_state");
      selected = selected.filter(point => validLatLon(point.latitude_deg, point.longitude_deg));
      selected.forEach((point, index) => {
        point.source_point_index = index + 1;
        point.point_index = index + 1;
      });
      assignDistances(selected);
      stats = { rawRecords: records.length, validFrames, badFrames, candidates: candidates.length, source };
      return selected;
    }

    function assignDistances(list) {
      let cumulative = 0;
      for (let i = 0; i < list.length; ++i) {
        const point = list[i];
        point.point_index = i + 1;
        point.segment_distance_m = i === 0 ? 0 : haversine(list[i - 1], point);
        cumulative += point.segment_distance_m;
        point.cumulative_distance_m = cumulative;
      }
    }

    async function readDatFile(file) {
      const bytes = new Uint8Array(await file.arrayBuffer());
      return parseRecordsFromBytes(file.name, bytes);
    }

    function chooseSessionEpsilonFile(files) {
      const list = Array.from(files);
      const preferred = [
        file => file.webkitRelativePath.replaceAll("\\", "/").endsWith("/raw/epsilon.dat"),
        file => file.webkitRelativePath.replaceAll("\\", "/").endsWith("/sensors/epsilon_raw.dat"),
        file => file.name === "epsilon.dat",
        file => file.name === "epsilon_raw.dat",
      ];
      for (const predicate of preferred) {
        const found = list.find(predicate);
        if (found) return found;
      }
      return null;
    }

    async function loadFromFile(file) {
      if (!file) return;
      document.getElementById("loadingOverlay").classList.add("is-active");
      setStatus("正在读取 " + file.name + " ...");
      try {
        const records = await readDatFile(file);
        points = decodePoints(records, positionSourceEl.value);
        if (!points.length) throw new Error("没有解析到有效经纬度轨迹点");
        startEl.value = 1;
        endEl.value = points.length;
        filterEl.value = "";
        state.excluded = new Set();
        state.nearestIndex = null;
        fitTo(points);
        render();
        setStatus("已解析 " + file.name + "：点数 " + points.length + "，raw records " + stats.rawRecords + "，valid frames " + stats.validFrames);
      } catch (error) {
        points = [];
        state.active = [];
        render();
        setStatus(error.message || String(error), false);
      } finally {
        document.getElementById("loadingOverlay").classList.remove("is-active");
      }
    }

    document.getElementById("sessionInput").addEventListener("change", event => {
      const file = chooseSessionEpsilonFile(event.target.files);
      if (!file) {
        setStatus("所选文件夹中没有找到 raw/epsilon.dat 或 sensors/epsilon_raw.dat", false);
        return;
      }
      loadFromFile(file);
    });
    document.getElementById("datInput").addEventListener("change", event => {
      loadFromFile(event.target.files[0]);
    });
    positionSourceEl.addEventListener("change", async () => {
      const dat = document.getElementById("datInput").files[0];
      const sessionFile = chooseSessionEpsilonFile(document.getElementById("sessionInput").files);
      await loadFromFile(dat || sessionFile);
    });

    function finiteNumber(value) {
      return typeof value === "number" && Number.isFinite(value);
    }

    function fmt(value, digits) {
      return finiteNumber(value) ? value.toFixed(digits) : "";
    }

    function clampLatitude(latitude) {
      return Math.max(-85.05112878, Math.min(85.05112878, latitude));
    }

    function worldSize(zoom) {
      return TILE_SIZE * Math.pow(2, zoom);
    }

    function latLonToWorld(latitude, longitude, zoom) {
      const lat = clampLatitude(latitude) * Math.PI / 180.0;
      const size = worldSize(zoom);
      const x = (longitude + 180.0) / 360.0 * size;
      const y = (0.5 - Math.log((1.0 + Math.sin(lat)) / (1.0 - Math.sin(lat))) / (4.0 * Math.PI)) * size;
      return { x, y };
    }

    function mapSize() {
      const rect = svgEl.getBoundingClientRect();
      return { width: Math.max(1, rect.width), height: Math.max(1, rect.height) };
    }

    function updateViewBox() {
      const size = mapSize();
      svgEl.setAttribute("viewBox", "0 0 " + size.width + " " + size.height);
      return size;
    }

    function pointToScreen(point) {
      const size = mapSize();
      const world = latLonToWorld(point.latitude_deg, point.longitude_deg, state.zoom);
      return {
        x: world.x - state.centerWorld.x + size.width / 2,
        y: world.y - state.centerWorld.y + size.height / 2
      };
    }

    function haversine(a, b) {
      const lat1 = a.latitude_deg * Math.PI / 180.0;
      const lat2 = b.latitude_deg * Math.PI / 180.0;
      const dLat = lat2 - lat1;
      const dLon = (b.longitude_deg - a.longitude_deg) * Math.PI / 180.0;
      const h = Math.sin(dLat / 2) ** 2 + Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLon / 2) ** 2;
      return EARTH_RADIUS_METERS * 2 * Math.atan2(Math.sqrt(h), Math.sqrt(Math.max(0, 1 - h)));
    }

    function parseIndexSet(text) {
      const result = new Set();
      for (const rawPart of text.split(",")) {
        const part = rawPart.trim();
        if (!part) continue;
        if (part.includes("-")) {
          const pieces = part.split("-", 2);
          let start = Number.parseInt(pieces[0].trim(), 10);
          let end = Number.parseInt(pieces[1].trim(), 10);
          if (!Number.isFinite(start) || !Number.isFinite(end)) continue;
          if (end < start) [start, end] = [end, start];
          for (let i = start; i <= end; ++i) result.add(i);
        } else {
          const value = Number.parseInt(part, 10);
          if (Number.isFinite(value)) result.add(value);
        }
      }
      return result;
    }

    function setToText(setValue) {
      return Array.from(setValue).sort((a, b) => a - b).join(",");
    }

    function currentActivePoints() {
      return points.filter(point => !state.excluded.has(point.point_index));
    }

    function chooseZoomFor(list) {
      const size = mapSize();
      if (list.length <= 1) return Math.min(state.maxZoom, 17);
      for (let zoom = state.maxZoom; zoom >= state.minZoom; --zoom) {
        const pixels = list.map(point => latLonToWorld(point.latitude_deg, point.longitude_deg, zoom));
        const xs = pixels.map(pixel => pixel.x);
        const ys = pixels.map(pixel => pixel.y);
        if (Math.max(...xs) - Math.min(...xs) <= Math.max(64, size.width - 120) &&
            Math.max(...ys) - Math.min(...ys) <= Math.max(64, size.height - 120)) {
          return zoom;
        }
      }
      return state.minZoom;
    }

    function fitTo(list) {
      if (!list.length) {
        state.centerWorld = latLonToWorld(31.2304, 121.4737, state.zoom);
        return;
      }
      state.zoom = chooseZoomFor(list);
      const minLat = Math.min(...list.map(point => point.latitude_deg));
      const maxLat = Math.max(...list.map(point => point.latitude_deg));
      const minLon = Math.min(...list.map(point => point.longitude_deg));
      const maxLon = Math.max(...list.map(point => point.longitude_deg));
      state.centerWorld = latLonToWorld((minLat + maxLat) / 2, (minLon + maxLon) / 2, state.zoom);
    }

    function providerLayers(provider) {
      if (provider === "tianditu_img") return [{ endpoint: "img_w", layer: "img", suffix: "img" }, { endpoint: "cia_w", layer: "cia", suffix: "cia" }];
      if (provider === "tianditu_vec") return [{ endpoint: "vec_w", layer: "vec", suffix: "vec" }, { endpoint: "cva_w", layer: "cva", suffix: "cva" }];
      return [{ endpoint: "", layer: "", suffix: "osm" }];
    }

    function tileUrl(provider, zoom, tileX, tileY, layer) {
      const worldTiles = Math.pow(2, zoom);
      const wrappedX = ((tileX % worldTiles) + worldTiles) % worldTiles;
      if (provider === "osm") return "https://tile.openstreetmap.org/" + zoom + "/" + wrappedX + "/" + tileY + ".png";
      const key = encodeURIComponent(keyEl.value.trim());
      const shardSeed = Math.abs(tileX * 31 + tileY * 17 + layer.suffix.length * 13) % 8;
      return "https://t" + shardSeed + ".tianditu.gov.cn/" + layer.endpoint + "/wmts?SERVICE=WMTS&REQUEST=GetTile&VERSION=1.0.0&LAYER=" + layer.layer +
        "&STYLE=default&TILEMATRIXSET=w&FORMAT=tiles&TILECOL=" + wrappedX + "&TILEROW=" + tileY + "&TILEMATRIX=" + zoom + "&tk=" + key;
    }

    function renderTiles() {
      tilesEl.replaceChildren();
      if (!state.centerWorld) fitTo(points);
      const size = mapSize();
      const provider = providerEl.value;
      const layers = providerLayers(provider);
      const center = state.centerWorld;
      const topLeftX = center.x - size.width / 2;
      const topLeftY = center.y - size.height / 2;
      const worldTiles = Math.pow(2, state.zoom);
      const minTileX = Math.floor(topLeftX / TILE_SIZE);
      const maxTileX = Math.floor((topLeftX + size.width) / TILE_SIZE);
      const minTileY = Math.max(0, Math.floor(topLeftY / TILE_SIZE));
      const maxTileY = Math.min(worldTiles - 1, Math.floor((topLeftY + size.height) / TILE_SIZE));
      for (let tileX = minTileX; tileX <= maxTileX; ++tileX) {
        for (let tileY = minTileY; tileY <= maxTileY; ++tileY) {
          for (const layer of layers) {
            const img = document.createElement("img");
            img.className = "tile";
            img.src = tileUrl(provider, state.zoom, tileX, tileY, layer);
            img.alt = "";
            img.style.left = (tileX * TILE_SIZE - topLeftX).toFixed(2) + "px";
            img.style.top = (tileY * TILE_SIZE - topLeftY).toFixed(2) + "px";
            tilesEl.appendChild(img);
          }
        }
      }
      attribEl.textContent = provider === "osm" ? "© OpenStreetMap contributors" : "底图数据 © 天地图";
    }

    function polyline(list) {
      return list.map(point => {
        const screen = pointToScreen(point);
        return screen.x.toFixed(2) + "," + screen.y.toFixed(2);
      }).join(" ");
    }

    function routeInfo(active, startIndex, endIndex) {
      const startPos = active.findIndex(point => point.point_index === startIndex);
      const endPos = active.findIndex(point => point.point_index === endIndex);
      if (startPos < 0 || endPos < 0) return { ok: false, message: "起点或终点已被过滤，或点号不存在。" };
      const lo = Math.min(startPos, endPos);
      const hi = Math.max(startPos, endPos);
      let distance = 0;
      for (let i = lo + 1; i <= hi; ++i) distance += haversine(active[i - 1], active[i]);
      let total = 0;
      for (let i = 1; i < active.length; ++i) total += haversine(active[i - 1], active[i]);
      return { ok: true, distance, total, segment: active.slice(lo, hi + 1), start: active[startPos], end: active[endPos] };
    }

    function addCircle(point, className, radius, titleText) {
      const screen = pointToScreen(point);
      const circle = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      circle.setAttribute("class", className);
      circle.setAttribute("cx", screen.x.toFixed(2));
      circle.setAttribute("cy", screen.y.toFixed(2));
      circle.setAttribute("r", radius);
      const title = document.createElementNS("http://www.w3.org/2000/svg", "title");
      title.textContent = titleText;
      circle.appendChild(title);
      markersEl.appendChild(circle);
    }

    function renderMarkers(active, info) {
      markersEl.replaceChildren();
      const step = Math.max(1, Math.floor(active.length / 400));
      for (let i = 0; i < active.length; i += step) {
        const point = active[i];
        addCircle(point, "point", "2", "#" + point.point_index + " " + fmt(point.latitude_deg, 8) + ", " + fmt(point.longitude_deg, 8));
      }
      if (info.ok) {
        addCircle(info.start, "start", "6", "起点 #" + info.start.point_index);
        addCircle(info.end, "end", "6", "终点 #" + info.end.point_index);
      }
      if (state.nearestIndex !== null) {
        const nearest = active.find(point => point.point_index === state.nearestIndex);
        if (nearest) addCircle(nearest, "nearest", "5", "最近选中 #" + nearest.point_index);
      }
    }

    function renderTable(active) {
      tableEl.replaceChildren();
      const shown = active.slice(0, MAX_TABLE_ROWS);
      for (const point of shown) {
        const tr = document.createElement("tr");
        tr.innerHTML = "<td>" + point.point_index + "</td>" +
          "<td>" + fmt(point.latitude_deg, 6) + "<br>" + fmt(point.longitude_deg, 6) + "</td>" +
          "<td>" + (point.gnss_fix || "") + "</td>" +
          "<td><button class='small-button' data-action='start' data-index='" + point.point_index + "'>起</button> " +
          "<button class='small-button' data-action='end' data-index='" + point.point_index + "'>终</button> " +
          "<button class='small-button' data-action='filter' data-index='" + point.point_index + "'>滤</button></td>";
        tableEl.appendChild(tr);
      }
      tableNoteEl.textContent = active.length > MAX_TABLE_ROWS ? "仅显示前 " + MAX_TABLE_ROWS + " 个当前可见点；更远点可直接输入 point_index。" : "当前可见点已全部显示。";
    }

function render() {
	      updateViewBox();
	      document.getElementById("zoomLabel").textContent = state.zoom;
	      state.excluded = parseIndexSet(filterEl.value);
      state.active = currentActivePoints();
      if (!state.centerWorld) fitTo(state.active.length ? state.active : points);
      const startIndex = Number.parseInt(startEl.value, 10);
      const endIndex = Number.parseInt(endEl.value, 10);
      const info = routeInfo(state.active, startIndex, endIndex);
      routeAllEl.setAttribute("points", polyline(state.active));
      routeSelectedEl.setAttribute("points", info.ok ? polyline(info.segment) : "");
      renderTiles();
      renderMarkers(state.active, info);
      renderTable(state.active);
      if (!points.length) {
        metricsEl.innerHTML = "尚未载入轨迹。";
        return;
      }
      if (!info.ok) {
        metricsEl.innerHTML = "<div class='error'>" + info.message + "</div><div>当前可见点数: <strong>" + state.active.length + "</strong></div><div>已过滤点数: <strong>" + state.excluded.size + "</strong></div>";
        return;
      }
      metricsEl.innerHTML =
        "<div>当前可见点数: <strong>" + state.active.length + "</strong></div>" +
        "<div>已过滤点数: <strong>" + state.excluded.size + "</strong></div>" +
        "<div>当前总路线距离: <strong>" + info.total.toFixed(3) + " m</strong></div>" +
        "<div>#" + startIndex + " 到 #" + endIndex + " 路线距离: <strong>" + info.distance.toFixed(3) + " m</strong></div>" +
        "<div>解析来源: <strong>" + (stats.source || "--") + "</strong></div>" +
        "<div>有效帧/异常帧: <strong>" + (stats.validFrames || 0) + "/" + (stats.badFrames || 0) + "</strong></div>";
    }

    function csvValue(value) {
      if (value === null || value === undefined) return "";
      return String(value);
    }

    function exportCsv() {
      const active = currentActivePoints();
      if (!active.length) return;
      const lines = [CSV_FIELDS.join(",")];
      for (const point of active) {
        lines.push(CSV_FIELDS.map(field => {
          const text = csvValue(point[field]).replaceAll('"', '""');
          return '"' + text + '"';
        }).join(","));
      }
      const blob = new Blob(["\ufeff" + lines.join("\r\n")], { type: "text/csv;charset=utf-8" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = "epsilon_route_current.csv";
      a.click();
      URL.revokeObjectURL(a.href);
    }

    document.getElementById("applyButton").addEventListener("click", () => { state.nearestIndex = null; render(); });
    document.getElementById("fitButton").addEventListener("click", () => { state.excluded = parseIndexSet(filterEl.value); fitTo(currentActivePoints().length ? currentActivePoints() : points); render(); });
    document.getElementById("resetButton").addEventListener("click", () => { filterEl.value = ""; startEl.value = points.length ? 1 : ""; endEl.value = points.length || ""; state.nearestIndex = null; fitTo(points); render(); });
    document.getElementById("exportCsvButton").addEventListener("click", exportCsv);
    providerEl.addEventListener("change", renderTiles);
    keyEl.addEventListener("change", renderTiles);
window.addEventListener("resize", () => { state.centerWorld = null; fitTo(state.active.length ? state.active : points); render(); });
	function zoomTo(zoom) { state.zoom = Math.max(state.minZoom, Math.min(state.maxZoom, zoom)); if (state.centerLat != null) state.centerWorld = latLonToWorld(state.centerLat, state.centerLon, state.zoom); render(); }
	    document.getElementById("zoomInBtn").addEventListener("click", () => zoomTo(state.zoom + 1));
	    document.getElementById("zoomOutBtn").addEventListener("click", () => zoomTo(state.zoom - 1));
	    svgEl.parentElement.addEventListener("wheel", event => { event.preventDefault(); const dz = event.deltaY < 0 ? 1 : -1; zoomTo(state.zoom + dz); }, { passive: false });
	    tableEl.addEventListener("click", event => {
      const button = event.target.closest("button[data-action]");
      if (!button) return;
      const index = Number.parseInt(button.dataset.index, 10);
      if (button.dataset.action === "start") startEl.value = index;
      if (button.dataset.action === "end") endEl.value = index;
      if (button.dataset.action === "filter") {
        const setValue = parseIndexSet(filterEl.value);
        setValue.add(index);
        filterEl.value = setToText(setValue);
      }
      state.nearestIndex = index;
      render();
    });
    svgEl.addEventListener("click", event => {
      const rect = svgEl.getBoundingClientRect();
      const x = event.clientX - rect.left;
      const y = event.clientY - rect.top;
      let best = null;
      let bestDistance = Infinity;
      for (const point of state.active) {
        const screen = pointToScreen(point);
        const distance = Math.hypot(screen.x - x, screen.y - y);
        if (distance < bestDistance) {
          bestDistance = distance;
          best = point;
        }
      }
      if (!best) return;
      state.nearestIndex = best.point_index;
      if (clickModeEl.value === "start") startEl.value = best.point_index;
      if (clickModeEl.value === "end") endEl.value = best.point_index;
      if (clickModeEl.value === "filter") {
        const setValue = parseIndexSet(filterEl.value);
        setValue.add(best.point_index);
        filterEl.value = setToText(setValue);
      }
      render();
    });

    fitTo(points);
    render();
  </script>
</div>
</body>
</html>
"""
    return app.replace("__TIANDITU_KEY__", DEFAULT_TIANDITU_KEY)


def write_browser_app(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(browser_app_html(), encoding="utf-8")


def open_browser_app() -> Path:
    app_path = Path(tempfile.gettempdir()) / "epsilon_raw_route_browser.html"
    write_browser_app(app_path)
    webbrowser.open(app_path.resolve().as_uri())
    return app_path


def load_points(input_path: Path, args: argparse.Namespace) -> tuple[list[EpsilonPoint], ParseStats]:
    stats = ParseStats()
    files = resolve_input_files(input_path)
    stats.input_files = len(files)
    records: list[RawRecord] = []
    for file in files:
        records.extend(iter_records(file))
    candidates = decode_coordinate_candidates(records, args.allow_bad_crc, stats)
    selected = select_position_source(candidates, args.position_source, stats)
    include_indices = parse_index_ranges(args.include_point, args.index_base, "--include-point")
    exclude_indices = parse_index_ranges(args.exclude_point, args.index_base, "--exclude-point")
    min_fix_code = parse_fix_code(args.min_fix)
    points = apply_filters(
        selected,
        include_indices=include_indices,
        exclude_indices=exclude_indices,
        min_fix_code=min_fix_code,
        max_hacc=args.max_hacc,
        max_vacc=args.max_vacc,
        max_segment_m=args.max_segment_m,
        stats=stats,
    )
    assign_route_distances(points)
    stats.exported_points = len(points)
    return points, stats


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="解析 VaporView EPSILON 原始数据，导出轨迹 CSV，并生成可交互 HTML 地图。",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("input", nargs="?", help="会话目录、raw/epsilon.dat、legacy sensors/epsilon_raw.dat，或裸 FDILink 文件")
    parser.add_argument("--output-csv", "-o", type=Path, help="导出的轨迹 CSV 路径")
    parser.add_argument("--map-html", type=Path, help="导出的 HTML 地图路径")
    parser.add_argument(
        "--position-source",
        choices=["auto", "geodetic", "system", "both"],
        default="auto",
        help="轨迹点来源：auto 优先 0x5C GEODETIC_POS，缺失时使用 0x50 SYS_STATE",
    )
    parser.add_argument("--start-index", help="计算路线距离的起点索引；默认第一点")
    parser.add_argument("--end-index", help="计算路线距离的终点索引；默认最后一点")
    parser.add_argument("--index-base", type=int, choices=[0, 1], default=1, help="命令行索引是 0 基还是 1 基")
    parser.add_argument(
        "--exclude-point",
        "--drop-point",
        action="append",
        default=[],
        help="过滤指定 source_point_index，支持逗号和范围，例如 5,8-10；可重复",
    )
    parser.add_argument(
        "--include-point",
        action="append",
        default=[],
        help="只保留指定 source_point_index，支持逗号和范围，例如 1-200；可重复",
    )
    parser.add_argument("--min-fix", help="最小 GNSS fix，可用数字或 NO_FIX/3D/DGPS/RTK_FLOAT/RTK_FIXED 等")
    parser.add_argument("--max-hacc", type=float, help="过滤 hacc_m 大于该值的点")
    parser.add_argument("--max-vacc", type=float, help="过滤 vacc_m 大于该值的点")
    parser.add_argument("--max-segment-m", type=float, help="过滤与上一保留点距离大于该值的跳点；轨迹查看器常用 20")
    parser.add_argument("--allow-bad-crc", action="store_true", help="即使 FDILink CRC 异常也尝试解码")
    parser.add_argument("--map-width", type=int, default=1100, help="HTML 地图宽度，单位 px")
    parser.add_argument("--map-height", type=int, default=720, help="HTML 地图高度，单位 px")
    parser.add_argument("--map-zoom", type=int, help="强制使用的地图 zoom；不传则自动适配轨迹范围")
    parser.add_argument("--min-map-zoom", type=int, default=1, help="自动缩放最小 zoom")
    parser.add_argument("--max-map-zoom", type=int, default=18, help="自动缩放最大 zoom")
    parser.add_argument(
        "--map-provider",
        choices=["tianditu_vec", "tianditu_img", "osm"],
        default="tianditu_vec",
        help="HTML 地图默认底图",
    )
    parser.add_argument("--tianditu-key", default=DEFAULT_TIANDITU_KEY, help="HTML 地图使用的天地图 Key")
    parser.add_argument("--title", default="EPSILON Route", help="HTML 地图标题")
    parser.add_argument("--self-test", action="store_true", help="生成一份临时 EPSILON raw DAT 并验证解析、CSV、地图输出")
    return parser


def build_fdilink_frame(packet_id: int, payload: bytes, serial: int) -> bytes:
    if len(payload) > 255:
        raise ValueError("FDILink payload must be <= 255 bytes")
    header = bytes([FRAME_HEAD, packet_id, len(payload), serial & 0xFF])
    crc8 = fdilink_crc8(header)
    crc16 = fdilink_crc16(payload)
    return header + bytes([crc8, (crc16 >> 8) & 0xFF, crc16 & 0xFF]) + payload + bytes([FRAME_TAIL])


def make_geodetic_payload(latitude_deg: float, longitude_deg: float, height_m: float) -> bytes:
    return struct.pack("<dddff", math.radians(latitude_deg), math.radians(longitude_deg), height_m, 0.05, 0.08)


def write_self_test_raw(path: Path) -> None:
    frames = [
        build_fdilink_frame(0x5C, make_geodetic_payload(31.2304000, 121.4737000, 4.0), 1),
        build_fdilink_frame(0x5C, make_geodetic_payload(31.2304500, 121.4738000, 4.1), 2),
        build_fdilink_frame(0x5C, make_geodetic_payload(31.2305200, 121.4739500, 4.2), 3),
    ]
    with path.open("wb") as file:
        file.write(struct.pack("<8sIIHH", UNIFIED_RAW_MAGIC, 2, UNIFIED_RAW_HEADER_SIZE, RAW_SOURCE_EPSILON, 0))
        for sequence, frame in enumerate(frames):
            host_timestamp_us = 1_735_689_600_000_000 + sequence * 100_000
            file.write(
                struct.pack(
                    "<IIQIHHIQ",
                    UNIFIED_RAW_RECORD_MARKER,
                    UNIFIED_RAW_RECORD_HEADER_SIZE,
                    host_timestamp_us,
                    len(frame),
                    RAW_SOURCE_EPSILON,
                    0x5C,
                    frame[3],
                    sequence,
                )
            )
            file.write(frame)


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="epsilon_route_selftest_") as temp_dir_text:
        temp_dir = Path(temp_dir_text)
        raw_path = temp_dir / "epsilon.dat"
        csv_path = temp_dir / "route.csv"
        map_path = temp_dir / "route.html"
        write_self_test_raw(raw_path)
        args = build_arg_parser().parse_args(
            [
                str(raw_path),
                "--output-csv",
                str(csv_path),
                "--map-html",
                str(map_path),
                "--start-index",
                "1",
                "--end-index",
                "3",
            ]
        )
        points, stats = load_points(raw_path, args)
        if len(points) != 3:
            raise EpsilonRouteError(f"self-test expected 3 points, got {len(points)}")
        if route_distance_between(points, 1, 3) <= 0.0:
            raise EpsilonRouteError("self-test route distance should be positive")
        export_csv(points, csv_path)
        write_map_html(
            points,
            map_path,
            args.title,
            1,
            3,
            args.map_width,
            args.map_height,
            None,
            1,
            18,
            args.map_provider,
            args.tianditu_key,
        )
        if not csv_path.is_file() or not map_path.is_file():
            raise EpsilonRouteError("self-test did not create CSV and map outputs")
        html = map_path.read_text(encoding="utf-8")
        for expected_text in ("应用过滤并重算", "tianditu_vec", DEFAULT_TIANDITU_KEY, "当前总路线距离"):
            if expected_text not in html:
                raise EpsilonRouteError(f"self-test map HTML is missing {expected_text!r}")
        print(
            "self-test OK: "
            f"frames={stats.valid_frames}, points={len(points)}, "
            f"distance={route_distance_between(points, 1, 3):.3f} m"
        )
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    raw_args = list(sys.argv[1:] if argv is None else argv)
    if not raw_args:
        app_path = open_browser_app()
        print(f"browser_app={app_path}")
        print("请在打开的浏览器页面中选择 session 文件夹或 EPSILON DAT 文件。")
        return 0

    parser = build_arg_parser()
    args = parser.parse_args(raw_args)

    try:
        if args.self_test:
            return run_self_test()
        if not args.input:
            parser.error("input is required unless --self-test is used")
        input_path = Path(args.input)
        points, stats = load_points(input_path, args)
        if not points:
            raise EpsilonRouteError("no valid route points remain after parsing and filtering")

        start_index = cli_index_to_one_based(args.start_index, args.index_base, "--start-index") if args.start_index else 1
        end_index = cli_index_to_one_based(args.end_index, args.index_base, "--end-index") if args.end_index else len(points)
        selected_distance = route_distance_between(points, start_index, end_index)

        if args.output_csv:
            export_csv(points, args.output_csv)
        if args.map_html:
            write_map_html(
                points,
                args.map_html,
                args.title,
                start_index,
                end_index,
                args.map_width,
                args.map_height,
                args.map_zoom,
                args.min_map_zoom,
                args.max_map_zoom,
                args.map_provider,
                args.tianditu_key,
            )

        total_distance = points[-1].cumulative_distance_m
        print(f"input_files={stats.input_files}")
        print(f"raw_records={stats.raw_records}, valid_frames={stats.valid_frames}, bad_frames={stats.bad_frames}")
        if stats.auto_position_source:
            print(f"position_source=auto -> {stats.auto_position_source}")
        else:
            print(f"position_source={args.position_source}")
        print(f"coordinate_candidates={stats.coordinate_candidates}, selected_source_candidates={stats.selected_source_candidates}")
        print(f"exported_points={len(points)}, filtered_points={stats.filtered_points}")
        print(f"total_route_distance_m={total_distance:.3f}")
        print(f"route_distance_#{start_index}_to_#{end_index}_m={selected_distance:.3f}")
        if args.output_csv:
            print(f"csv={args.output_csv}")
        if args.map_html:
            print(f"map_html={args.map_html}")
        return 0
    except EpsilonRouteError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
