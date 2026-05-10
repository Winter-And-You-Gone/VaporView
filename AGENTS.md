# VaporView-QML Agent Rules

## Project Overview

Qt6 QML/C++ application for GPS/GNSS sensor data collection, waveform visualization, and RTK streaming. The UI uses QML (Qt Quick Controls Basic) with C++ backends exposed via `QQmlContext::setContextProperty`. The project is migrating from legacy Qt Widgets code (still in `src/` but unused). Build system: CMake 3.16+ with Ninja, C++17.

## Build Commands

```powershell
# Windows — configure + build (Requires: VS2022, Qt6 MSVC, vcvarsall x64 in PATH)
# Option A: PowerShell build script (handles environment setup)
.\scripts\build-windows-msvc2022.ps1 -Action Build
.\scripts\build-windows-msvc2022.ps1 -Action Rebuild

# Option B: CMake presets (must run from VS dev prompt with vcvarsall x64)
cmake --preset windows-msvc2022-x64-release
cmake --build --preset windows-msvc2022-x64-release
```

```bash
# Linux ARM64
./scripts/build-linux-arm64.sh build
./scripts/build-linux-arm64.sh rebuild
```

**Exe location:** `build/Release/VaporView.exe` (Windows) or `build/Release/VaporView` (Linux).

**Important:** QML-only changes get packed into rcc resources and may not trigger cmake re-link. If the exe timestamp hasn't changed after `cmake --build`, do a clean rebuild: delete `build/` and reconfigure.

## Test Commands

```powershell
# Run all tests (Windows)
ctest --preset windows-msvc2022-x64-release

# Run a single test
ctest --preset windows-msvc2022-x64-release -R tcp_wave_encoding_test

# Or via the build script
.\scripts\build-windows-msvc2022.ps1 -Action Test
```

Only one test exists: `tests/tcp_wave_encoding_test.cpp`. It's a plain C++ binary (no test framework) that returns 0 on success, 1 on failure. Tests are only built when `BUILD_TESTING=ON` (enabled by `include(CTest)`).

## Lint / Format

No `.clang-format`, `.clang-tidy`, or `.editorconfig` files exist. Code quality is enforced via compiler flags:
- MSVC: `/W4 /permissive- /utf-8 /Zc:__cplusplus`
- GCC/Clang: `-Wall -Wextra -Wpedantic`

Always ensure new code compiles cleanly under these flags. Do not introduce shadowed variables, implicit fallthrough, signed/unsigned mismatches, or unused parameters.

## Mandatory Workflow

每次修改代码后，**必须**执行以下完整流程，缺一不可：

1. **Build** — 使用 PowerShell 脚本或 cmake presets 构建（见上方 Build Commands）
2. **Verify** — 确认 `build/Release/VaporView.exe` 时间戳已更新；若未变化则 clean rebuild
3. **Commit** — `git add -A && git commit -m "<中文提交信息>"` 在仓库根目录执行
4. **Push** — `git push origin QML`
5. **Report** — 推送完成后向用户汇报：改了什么、commit hash、推送结果

Always use PowerShell (not Bash) for vcvarsall setup to avoid swallowed cmake output.

**注意：** 本地提交只能使用以下格式的 commit message：
```
<type>: <中文描述>
```
其中 type 为以下之一：`feat`(新功能)、`fix`(修复)、`refactor`(重构)、`style`(样式)、`chore`(杂项)。

## C++ Code Style

### Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Classes / Structs | PascalCase | `DeviceBackend`, `RtkStreamConfig` |
| Enums (class) | PascalCase | `TcpFloatEncoding::LittleEndian` |
| Functions / Methods | camelCase | `connectToHost()`, `refreshPorts()` |
| Q_PROPERTY bindings | camelCase | `host`, `connected`, `language` |
| Member variables | snake_case + trailing `_` | `recording_directory_`, `is_english_` |
| Local variables | snake_case | `device_backend`, `raw_payload` |
| Constants (file-scope) | `k` prefix + PascalCase | `kFloatSize`, `kMaxTcpPayloadSize` |
| Include guards | `VaporView_FILENAME_H_` or `VAPORVIEW_FILENAME_H_` | Both styles exist, prefer `VaporView_FILENAME_H_` |

### File Organization

- **Headers:** `include/` — one class per file for small classes; `VaporViewBackends.h` is the exception (all QML backend classes, ~1060 lines)
- **Sources:** `src/` — `.cpp` file per module
- **Third-party:** `third_party/` — vendored, never modify casually
- **File naming:** PascalCase for class files (`VaporViewBackends.h/.cpp`), snake_case for utility modules (`serial_port.h/.cpp`, `data_types.h`)

### Include Order

Qt headers first, then standard library, then project headers. Always alphabetize within each group.

```cpp
#include <QObject>
#include <QVector>

#include <atomic>
#include <thread>

#include "serial_port.h"
#include "VaporViewBackends.h"
```

### Namespaces

All project code lives in `namespace VaporView { }`. Data structs, enums, and utility functions go here. QML backend classes (`AppBackend`, `DeviceBackend`, etc.) are in the global namespace per Qt MOC requirements — they reference `VaporView::` types explicitly.

### Qt Patterns

- Every QObject-derived class must have `Q_OBJECT` macro
- QML-exposed properties use `Q_PROPERTY(... READ ... WRITE ... NOTIFY ...)`
- QML-callable methods use `Q_INVOKABLE`
- Use `signals:`, `public slots:`, `private slots:` sections
- Lambda-based connections: `QObject::connect(&sender, &Sender::signal, this, [&]() { ... })`
- `QStringLiteral("literal")` for compile-time strings
- `QLatin1Char('/')`, `QLatin1String("info")` for ASCII literals
- `QVariantMap`, `QVariantList`, `QVector<float>` for QML data exchange
- `QByteArray` for binary payloads
- `QSettings` (without organization/app args) for persisting UI state

### Error Handling

- Functions return `bool` to indicate success/failure
- `QString *errorMessage` out-parameter pattern for detailed errors
- Data structs have `bool valid` and `std::string error_message` fields
- Backend classes emit `notificationRequested(level, message)` signal for UI notifications
- TCP/socket errors go through `socketError(message)` signal

### Thread Safety

- `std::atomic<T>` for shared bools, ints, and counters
- `std::mutex` / `QMutex` for protecting complex shared data
- `QThread` with `moveToThread()` for event-loop workers
- `std::thread` for non-Qt blocking operations
- `cancel_requested_` atomic flag for cooperative cancellation
- Copy constructors/assignment deleted or move-only where appropriate

### Memory Management

- Qt parent-child ownership for QObject-derived classes
- `std::unique_ptr` for exclusive ownership (e.g., `QFile` handles)
- `std::shared_ptr` for shared collector objects
- `= delete` on copy, `= default` / `noexcept` on move
- PIMPL idiom used in `RtkStreamService`

### Third-Party Code

- **RTKLIB** (`third_party/rtklib/`): compiled as `rtklib_strsvr` static lib with `/W0` (warnings suppressed)
- **HiPNUC IMU driver** (`third_party/hipnuc_driver/`): C code, compiled into main binary
- **UM982 GNSS driver** (`third_party/um982_driver/`): C++ code, compiled into main binary
- Do not modify third-party source files

## QML Code Style

### Imports

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"
import "pages"
```

Always use `QtQuick.Controls.Basic` (not Material, Fusion, etc.).

### Naming

- QML files: PascalCase matching component name (`NavigationRail.qml`, `MetricTile.qml`)
- Component `id`: camelCase matching the filename (`navigationRail`, `metricTile`)
- Properties: camelCase (`currentPage`, `selectedSession`)
- `readonly property` for derived/computed values

### Theme & Styling

Dark/light theme via `appBackend.dark` boolean. Define semantic color properties on the root `ApplicationWindow` (`bg`, `card`, `border`, `text`, `muted`, `primary`, `danger`, etc.) and reference them throughout. Color values: dark palette uses slate tones (`#020817`, `#1e293b`, `#94a3b8`), light palette uses matching light tones (`#ffffff`, `#f1f5f9`, `#64748b`).

Font scaling: `font.pixelSize: baseSize * ApplicationWindow.window.scaleFactor` where `scaleFactor` = `appBackend.fontScale / 100`.

### Backend Access

C++ backends are injected as context properties in `main.cpp`:
```cpp
engine.rootContext()->setContextProperty("appBackend", &app_backend);
engine.rootContext()->setContextProperty("deviceBackend", &device_backend);
// ... waveformBackend, recordingBackend, rtkBackend, sessionBackend,
//     rawParserBackend, settingsBackend
```

Access from QML: `deviceBackend.ports`, `waveformBackend.connectToHost()`.

### Translation

```qml
function t(key) {
    appBackend.language  // force re-evaluate on language change
    return appBackend.t(key)
}
```

### Components

Use `component Name: Type { }` syntax for inline page definitions. Reusable components go in `qml/components/`, pages in `qml/pages/`. Components should be self-contained and accept configuration via properties.

## Architecture

QML UI ↔ C++ Backends (AppBackend, DeviceBackend, WaveformBackend, RecordingBackend, RtkBackend, SessionBackend, RawParserBackend, SettingsBackend) ↔ Data Collectors (Epsilon, Ptb, Hmp, Lidar) ↔ Serial Port / TCP ↔ Third-party (RTKLIB, HiPNUC IMU, UM982 GNSS).

Prefer the QML layer over the legacy Widget code (`MainWindow.cpp`, `TcpWavePanel.cpp`, etc.) — they are deprecated.

## Communication Language

Use Chinese (Simplified / 简体中文) for all responses, explanations, commit messages, and code comments. Code identifiers (variable names, function names, class names) remain in English per the conventions above.

## Network & Proxy

网络操作（如 `git push`、WebFetch）出现连接失败时：

1. **优先尝试本地代理** — 设置 git HTTP/HTTPS 代理到 `127.0.0.1:7890`：
   ```
   git config http.proxy http://127.0.0.1:7890
   git config https.proxy http://127.0.0.1:7890
   ```
   然后重试操作。
2. **代理无效时** — 向用户汇报网络状况，等待用户指示。
