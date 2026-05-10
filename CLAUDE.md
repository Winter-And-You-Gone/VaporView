# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

VaporView is a Qt 6 / C++17 desktop application with a QML front end. It connects to EPSILON GNSS/INS, PTB210 barometer, HMP3 humidity/temperature sensor, TFA1500-L lidar, RTK/NTRIP streams, and a local TCP waveform stream. The app provides live monitoring, RTK forwarding, session recording, offline session viewing, raw data parsing, and trajectory/waveform analysis.

## Required Workflow After Code Changes

Every code change in this repository must be verified and published in this order:

1. Build locally from the Visual Studio developer environment:
   ```powershell
   call "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
   cd /d X:\Project\GPS\VaporView-QML\build\Release
   cmake --build . --config Release
   ```
2. Commit the changed files from the repository root.
3. Push to `origin QML`.

Do not skip the build. Do not push before the commit succeeds.

## Common Commands

Windows release configure/build through the project script:

```powershell
$env:VAPORVIEW_QT_MSVC_PREFIX = "<local Qt 6 MSVC prefix>"
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-msvc2022.ps1 -Action Rebuild
```

Linux ARM64 release build:

```bash
./scripts/build-linux-arm64.sh rebuild
```

Manual CMake presets:

```bash
cmake --preset windows-msvc2022-x64-release
cmake --build --preset windows-msvc2022-x64-release
ctest --preset windows-msvc2022-x64-release
```

Run the current single CTest target from an existing `build/Release` tree:

```bash
ctest -R tcp_wave_encoding_test --output-on-failure
```

Run the TCP waveform simulator:

```powershell
python scripts/mock_tcp_waveform_sender.py --host 0.0.0.0 --port 8888 --samples 50000 --rate 10
```

Probe EPSILON serial data:

```powershell
python tools/epsilon_serial_probe.py --port COM3 --baud 921600 460800 115200 --duration 3 --show-frames
powershell -ExecutionPolicy Bypass -File tools/epsilon_serial_probe.ps1 -Port COM3 -BaudRates 921600,460800,115200 -ShowFrames
```

Recover EPSILON main port:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/recover_epsilon_main.ps1 -Ports COM3 -Bauds 921600,115200,230400,460800
```

## Build System

- `CMakeLists.txt` defines the `VaporView` executable, QML module `VaporView`, static `rtklib_strsvr`, optional `str2str`, and `tcp_wave_encoding_test` when `BUILD_TESTING` is enabled.
- CMake requires Qt 6 components: `Core`, `Qml`, `Quick`, `QuickControls2`, `SerialPort`, and `Network`.
- QML files under `qml/` are collected with `qt_add_qml_module(... URI VaporView ...)`.
- `BUILD_PYTHON_BINDINGS` defaults to `OFF`; enabling it currently fails unless `python/bindings.cpp` is added.
- The repository convention is `build/Release` for local verification artifacts.

## High-Level Architecture

### Application entry and QML bridge

- `src/main.cpp` creates the Qt application and loads the QML UI.
- `src/VaporViewBackends.cpp` and `include/VaporViewBackends.h` expose backend state and actions to QML. This is the main integration layer between UI pages and C++ services.
- `qml/Main.qml` is the shell; `qml/pages/` contains feature pages; `qml/components/` contains reusable controls such as cards, metric tiles, waveform canvas, navigation rail, combo boxes, and toolbar buttons.

### Device and data acquisition layer

- `src/serial_port.cpp` implements the repository's cross-platform serial abstraction. Qt SerialPort is used for enumeration, while acquisition uses `VaporView::SerialPort`.
- `include/data_collector.h` and `src/data_collector.cpp` define the collectors for EPSILON FDILink, PTB210, HMP3 Modbus RTU, TFA1500-L lidar, plus retained GNSS/IMU collector code.
- Shared data structures live in `include/data_types.h`.
- Third-party protocol code is compiled from `third_party/um982_driver`, `third_party/hipnuc_driver`, and `third_party/rtklib`.

### RTK and waveform services

- `src/RtkStreamService.cpp` wraps RTKLIB stream server behavior for NTRIP input and serial/TCP-client output configuration.
- `src/RtkConfigDialog.cpp` contains the legacy/widgets RTK dialog implementation; QML-facing behavior is mediated through the backend layer.
- `src/TcpWaveEncoding.cpp` contains reusable TCP waveform encoding/decoding logic covered by `tests/tcp_wave_encoding_test.cpp`.
- `src/TcpWavePanel.cpp` contains the legacy/widgets waveform panel logic. QML waveform rendering uses `qml/components/WaveformCanvas.qml` and related pages.

### Offline data tools

- `src/SessionViewerWindow.cpp`, `src/RawDataParserWindow.cpp`, and `src/TrajectoryViewerDialog.cpp` provide legacy/widgets offline viewing and parsing windows still built into the application.
- Session/raw format documentation is in `docs/`, especially `docs/raw_dat_format.md`.
- Recorded runtime data goes under `data/` and is ignored by git.

## UI Development Notes

- Prefer changing QML files for visual/layout behavior and `VaporViewBackends` for QML-facing state/actions.
- Keep C++ device, RTK, recording, and parser logic out of QML unless it is only presentation glue.
- After frontend changes, build the app and, when feasible, launch the built executable to exercise the changed page. If UI verification is not possible, state that explicitly.

## Repository Notes

- `scripts/*.local.ps1`, `build/`, and `data/` are local-only/ignored artifacts.
- There is no top-level project license file; `third_party/rtklib/LICENSE.txt` applies to bundled RTKLIB sources.
- `README.md` is broad project documentation. Keep `CLAUDE.md` focused on commands, architecture, and Claude workflow guidance rather than duplicating the full README.
