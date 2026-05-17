#!/usr/bin/env python3
"""
VaporView TCP 波形模拟发送器。

VaporView 作为 TCP client 连接本脚本。本脚本监听 TCP 端口，并按以下格式发送一帧：

    int32 little-endian payload byte count + raw float32 samples
    int32 little-endian payload byte count + normalized-second-harmonic samples

默认每条波形的采样吞吐为 500,000 points/s。每帧点数根据帧率计算，
例如 10 Hz 每帧 50,000 点，100 Hz 每帧 5,000 点。

当前测试波形为突变矩形波：在 [2/5, 3/5) 区间内有连续 10 个点值为 2，
该 10 点块按帧周期移动且不会越过区间边缘，其余所有点值均为 -1。
"""

from __future__ import annotations

import argparse
import array
import ctypes
import contextlib
import math
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
DEFAULT_PEAK_VALUE = 2.0
DEFAULT_BASELINE_VALUE = -1.0
DEFAULT_PEAK_BLOCK_POINTS = 10
DEFAULT_PRECOMPUTE_FRAMES = 120
SOCKET_TIMEOUT_SECONDS = 0.5
DEFAULT_REPORT_INTERVAL_SECONDS = 1.0
DEFAULT_SEND_BUFFER_BYTES = 4 * 1024 * 1024
SPIN_WAIT_SECONDS = 0.001


def log(message: str = "", *, error: bool = False) -> None:
    stream = sys.stderr if error else sys.stdout
    print(message, file=stream, flush=True)


@contextlib.contextmanager
def high_resolution_timer() -> Iterable[None]:
    winmm = None
    if sys.platform.startswith("win"):
        try:
            winmm = ctypes.WinDLL("winmm")
            winmm.timeBeginPeriod(1)
        except Exception:
            winmm = None
    try:
        yield
    finally:
        if winmm is not None:
            winmm.timeEndPeriod(1)


def wait_until(target_time: float, stop_event: threading.Event) -> None:
    while not stop_event.is_set():
        remaining = target_time - time.perf_counter()
        if remaining <= 0.0:
            return
        if remaining > SPIN_WAIT_SECONDS:
            stop_event.wait(remaining - SPIN_WAIT_SECONDS)
        else:
            time.sleep(0)


def samples_for_rate(rate_hz: float, samples_per_second: int) -> int:
    if rate_hz <= 0.0:
        raise ValueError("--rate 必须为正数")
    if samples_per_second <= 0:
        raise ValueError("--samples-per-second 必须为正数")
    return max(1, int(round(samples_per_second / rate_hz)))


def default_peak_width(sample_count: int) -> float:
    return max(1.0, REFERENCE_PEAK_WIDTH * sample_count / REFERENCE_SAMPLE_COUNT)


def little_endian_float_bytes(values: Iterable[float]) -> bytes:
    payload = array.array("f", values)
    if sys.byteorder != "little":
        payload.byteswap()
    return payload.tobytes()


def moving_peak_block_start(
    frame_index: int,
    sample_count: int,
    peak_index: int,
    block_points: int,
    move_peak: bool,
) -> tuple[int, int]:
    block_points = max(1, min(block_points, sample_count))
    left = min(max(0, int(sample_count * 2 / 5)), sample_count - block_points)
    right = min(sample_count, max(left + block_points, int(sample_count * 3 / 5)))
    last_start = max(left, right - block_points)
    if last_start <= left:
        return left, block_points
    if not move_peak:
        desired_start = peak_index - block_points // 2
        return max(left, min(last_start, desired_start)), block_points

    phase = (math.sin(frame_index * 0.11) + 1.0) * 0.5
    start = left + int(round(phase * (last_start - left)))
    return max(left, min(last_start, start)), block_points


def build_wave_pair(
    frame_index: int,
    sample_count: int,
    peak_index: int,
    peak_amplitude: float,
    peak_width: float,
    noise: float,
    move_peak: bool,
) -> tuple[bytes, bytes, int, float]:
    del peak_width, noise
    start, block_points = moving_peak_block_start(
        frame_index=frame_index,
        sample_count=sample_count,
        peak_index=peak_index,
        block_points=DEFAULT_PEAK_BLOCK_POINTS,
        move_peak=move_peak,
    )
    values = [DEFAULT_BASELINE_VALUE] * sample_count
    for i in range(start, start + block_points):
        values[i] = peak_amplitude

    return (
        little_endian_float_bytes(values),
        little_endian_float_bytes(values),
        start,
        peak_amplitude if values else 0.0,
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
    log(f"正在预生成 {frame_count} 帧用于快速回放...")
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
    log(
        f"已预生成 {frame_count} 帧，大小 {total_bytes / (1024 * 1024):.1f} MiB，"
        f"耗时 {elapsed:.2f}s。使用 --precompute-frames 0 可改为实时生成。"
    )
    return frames


def serve(args: argparse.Namespace, stop_event: threading.Event) -> None:
    if args.samples is None:
        args.samples = samples_for_rate(args.rate, args.samples_per_second)
    if args.samples <= 0:
        raise ValueError("--samples 必须为正数")
    if args.rate <= 0.0:
        raise ValueError("--rate 必须为正数")
    if args.peak_width is None:
        args.peak_width = default_peak_width(args.samples)
    if args.peak_width <= 0.0:
        raise ValueError("--peak-width 必须为正数")
    if args.report_interval <= 0.0:
        raise ValueError("--report-interval 必须为正数")

    peak_index = args.peak_index
    if peak_index is None:
        peak_index = args.samples // 2
    peak_index = max(0, min(args.samples - 1, peak_index))

    frame_interval = 1.0 / args.rate
    frame_bytes = args.samples * 4 * 2 + 8
    points_per_frame_total = args.samples * 2
    points_per_second_per_wave = args.samples * args.rate
    points_per_second_total = points_per_frame_total * args.rate
    log(
        f"配置：每帧每条波形 {args.samples:,} 点，总点数/帧 {points_per_frame_total:,}，"
        f"目标帧率 {args.rate:g} Hz，目标吞吐 {points_per_second_per_wave:,.0f} points/s/波形 "
        f"（合计 {points_per_second_total:,.0f} points/s）。"
    )
    replay_frames = precompute_frames(args, peak_index)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.settimeout(SOCKET_TIMEOUT_SECONDS)
        server.bind((args.host, args.port))
        server.listen(1)
        log(f"正在监听 {args.host}:{args.port}；请将 VaporView TCP 主机设为 127.0.0.1，端口设为 {args.port}。")
        log(
            f"帧格式：两个 payload，每个 payload {args.samples} 个 float32 采样点，帧率 {args.rate:g} Hz "
            f"（每条波形 {args.samples * args.rate:g} points/s）。"
        )
        log(
            f"统计：每帧每条波形 {args.samples:,} 点，总点数/帧 {points_per_frame_total:,}，"
            f"帧字节数 {frame_bytes:,}，目标帧率 {args.rate:g} Hz，"
            f"目标吞吐 {points_per_second_per_wave:,.0f} points/s/波形 "
            f"（合计 {points_per_second_total:,.0f} points/s）。"
        )
        if replay_frames:
            log(f"模式：预生成回放，循环发送 {len(replay_frames)} 帧。")
        else:
            log("模式：实时生成；高帧率下可能受 Python CPU 性能限制。")
        log("波形：连续 10 个点为 2，在 [2/5, 3/5) 区间内周期移动，其余点为 -1。")
        log("按 Ctrl+C 停止。")

        frame_index = 0
        last_idle_report_time = time.perf_counter()
        while not stop_event.is_set():
            try:
                conn, address = server.accept()
            except socket.timeout:
                now = time.perf_counter()
                if now - last_idle_report_time >= args.report_interval:
                    last_idle_report_time = now
                    log(
                        f"等待客户端连接：每帧每条波形 {args.samples:,} 点，"
                        f"目标帧率 {args.rate:g} Hz，目标吞吐 {points_per_second_per_wave:,.0f} points/s/波形"
                    )
                continue
            with conn:
                conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                conn.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, args.send_buffer)
                conn.settimeout(SOCKET_TIMEOUT_SECONDS)
                log(f"客户端已连接：{address[0]}:{address[1]}")
                next_send_time = time.perf_counter()
                last_report_time = next_send_time
                frames_since_report = 0
                bytes_since_report = 0
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
                        bytes_since_report += frame_bytes
                        now = time.perf_counter()
                        if now - last_report_time >= args.report_interval:
                            elapsed = now - last_report_time
                            actual_rate = frames_since_report / elapsed if elapsed > 0.0 else 0.0
                            actual_points_per_wave = args.samples * actual_rate
                            actual_points_total = points_per_frame_total * actual_rate
                            actual_mib = bytes_since_report / (1024 * 1024) / elapsed if elapsed > 0.0 else 0.0
                            last_report_time = now
                            frames_since_report = 0
                            bytes_since_report = 0
                            log(
                                f"已发送帧 {frame_index:06d}：每帧每条波形 {args.samples:,} 点，"
                                f"目标 {args.rate:g} Hz，实际 {actual_rate:.2f} Hz，"
                                f"吞吐 {actual_points_per_wave:,.0f} points/s/波形，"
                                f"合计 {actual_points_total:,.0f} points/s，"
                                f"网络吞吐 {actual_mib:.2f} MiB/s，"
                                f"峰值块起点 {center}，峰值 {peak_value:.4f}"
                            )
                            if actual_rate < args.rate * 0.9:
                                log(
                                    "警告：实际帧率低于目标；sendall 可能受 Python CPU 开销或接收端 TCP 反压影响。",
                                    error=True,
                                )

                        frame_index += 1
                        frames_sent_to_client += 1
                        if args.frames > 0 and frames_sent_to_client >= args.frames:
                            log(f"已发送 {frames_sent_to_client} 帧；由于设置了 --frames，脚本退出。")
                            return

                        next_send_time += frame_interval
                        if next_send_time > time.perf_counter():
                            wait_until(next_send_time, stop_event)
                        else:
                            next_send_time = time.perf_counter()
                    break
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    log("客户端已断开，等待下一次连接。")
                except socket.timeout:
                    if stop_event.is_set():
                        break
                    log("客户端发送超时，等待下一次连接。")
                except InterruptedError:
                    break


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="发送 VaporView TCP 模拟波形帧。")
    parser._positionals.title = "位置参数"
    parser._optionals.title = "选项"
    for action in parser._actions:
        if action.option_strings == ["-h", "--help"]:
            action.help = "显示帮助信息并退出"
            break
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"监听地址，默认 {DEFAULT_HOST}")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"监听端口，默认 {DEFAULT_PORT}")
    parser.add_argument(
        "--samples",
        type=int,
        default=None,
        help=(
            "每帧每条波形采样点数。默认由 --samples-per-second / --rate 计算，"
            "例如 10 Hz 时为 50000 点，100 Hz 时为 5000 点。"
        ),
    )
    parser.add_argument(
        "--samples-per-second",
        type=int,
        default=DEFAULT_SAMPLES_PER_SECOND,
        help=(
            "每条波形生成采样吞吐；省略 --samples 时使用。"
            f"默认 {DEFAULT_SAMPLES_PER_SECOND}。"
        ),
    )
    parser.add_argument("--rate", type=float, default=DEFAULT_RATE_HZ, help=f"发送帧率 Hz，默认 {DEFAULT_RATE_HZ:g}")
    parser.add_argument("--peak-index", type=int, default=None, help="固定峰值块中心采样点；默认中点，仅 --static-peak 时使用")
    parser.add_argument("--peak", type=float, default=DEFAULT_PEAK_VALUE, help=f"峰值块数值，默认 {DEFAULT_PEAK_VALUE:g}")
    parser.add_argument(
        "--peak-width",
        type=float,
        default=None,
        help="兼容旧参数；当前矩形突变波不使用该参数",
    )
    parser.add_argument("--noise", type=float, default=0.0, help="兼容旧参数；当前矩形突变波不叠加噪声")
    parser.add_argument("--move-peak", dest="move_peak", action="store_true", help="启用峰值块周期移动（默认启用）")
    parser.add_argument("--static-peak", dest="move_peak", action="store_false", help="固定峰值块位置")
    parser.set_defaults(move_peak=True)
    parser.add_argument("--frames", type=int, default=0, help="发送指定帧数后退出；0 表示持续运行")
    parser.add_argument(
        "--precompute-frames",
        type=int,
        default=DEFAULT_PRECOMPUTE_FRAMES,
        help=(
            "预生成指定数量的完整帧并循环发送。"
            f"默认 {DEFAULT_PRECOMPUTE_FRAMES}；设为 0 表示每帧实时生成。"
        ),
    )
    parser.add_argument(
        "--report-interval",
        type=float,
        default=DEFAULT_REPORT_INTERVAL_SECONDS,
        help=f"运行统计输出间隔秒数，默认 {DEFAULT_REPORT_INTERVAL_SECONDS:g}。",
    )
    parser.add_argument(
        "--send-buffer",
        type=int,
        default=DEFAULT_SEND_BUFFER_BYTES,
        help=f"TCP 发送缓冲区字节数，默认 {DEFAULT_SEND_BUFFER_BYTES}。",
    )
    return parser.parse_args()


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)

    stop_event = threading.Event()
    args = parse_args()
    log("正在启动 TCP 模拟波形发送器。")

    def request_stop(signum: int, _frame: object) -> None:
        del signum
        stop_event.set()

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, request_stop)

    try:
        with high_resolution_timer():
            serve(args, stop_event)
    except KeyboardInterrupt:
        stop_event.set()
    except Exception as exc:
        log(f"错误：{exc}", error=True)
        return 1
    if stop_event.is_set():
        log("\n已停止。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
