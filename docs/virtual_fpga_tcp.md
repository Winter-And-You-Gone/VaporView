# VaporView virtual FPGA TCP

Start the VirtualLab TCP server on IPv4 loopback, then start VaporView:

```powershell
.\build\Release\VaporView.exe --virtual-fpga
```

The default endpoint is `127.0.0.1:39181`. To select a different local port, add
`--virtual-fpga-port <port>`. Start a new VaporView process without these flags for
normal hardware operation. The FPGA page displays **Virtual FPGA TCP** and offers
Refresh and Connect if the server was unavailable at startup.

The server receives and sends raw VLP1 wire frames, including the CRC and the
existing zero padding to a four-byte boundary. TCP reads can contain a partial
frame or several frames. There is no extra envelope or socket handshake. The
startup sequence uses the existing PING, capability query, and register reads.
USB vendor control requests are unavailable through this transport.

Virtual mode pins the device route to the virtual FPGA and uses the usual FPGA
controls, telemetry and recording. Recordings use the `virtual_fpga_sessions`
subdirectory of the configured recording directory. Keep the VirtualLab listener
bound to `127.0.0.1`.

## Live XSI acquisition regression

Build `fpga_main_window_test` in `build/Release` using the MSVC developer
environment. Start a fresh VirtualLab instance with a unique loopback port and
runtime directory. The Vivado 2025.1 diagnostic run uses the Lab ADC history scenario to avoid the partial-cycle flag raised by the default full-scale stimulus.

```powershell
& D:\Project\VaporVirtualLab\run.ps1 -BuildDir D:\Project\VaporVirtualLab\build\diagnostic-2025.1-full -RuntimeDir "$env:TEMP\vaporlab-xsi-39197" -AllowDiagnosticVersion -Headless -Port 39197 -RunForMs 360000 -Scenario D:\Project\VaporVirtualLab\tests\e2e\adc_history_scenario.json
```

After `VIRTUAL_LAB_READY`, run the test from the VaporView checkout:

```powershell
.\build\Release\fpga_main_window_test.exe --xsi-port 39197 --xsi-acquisition
```

The test configures a short continuous scan, enables the virtual DACs, and
checks RAW previews and completed DLIA cycles from both lanes. It reopens the
recorded harmonic waveform through the session reader and verifies shutdown.
The test prints the retained recording directory under the system temporary
folder. It uses an isolated synthetic calibration fixture for recording, and
the waveform data comes from XSI. Omit `--xsi-acquisition` to run the connection
and register checks alone.
