#!/usr/bin/env python3
"""Probe an EPSILON FDILink serial stream using both official-demo and strict-CRC modes."""

from __future__ import annotations

import argparse
import sys
import time
from collections import Counter, deque

import serial
import serial.tools.list_ports


FRAME_HEAD = 0xFC
FRAME_TAIL = 0xFD

KNOWN_PACKET_NAMES = {
    0x40: "IMU",
    0x41: "AHRS",
    0x42: "INS_GPS",
    0x50: "SYS_STATE",
    0x59: "RAW_GNSS",
    0x5A: "SATELLITES",
    0x5C: "GEODETIC_POS",
    0x5D: "ECEF_POS",
}

OFFICIAL_LENGTHS = {
    0x40: 0x38,
    0x41: 0x30,
    0x42: 0x48,
    0x50: 0x64,
    0x5C: 0x20,
}

CRC8_TABLE = bytes([
    0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
    157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
    35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
    190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
    70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
    219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
    101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
    248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
    140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
    17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
    175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
    50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
    202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
    87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
    233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
    116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53,
])

CRC16_TABLE = [
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM1")
    parser.add_argument("--baud", type=int, nargs="+", default=[921600, 460800, 115200])
    parser.add_argument("--duration", type=float, default=3.0)
    parser.add_argument("--timeout", type=float, default=0.05)
    parser.add_argument("--show-frames", action="store_true")
    parser.add_argument("--max-frames", type=int, default=12)
    parser.add_argument("--command", action="append", default=[], help="Optional ASCII command to send after opening.")
    return parser.parse_args()


def crc8(data: bytes) -> int:
    crc = 0
    for value in data:
        crc = CRC8_TABLE[crc ^ value]
    return crc


def crc16(data: bytes) -> int:
    crc = 0
    for value in data:
        crc = CRC16_TABLE[((crc >> 8) ^ value) & 0xFF] ^ ((crc << 8) & 0xFFFF)
    return crc & 0xFFFF


def packet_name(packet_id: int) -> str:
    name = KNOWN_PACKET_NAMES.get(packet_id)
    return f"0x{packet_id:02X}({name})" if name else f"0x{packet_id:02X}"


def hex_preview(data: bytes, count: int = 32) -> str:
    return " ".join(f"{value:02X}" for value in data[:count])


def available_ports() -> list[str]:
    return sorted({port.device for port in serial.tools.list_ports.comports()})


def probe_baud(port: str, baud: int, duration: float, timeout: float, show_frames: bool, max_frames: int, commands: list[str]) -> dict[str, object]:
    stats: dict[str, object] = {
        "port": port,
        "baud": baud,
        "total_bytes": 0,
        "fc_bytes": 0,
        "loose_frames": 0,
        "official_frames": 0,
        "strict_frames": 0,
        "tail_failures": 0,
        "crc8_failures": 0,
        "crc16_failures": 0,
        "packet_counts": Counter(),
        "first_bytes": deque(maxlen=64),
        "errors": [],
    }

    frame_printed = 0
    buf = bytearray()

    try:
        with serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=timeout,
            write_timeout=0.5,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        ) as ser:
            time.sleep(0.12)

            for command in commands:
                line = command
                if not line.endswith("\r") and not line.endswith("\n"):
                    line += "\r\n"
                ser.write(line.encode("ascii", errors="ignore"))
                print(f"[{port}@{baud}] TX {command}")
                time.sleep(0.15)

            deadline = time.monotonic() + duration
            while time.monotonic() < deadline:
                chunk = ser.read(ser.in_waiting or 1)
                if not chunk:
                    continue

                stats["total_bytes"] += len(chunk)
                stats["fc_bytes"] += chunk.count(FRAME_HEAD)
                for value in chunk:
                    stats["first_bytes"].append(value)
                buf.extend(chunk)

                while len(buf) >= 8:
                    try:
                        head_index = buf.index(FRAME_HEAD)
                    except ValueError:
                        buf.clear()
                        break

                    if head_index > 0:
                        del buf[:head_index]
                    if len(buf) < 8:
                        break

                    packet_id = buf[1]
                    payload_len = buf[2]
                    frame_size = payload_len + 8
                    if frame_size <= 0 or frame_size > 4096:
                        del buf[0]
                        continue
                    if len(buf) < frame_size:
                        break

                    frame = bytes(buf[:frame_size])
                    if frame[-1] != FRAME_TAIL:
                        stats["tail_failures"] += 1
                        del buf[0]
                        continue

                    stats["loose_frames"] += 1
                    stats["packet_counts"][packet_id] += 1

                    official = packet_id in KNOWN_PACKET_NAMES
                    if official and packet_id in OFFICIAL_LENGTHS:
                        official = payload_len == OFFICIAL_LENGTHS[packet_id]
                    if official:
                        stats["official_frames"] += 1

                    crc8_ok = crc8(frame[:4]) == frame[4]
                    crc16_ok = crc16(frame[7:7 + payload_len]) == ((frame[5] << 8) | frame[6])
                    if not crc8_ok:
                        stats["crc8_failures"] += 1
                    if not crc16_ok:
                        stats["crc16_failures"] += 1
                    if crc8_ok and crc16_ok:
                        stats["strict_frames"] += 1

                    if show_frames and frame_printed < max_frames:
                        print(
                            f"[{port}@{baud}] frame {packet_name(packet_id)} "
                            f"len={payload_len} tail=True crc8={crc8_ok} crc16={crc16_ok} "
                            f"hex={hex_preview(frame, 24)}"
                        )
                        frame_printed += 1

                    del buf[:frame_size]

    except Exception as exc:  # pragma: no cover - diagnostic tool
        stats["errors"].append(str(exc))

    return stats


def print_summary(result: dict[str, object]) -> None:
    print(
        f"bytes={result['total_bytes']}, 0xFC={result['fc_bytes']}, "
        f"loose={result['loose_frames']}, official={result['official_frames']}, "
        f"strict_crc={result['strict_frames']}"
    )
    print(
        f"tail_fail={result['tail_failures']}, crc8_fail={result['crc8_failures']}, "
        f"crc16_fail={result['crc16_failures']}"
    )

    first_bytes = bytes(result["first_bytes"])
    if first_bytes:
        print(f"first_bytes: {hex_preview(first_bytes, 64)}")

    packet_counts: Counter[int] = result["packet_counts"]  # type: ignore[assignment]
    if packet_counts:
        summary = ", ".join(
            f"{packet_name(packet_id)}={count}" for packet_id, count in sorted(packet_counts.items())
        )
        print(f"packets: {summary}")

    if result["errors"]:
        print(f"errors: {' | '.join(result['errors'])}")


def main() -> int:
    args = parse_args()
    ports = available_ports()
    print(f"Available ports: {', '.join(ports)}")
    if args.port not in ports:
        print(f"Port {args.port} is not present in the current port list.", file=sys.stderr)
        return 4

    results = []
    for baud in args.baud:
        print(f"\n=== Probing {args.port} @ {baud} N81 for {args.duration:.1f}s ===")
        result = probe_baud(
            port=args.port,
            baud=baud,
            duration=args.duration,
            timeout=args.timeout,
            show_frames=args.show_frames,
            max_frames=args.max_frames,
            commands=args.command,
        )
        results.append(result)
        print_summary(result)

    best = max(
        results,
        key=lambda item: (item["strict_frames"], item["official_frames"], item["total_bytes"]),
    )
    print(
        f"\nBest result: {best['port']} @ {best['baud']}, "
        f"bytes={best['total_bytes']}, official={best['official_frames']}, strict_crc={best['strict_frames']}"
    )

    if best["strict_frames"] > 0 or best["official_frames"] > 0:
        return 0
    if best["total_bytes"] > 0:
        return 2
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
