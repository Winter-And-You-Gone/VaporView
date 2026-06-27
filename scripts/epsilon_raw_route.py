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
import math
import struct
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
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
) -> None:
    if not points:
        raise EpsilonRouteError("cannot draw map because no route points remain after filtering")

    zoom_value = zoom if zoom is not None else choose_zoom(points, width, height, min_zoom, max_zoom)
    latitudes = [float(point.latitude_deg) for point in points]
    longitudes = [float(point.longitude_deg) for point in points]
    center_lat = (min(latitudes) + max(latitudes)) / 2.0
    center_lon = (min(longitudes) + max(longitudes)) / 2.0
    center_x, center_y = lat_lon_to_world(center_lat, center_lon, zoom_value)
    top_left_x = center_x - width / 2.0
    top_left_y = center_y - height / 2.0
    world_tiles = 2**zoom_value

    def screen_xy(point: EpsilonPoint) -> tuple[float, float]:
        world_x, world_y = lat_lon_to_world(float(point.latitude_deg), float(point.longitude_deg), zoom_value)
        return world_x - top_left_x, world_y - top_left_y

    all_polyline = " ".join(f"{x:.2f},{y:.2f}" for x, y in (screen_xy(point) for point in points))
    lo = min(start_index, end_index)
    hi = max(start_index, end_index)
    selected_points = points[lo - 1 : hi]
    selected_polyline = " ".join(f"{x:.2f},{y:.2f}" for x, y in (screen_xy(point) for point in selected_points))
    route_distance = route_distance_between(points, start_index, end_index)
    total_distance = points[-1].cumulative_distance_m if points else 0.0

    min_tile_x = math.floor(top_left_x / TILE_SIZE)
    max_tile_x = math.floor((top_left_x + width) / TILE_SIZE)
    min_tile_y = max(0, math.floor(top_left_y / TILE_SIZE))
    max_tile_y = min(world_tiles - 1, math.floor((top_left_y + height) / TILE_SIZE))
    tile_html: list[str] = []
    for tile_x in range(min_tile_x, max_tile_x + 1):
        for tile_y in range(min_tile_y, max_tile_y + 1):
            wrapped_x = tile_x % world_tiles
            left = tile_x * TILE_SIZE - top_left_x
            top = tile_y * TILE_SIZE - top_left_y
            tile_url = f"https://tile.openstreetmap.org/{zoom_value}/{wrapped_x}/{tile_y}.png"
            tile_html.append(
                f'<img class="tile" src="{tile_url}" '
                f'style="left:{left:.2f}px;top:{top:.2f}px" alt="" />'
            )

    marker_html: list[str] = []
    marker_step = max(1, len(points) // 300)
    for point in points[::marker_step]:
        x, y = screen_xy(point)
        marker_html.append(
            f'<circle class="point" cx="{x:.2f}" cy="{y:.2f}" r="2">'
            f"<title>#{point.point_index} {point.latitude_deg:.8f}, {point.longitude_deg:.8f}</title></circle>"
        )
    sx, sy = screen_xy(points[start_index - 1])
    ex, ey = screen_xy(points[end_index - 1])
    marker_html.append(f'<circle class="start" cx="{sx:.2f}" cy="{sy:.2f}" r="6"><title>Start #{start_index}</title></circle>')
    marker_html.append(f'<circle class="end" cx="{ex:.2f}" cy="{ey:.2f}" r="6"><title>End #{end_index}</title></circle>')

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <title>{html_escape(title)}</title>
  <style>
    body {{ margin: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: #0f172a; color: #e2e8f0; }}
    header {{ padding: 16px 20px; background: #111827; border-bottom: 1px solid #334155; }}
    h1 {{ margin: 0 0 8px; font-size: 20px; }}
    .summary {{ display: flex; flex-wrap: wrap; gap: 16px; font-size: 14px; color: #cbd5e1; }}
    #map {{ position: relative; width: {width}px; height: {height}px; margin: 18px auto; overflow: hidden; background: #dbeafe; border: 1px solid #334155; box-shadow: 0 12px 40px rgba(0,0,0,.35); }}
    .tile {{ position: absolute; width: 256px; height: 256px; user-select: none; }}
    svg {{ position: absolute; inset: 0; width: 100%; height: 100%; overflow: visible; }}
    .route-all {{ fill: none; stroke: rgba(15, 23, 42, .50); stroke-width: 4; stroke-linejoin: round; stroke-linecap: round; }}
    .route-selected {{ fill: none; stroke: #f97316; stroke-width: 5; stroke-linejoin: round; stroke-linecap: round; }}
    .point {{ fill: #2563eb; stroke: white; stroke-width: 1; opacity: .75; }}
    .start {{ fill: #22c55e; stroke: white; stroke-width: 2; }}
    .end {{ fill: #ef4444; stroke: white; stroke-width: 2; }}
    .attrib {{ position: absolute; left: 8px; bottom: 6px; font-size: 12px; background: rgba(255,255,255,.85); color: #0f172a; padding: 2px 6px; border-radius: 4px; }}
    .legend {{ max-width: {width}px; margin: -8px auto 18px; font-size: 13px; color: #cbd5e1; }}
  </style>
</head>
<body>
  <header>
    <h1>{html_escape(title)}</h1>
    <div class="summary">
      <span>点数: {len(points)}</span>
      <span>总轨迹距离: {total_distance:.3f} m</span>
      <span>#{start_index} 到 #{end_index} 路线距离: {route_distance:.3f} m</span>
      <span>Zoom: {zoom_value}</span>
    </div>
  </header>
  <main>
    <div id="map">
      {''.join(tile_html)}
      <svg viewBox="0 0 {width} {height}" aria-label="EPSILON route">
        <polyline class="route-all" points="{all_polyline}" />
        <polyline class="route-selected" points="{selected_polyline}" />
        {''.join(marker_html)}
      </svg>
      <div class="attrib">© OpenStreetMap contributors</div>
    </div>
    <div class="legend">橙色为本次 start/end 之间按记录顺序累加的路线段；灰色为过滤后的完整轨迹。绿色为起点，红色为终点。</div>
  </main>
</body>
</html>
""",
        encoding="utf-8",
    )


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
        description="解析 VaporView EPSILON 原始数据，导出轨迹 CSV，并生成 OSM HTML 地图。",
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
    parser.add_argument("--map-zoom", type=int, help="强制使用的 OSM zoom；不传则自动适配轨迹范围")
    parser.add_argument("--min-map-zoom", type=int, default=1, help="自动缩放最小 zoom")
    parser.add_argument("--max-map-zoom", type=int, default=18, help="自动缩放最大 zoom")
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
        write_map_html(points, map_path, args.title, 1, 3, args.map_width, args.map_height, None, 1, 18)
        if not csv_path.is_file() or not map_path.is_file():
            raise EpsilonRouteError("self-test did not create CSV and map outputs")
        print(
            "self-test OK: "
            f"frames={stats.valid_frames}, points={len(points)}, "
            f"distance={route_distance_between(points, 1, 3):.3f} m"
        )
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)

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
