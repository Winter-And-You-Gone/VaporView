#!/usr/bin/env python3
"""
Mock VaporView TCP waveform sender.

VaporView connects as a TCP client. This script listens on a TCP port and sends
one frame as:

    int32 little-endian payload byte count + raw float32 samples
    int32 little-endian payload byte count + normalized-second-harmonic samples

By default the generated sample throughput is fixed at 500,000 points per
second per wave. The per-frame sample count is derived from the requested frame
rate, so 10 Hz sends 50,000 points per frame and 100 Hz sends 5,000 points.
"""

from __future__ import annotations

import argparse
import array
import math
import random
import signal
import socket
import struct
import sys
import threading
import time
from typing import Iterable


DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8888
DEFAULT_SAMPLES_PER_SECOND = 500_000
DEFAULT_RATE_HZ = 10.0
REFERENCE_SAMPLE_COUNT = 50_000
REFERENCE_PEAK_WIDTH = 520.0
DEFAULT_PRECOMPUTE_FRAMES = 120
SOCKET_TIMEOUT_SECONDS = 0.5


def samples_for_rate(rate_hz: float, samples_per_second: int) -> int:
    if rate_hz <= 0.0:
        raise ValueError("--rate must be positive")
    if samples_per_second <= 0:
        raise ValueError("--samples-per-second must be positive")
    return max(1, int(round(samples_per_second / rate_hz)))


def default_peak_width(sample_count: int) -> float:
    return max(1.0, REFERENCE_PEAK_WIDTH * sample_count / REFERENCE_SAMPLE_COUNT)


def little_endian_float_bytes(values: Iterable[float]) -> bytes:
    payload = array.array("f", values)
    if sys.byteorder != "little":
        payload.byteswap()
    return payload.tobytes()


def build_wave_pair(
    frame_index: int,
    sample_count: int,
    peak_index: int,
    peak_amplitude: float,
    peak_width: float,
    noise: float,
    move_peak: bool,
) -> tuple[bytes, bytes, int, float]:
    phase = frame_index * 0.14
    moving_offset = int(math.sin(frame_index * 0.11) * sample_count * 0.08) if move_peak else 0
    center = max(0, min(sample_count - 1, peak_index + moving_offset))

    raw_values: list[float] = []
    harmonic_values: list[float] = []
    inv_two_sigma_sq = 1.0 / (2.0 * peak_width * peak_width)

    for i in range(sample_count):
        x = i / max(1, sample_count - 1)
        carrier = math.sin(2.0 * math.pi * (7.0 * x + phase))
        slow = 0.18 * math.sin(2.0 * math.pi * (1.3 * x + phase * 0.35))
        jitter = random.uniform(-noise, noise) if noise > 0.0 else 0.0

        distance = i - center
        peak = peak_amplitude * math.exp(-(distance * distance) * inv_two_sigma_sq)
        shoulder = 0.25 * peak_amplitude * math.exp(-((distance - peak_width * 2.8) ** 2) * inv_two_sigma_sq * 0.55)

        raw_values.append(0.35 * carrier + slow + 0.08 * peak + jitter)
        harmonic_values.append(0.03 * carrier + 0.04 * slow + peak + shoulder + 0.35 * jitter)

    return (
        little_endian_float_bytes(raw_values),
        little_endian_float_bytes(harmonic_values),
        center,
        max(harmonic_values) if harmonic_values else 0.0,
    )


def send_payload(sock: socket.socket, payload: bytes, stop_event: threading.Event) -> None:
    if stop_event.is_set():
        raise InterruptedError
    sock.sendall(struct.pack("<i", len(payload)))
    if stop_event.is_set():
        raise InterruptedError
    sock.sendall(payload)


def pack_payload(payload: bytes) -> bytes:
    return struct.pack("<i", len(payload)) + payload


def build_frame_bytes(
    frame_index: int,
    sample_count: int,
    peak_index: int,
    peak_amplitude: float,
    peak_width: float,
    noise: float,
    move_peak: bool,
) -> tuple[bytes, int, float]:
    raw_payload, harmonic_payload, center, peak_value = build_wave_pair(
        frame_index=frame_index,
        sample_count=sample_count,
        peak_index=peak_index,
        peak_amplitude=peak_amplitude,
        peak_width=peak_width,
        noise=noise,
        move_peak=move_peak,
    )
    return pack_payload(raw_payload) + pack_payload(harmonic_payload), center, peak_value


def precompute_frames(args: argparse.Namespace, peak_index: int) -> list[tuple[bytes, int, float]]:
    if args.precompute_frames <= 0:
        return []

    frame_count = args.precompute_frames
    if args.frames > 0:
        frame_count = min(frame_count, args.frames)
    frame_count = max(1, frame_count)

    started = time.perf_counter()
    print(f"Precomputing {frame_count} frame(s) for fast replay...", flush=True)
    frames = [
        build_frame_bytes(
            frame_index=frame_index,
            sample_count=args.samples,
            peak_index=peak_index,
            peak_amplitude=args.peak,
            peak_width=args.peak_width,
            noise=args.noise,
            move_peak=args.move_peak,
        )
        for frame_index in range(frame_count)
    ]
    elapsed = time.perf_counter() - started
    total_bytes = sum(len(frame[0]) for frame in frames)
    print(
        f"Precomputed {frame_count} frame(s), {total_bytes / (1024 * 1024):.1f} MiB "
        f"in {elapsed:.2f}s. Use --precompute-frames 0 for live generation.",
        flush=True,
    )
    return frames


def serve(args: argparse.Namespace, stop_event: threading.Event) -> None:
    if args.samples is None:
        args.samples = samples_for_rate(args.rate, args.samples_per_second)
    if args.samples <= 0:
        raise ValueError("--samples must be positive")
    if args.rate <= 0.0:
        raise ValueError("--rate must be positive")
    if args.peak_width is None:
        args.peak_width = default_peak_width(args.samples)
    if args.peak_width <= 0.0:
        raise ValueError("--peak-width must be positive")

    peak_index = args.peak_index
    if peak_index is None:
        peak_index = args.samples // 2
    peak_index = max(0, min(args.samples - 1, peak_index))

    frame_interval = 1.0 / args.rate
    replay_frames = precompute_frames(args, peak_index)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.settimeout(SOCKET_TIMEOUT_SECONDS)
        server.bind((args.host, args.port))
        server.listen(1)
        print(f"Listening on {args.host}:{args.port}; set VaporView TCP host to 127.0.0.1 and port to {args.port}.")
        print(
            f"Frame: two payloads, {args.samples} float32 samples each, {args.rate:g} Hz "
            f"({args.samples * args.rate:g} points/s per wave)."
        )
        if replay_frames:
            print(f"Mode: precomputed replay, {len(replay_frames)} frame(s) looped.")
        else:
            print("Mode: live generation; high rates may be CPU-bound in Python.")
        print("Press Ctrl+C to stop.")

        frame_index = 0
        while not stop_event.is_set():
            try:
                conn, address = server.accept()
            except socket.timeout:
                continue
            with conn:
                conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                conn.settimeout(SOCKET_TIMEOUT_SECONDS)
                print(f"Client connected from {address[0]}:{address[1]}")
                next_send_time = time.perf_counter()
                last_report_time = next_send_time
                frames_since_report = 0
                frames_sent_to_client = 0
                try:
                    while not stop_event.is_set():
                        if replay_frames:
                            frame_payload, center, peak_value = replay_frames[frame_index % len(replay_frames)]
                            if stop_event.is_set():
                                raise InterruptedError
                            conn.sendall(frame_payload)
                        else:
                            raw_payload, harmonic_payload, center, peak_value = build_wave_pair(
                                frame_index=frame_index,
                                sample_count=args.samples,
                                peak_index=peak_index,
                                peak_amplitude=args.peak,
                                peak_width=args.peak_width,
                                noise=args.noise,
                                move_peak=args.move_peak,
                            )
                            send_payload(conn, raw_payload, stop_event)
                            send_payload(conn, harmonic_payload, stop_event)

                        frames_since_report += 1
                        if frame_index % max(1, int(args.rate)) == 0:
                            now = time.perf_counter()
                            elapsed = now - last_report_time
                            actual_rate = frames_since_report / elapsed if elapsed > 0.0 else 0.0
                            last_report_time = now
                            frames_since_report = 0
                            print(
                                f"sent frame {frame_index:06d}: peak index={center}, "
                                f"harmonic peak={peak_value:.4f}, actual={actual_rate:.1f} Hz",
                                flush=True,
                            )

                        frame_index += 1
                        frames_sent_to_client += 1
                        if args.frames > 0 and frames_sent_to_client >= args.frames:
                            print(f"Sent {frames_sent_to_client} frame(s); exiting because --frames was set.")
                            return

                        next_send_time += frame_interval
                        delay = next_send_time - time.perf_counter()
                        if delay > 0.0:
                            stop_event.wait(delay)
                        else:
                            next_send_time = time.perf_counter()
                    break
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    print("Client disconnected; waiting for the next connection.")
                except socket.timeout:
                    if stop_event.is_set():
                        break
                    print("Client send timed out; waiting for the next connection.")
                except InterruptedError:
                    break


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send mock VaporView TCP waveform frames with a visible peak.")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"Listen address, default {DEFAULT_HOST}")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"Listen port, default {DEFAULT_PORT}")
    parser.add_argument(
        "--samples",
        type=int,
        default=None,
        help=(
            "Samples per wave frame. Default is derived from --samples-per-second / --rate, "
            "for example 50000 at 10 Hz and 5000 at 100 Hz."
        ),
    )
    parser.add_argument(
        "--samples-per-second",
        type=int,
        default=DEFAULT_SAMPLES_PER_SECOND,
        help=(
            "Generated sample throughput per wave, used when --samples is omitted. "
            f"Default {DEFAULT_SAMPLES_PER_SECOND}."
        ),
    )
    parser.add_argument("--rate", type=float, default=DEFAULT_RATE_HZ, help=f"Frames per second, default {DEFAULT_RATE_HZ:g}")
    parser.add_argument("--peak-index", type=int, default=None, help="Peak center sample index, default middle")
    parser.add_argument("--peak", type=float, default=1.0, help="Peak amplitude in the harmonic wave, default 1.0")
    parser.add_argument(
        "--peak-width",
        type=float,
        default=None,
        help="Gaussian peak width in samples, default scales from 520 at 50000 samples/frame",
    )
    parser.add_argument("--noise", type=float, default=0.01, help="Uniform noise amplitude, default 0.01")
    parser.add_argument("--move-peak", action="store_true", help="Move the peak slowly across frames")
    parser.add_argument("--frames", type=int, default=0, help="Frames to send before exiting; 0 means run forever")
    parser.add_argument(
        "--precompute-frames",
        type=int,
        default=DEFAULT_PRECOMPUTE_FRAMES,
        help=(
            "Prebuild this many full frames and loop them while sending. "
            f"Default {DEFAULT_PRECOMPUTE_FRAMES}; use 0 to generate every frame live."
        ),
    )
    return parser.parse_args()


def main() -> int:
    stop_event = threading.Event()

    def request_stop(signum: int, _frame: object) -> None:
        del signum
        stop_event.set()

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, request_stop)

    try:
        serve(parse_args(), stop_event)
    except KeyboardInterrupt:
        stop_event.set()
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    if stop_event.is_set():
        print("\nStopped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
