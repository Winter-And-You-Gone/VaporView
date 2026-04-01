#!/usr/bin/env python
"""Preview wave1/wave4 data streamed from a local TCP socket.

Protocol inferred from the LabVIEW reference:
    int32_le wave1_payload_size
    wave1_payload (float32_le array)
    int32_le wave4_payload_size
    wave4_payload (float32_le array)

Use --mock to start a local sender that emits synthetic frames in the same format.
"""

from __future__ import annotations

import argparse
import math
import queue
import socket
import struct
import threading
import time
from dataclasses import dataclass
from typing import Optional

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.animation import FuncAnimation


HEADER_STRUCT = struct.Struct("<i")
FLOAT_SIZE = 4
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8888
MAX_PAYLOAD_BYTES = 16 * 1024 * 1024


@dataclass
class WavePair:
    wave1: np.ndarray
    wave4: np.ndarray
    received_at: float


class TcpWaveReader(threading.Thread):
    def __init__(self, host: str, port: int, output_queue: "queue.Queue[WavePair]", stop_event: threading.Event) -> None:
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.output_queue = output_queue
        self.stop_event = stop_event
        self.last_status = "waiting to connect"

    def run(self) -> None:
        while not self.stop_event.is_set():
            sock: Optional[socket.socket] = None
            try:
                self.last_status = f"connecting to {self.host}:{self.port}"
                sock = socket.create_connection((self.host, self.port), timeout=5)
                sock.settimeout(2.0)
                self.last_status = f"connected to {self.host}:{self.port}"
                while not self.stop_event.is_set():
                    pair = self._read_wave_pair(sock)
                    self.output_queue.put(pair)
                    self.last_status = (
                        f"connected to {self.host}:{self.port} | "
                        f"wave1={pair.wave1.size} samples wave4={pair.wave4.size} samples"
                    )
            except (ConnectionError, OSError, ValueError) as exc:
                self.last_status = f"stream error: {exc}"
                time.sleep(1.0)
            finally:
                if sock is not None:
                    try:
                        sock.close()
                    except OSError:
                        pass

    def _recv_exact(self, sock: socket.socket, size: int) -> bytes:
        data = bytearray()
        while len(data) < size and not self.stop_event.is_set():
            chunk = sock.recv(size - len(data))
            if not chunk:
                raise ConnectionError("socket closed by peer")
            data.extend(chunk)
        if len(data) != size:
            raise ConnectionError(f"incomplete packet, expected {size} bytes, got {len(data)}")
        return bytes(data)

    def _read_payload(self, sock: socket.socket) -> np.ndarray:
        header = self._recv_exact(sock, HEADER_STRUCT.size)
        payload_size = HEADER_STRUCT.unpack(header)[0]
        if payload_size < 0:
            raise ValueError(f"negative payload size {payload_size}")
        if payload_size > MAX_PAYLOAD_BYTES:
            raise ValueError(f"payload too large: {payload_size} bytes")
        if payload_size % FLOAT_SIZE != 0:
            raise ValueError(f"payload size {payload_size} is not a multiple of {FLOAT_SIZE}")
        payload = self._recv_exact(sock, payload_size)
        return np.frombuffer(payload, dtype="<f4").copy()

    def _read_wave_pair(self, sock: socket.socket) -> WavePair:
        wave1 = self._read_payload(sock)
        wave4 = self._read_payload(sock)
        return WavePair(wave1=wave1, wave4=wave4, received_at=time.time())


class MockWaveServer(threading.Thread):
    def __init__(self, host: str, port: int, stop_event: threading.Event) -> None:
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.stop_event = stop_event

    def run(self) -> None:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.port))
        server.listen(1)
        server.settimeout(1.0)
        phase = 0.0
        try:
            while not self.stop_event.is_set():
                try:
                    client, _ = server.accept()
                except socket.timeout:
                    continue
                client.settimeout(1.0)
                with client:
                    while not self.stop_event.is_set():
                        wave1, wave4 = self._make_frame(phase)
                        phase += 0.12
                        self._send_array(client, wave1)
                        self._send_array(client, wave4)
                        time.sleep(0.1)
        finally:
            server.close()

    def _make_frame(self, phase: float) -> tuple[np.ndarray, np.ndarray]:
        x1 = np.linspace(0.0, 8.0 * math.pi, 50000, dtype=np.float32)
        wave1 = (0.0045 * np.sin(x1 + phase) + 0.001 * np.sin(3.0 * x1 + phase * 0.5)).astype(np.float32)

        x4 = np.linspace(-6.0, 6.0, 5000, dtype=np.float32)
        logistic = 1.0 / (1.0 + np.exp(-(x4 - math.sin(phase) * 0.5)))
        wave4 = (3.0 + 0.7 * logistic).astype(np.float32)
        return wave1, wave4

    def _send_array(self, sock: socket.socket, values: np.ndarray) -> None:
        payload = values.astype("<f4", copy=False).tobytes()
        sock.sendall(HEADER_STRUCT.pack(len(payload)))
        sock.sendall(payload)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Preview wave1 and wave4 streamed over TCP.")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"TCP host, default {DEFAULT_HOST}")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"TCP port, default {DEFAULT_PORT}")
    parser.add_argument(
        "--mock",
        action="store_true",
        help="Start a local mock sender that emits wave1/wave4 frames using the inferred protocol.",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    stop_event = threading.Event()
    output_queue: "queue.Queue[WavePair]" = queue.Queue(maxsize=8)

    mock_server: Optional[MockWaveServer] = None
    if args.mock:
        mock_server = MockWaveServer(args.host, args.port, stop_event)
        mock_server.start()
        time.sleep(0.3)

    reader = TcpWaveReader(args.host, args.port, output_queue, stop_event)
    reader.start()

    fig, axes = plt.subplots(2, 1, figsize=(11, 8))
    fig.suptitle(f"TCP Wave Preview  {args.host}:{args.port}")

    line1, = axes[0].plot([], [], color="#f2f2f2", linewidth=1.0)
    axes[0].set_title("波形图1")
    axes[0].set_xlabel("Time")
    axes[0].set_ylabel("Amplitude")
    axes[0].set_facecolor("#050805")
    axes[0].grid(True, color="#007200", alpha=0.85)

    line4, = axes[1].plot([], [], color="#ffe100", linewidth=1.2)
    axes[1].set_title("波形图4")
    axes[1].set_xlabel("Time")
    axes[1].set_ylabel("Amplitude")
    axes[1].set_facecolor("#050805")
    axes[1].grid(True, color="#007200", alpha=0.85)

    status_text = fig.text(0.01, 0.01, "starting...", ha="left", va="bottom")

    latest = {
        "wave1": np.array([], dtype=np.float32),
        "wave4": np.array([], dtype=np.float32),
        "timestamp": 0.0,
    }

    def autoscale(axis: plt.Axes, values: np.ndarray) -> None:
        if values.size == 0:
            axis.set_xlim(0, 1)
            axis.set_ylim(-1, 1)
            return
        axis.set_xlim(0, max(1, values.size - 1))
        vmin = float(np.min(values))
        vmax = float(np.max(values))
        if math.isclose(vmin, vmax):
            pad = max(1e-6, abs(vmin) * 0.05 + 1e-6)
        else:
            pad = max((vmax - vmin) * 0.08, 1e-6)
        axis.set_ylim(vmin - pad, vmax + pad)

    def update(_frame_index: int):
        updated = False
        while True:
            try:
                pair = output_queue.get_nowait()
            except queue.Empty:
                break
            latest["wave1"] = pair.wave1
            latest["wave4"] = pair.wave4
            latest["timestamp"] = pair.received_at
            updated = True

        if updated:
            wave1 = latest["wave1"]
            wave4 = latest["wave4"]
            line1.set_data(np.arange(wave1.size), wave1)
            line4.set_data(np.arange(wave4.size), wave4)
            autoscale(axes[0], wave1)
            autoscale(axes[1], wave4)

        age = time.time() - latest["timestamp"] if latest["timestamp"] else float("inf")
        if math.isinf(age):
            age_text = "no frame received yet"
        else:
            age_text = f"last frame {age:.1f}s ago"
        status_text.set_text(reader.last_status + " | " + age_text)
        return line1, line4, status_text

    animation = FuncAnimation(fig, update, interval=150, blit=False, cache_frame_data=False)
    fig._animation = animation  # keep a strong reference for TkAgg

    try:
        plt.tight_layout()
        plt.show()
    finally:
        stop_event.set()
        reader.join(timeout=2.0)
        if mock_server is not None:
            mock_server.join(timeout=2.0)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
