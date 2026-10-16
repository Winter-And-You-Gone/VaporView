# Architecture execution plan

Baseline: `8f39761` (2026-10-16). Scope approved by the user: architecture review
items 2–5. Authentication and TCP connection takeover policy are excluded.

## Delivery stages

1. Command identity and retries: deduplicate mutating commands within a client
   connection, reject identity/payload conflicts, retain bounded results, and
   leave status queries fresh. Verify lost-ACK retries and reconnection isolation.
2. Asynchronous control: move legacy temperature hardware transactions off the
   SkyCore event loop; make Ground TCP connection establishment asynchronous.
   Verify responsiveness, cancellation, readback, and shutdown ownership.
3. Window boundary: move remote configuration transaction state and FPGA session
   routing/lifecycle into widget-free services. Keep page composition, labels,
   and field-to-model mapping in the window. Verify stale completion rejection,
   mode switching, and existing GUI integration behavior.
4. Build boundary: share serial/TCP telemetry transports without a dependency on
   SkyRuntime; support independently selected Ground, SkyCore and TUI products.
   Verify a Core-only configuration without Widgets and the normal full build.
5. Delivery: update architecture documentation; validate in `build/Release`,
   first with osgEarth OFF and finally ON; run focused checks followed by full
   CTest, inspect staged files, commit, and push the scoped change to origin/main.

Pre-change baseline GUI failures were `combination_navigation_page_test`
(overview title visibility) and `main_window_map3d_open_test` (immediate language
change). Record these separately from regressions introduced by this work.

Wire formats, existing Local/Remote and Direct/FPGA choices, application settings,
recording semantics, and visible page behavior are compatibility requirements.

## Implementation and validation (2026-10-16)

Stages 1–4 are implemented. The desktop uses asynchronous TCP connection and
cancellation; RD105 I/O uses the Core device worker; remote configuration and
FPGA session ownership have moved into widget-free services. Ground services
link shared transport/collector libraries instead of SkyCore.

All validation used `build/Release` and the MSVC Developer environment with
UTF-8 code page 65001:

- Core-only, TUI-only and Ground-only configurations built successfully with
  tests disabled and osgEarth OFF. Core-only Qt deployment dependencies were
  Core, Network and SerialPort, with no Widgets requirement.
- With all products enabled and osgEarth OFF, all five focused tests passed:
  command execution, TCP telemetry, device control sessions, device configuration
  layout, and FPGA main-window integration.
- With all products restored and osgEarth ON, the full build succeeded using
  `cmake --build build/Release --config Release --parallel 2`.
- Full CTest (`--parallel 2 --timeout 180`) completed with 137 passes, one
  elevated-parent test skipped, and two map test failures. The navigation layout
  test that failed in the pre-change baseline passed in this run.
- Final cancellation wiring was rebuilt successfully. Its focused
  `device_config_layout_test` passed, including cancel-before-open and reconnect.
  Both failing map tests were rerun serially and still failed:
  `map3d_real_window_close_test` waited 10 seconds for the initial map, and
  `main_window_map3d_open_test` exceeded its one-second action threshold
  (1040 ms). These are unresolved verification failures; the map implementation,
  map resources and those two test sources were not changed by this work.

Final configuration keeps all three products, tests and osgEarth enabled.
Physical checks below remain pending; simulation does not establish hardware
write/readback or latency behavior.

## Physical verification

Software regression uses simulated devices and loopback TCP peers. No physical
RD105 or FPGA board is used for this change. During the next hardware session:

- Verify RD105 write/readback and a delayed or missing response while SkyCore
  heartbeat/status traffic continues; include address/baud changes and restore
  the original device settings afterwards.
- Disconnect/reconnect telemetry during a pending operation and confirm that old
  results do not update the new session or trigger an automatic hardware write.
- Verify USB acquisition and recording stop cleanly when the desktop closes;
  continue using `fpga_hardware_validation.md` for board-level validation.
