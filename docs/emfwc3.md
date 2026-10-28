# EM-FWC3 USB motion control

VaporView controls an LBTEK EM-FWC3 through a USB virtual COM port on the
computer running the ground application. This session is independent of the
Direct/FPGA sensor path and Local/Remote acquisition source. It does not use
FPGA or SkyCore forwarding, and acquisition connect/disconnect does not open
or close the motion controller.

## Runtime SDK

The implementation uses the C ABI from the manufacturer's EM-FWC3 development
package V1.0.1, `controllibrary.h`, `devices/emfwc3/emfwc3.h`, and the supplied
DLL interface reference V1.0. Vendor DLLs are external dependencies, not Git
artifacts. On Windows x64, configure with:

```powershell
cmake --preset windows-msvc2022-x64-release -DVAPORVIEW_EMFWC3_SDK_DIR="<SDK>/LBTEK_EM-FWC3_C++sdk/libs/x64"
cmake --build build/Release --config Release --target VaporView
```

Run these commands in the VS/MSVC Developer environment. The configured
directory must contain `LB_Control_Library_x64.dll`, `lubonCore.dll`, and
`libusb-1.0.dll`. CMake stages them in `build/Release/emfwc3` and installs them
in `bin/emfwc3`. Without the optional SDK, the rest of the application still
builds and runs; attempting a motion connection reports the loader error.
The runtime environment variable `VAPORVIEW_EMFWC3_SDK_DIR` can override the
default application-relative `emfwc3` directory. Other platforms retain the
page but require an appropriate manufacturer runtime before connection.

## Operation

1. Open **Device configuration → Local direct devices · EM-FWC3**.
2. Select the actual COM port. Port numbers may change between computers or
   USB sockets; CH340 identifies the bridge, not uniquely the attached product.
3. Check each channel's physical motor type (EM-FPM or CEM-FPM; the confirmed
   project hardware defaults to CEM-FPM on all three channels), then explicitly
   confirm the three types. They are used by the SDK's angle conversion.
4. Connect. Success requires initialization, serial open, motor-type setup,
   three finite position responses, and a speed response. Connection never
   homes, moves, changes speed, clears the origin, or writes EEPROM.
5. Open **Motion control** from the sidebar, after **Thermal**, or from the
   configuration panel. Channels 1–3 map to SDK motor indices 0–2.

Each channel provides absolute positioning, relative steps, homing, continuous
motion, and stop. Homing and continuous motion require explicit confirmation.
The stop controls remain available while another command is pending. Calls
are serialized on a dedicated worker thread; a stop waits for the in-flight
SDK call to return. **Stop all** attempts all three channels even if a previous
stop fails; it is a software command, not a hardware emergency stop.

Speed and EEPROM position saving are device-wide operations because these
SDK functions do not accept a motor index. Saving requires confirmation and
is never automatic. Verify speed units, permitted speed, and mechanical
travel against the actual fitted hardware; numeric input bounds are not
mechanical limit protection. No XYZ/attitude meaning is assigned to channels.

Position readback is polled every 500 ms, subject to SDK response time. A
command returning zero means it was sent successfully, not that the mechanism
has arrived. The supplied EM-FWC3 API has no separate motion-complete/limit/
alarm query; the UI reports angles without inventing hardware status. Invalid
or nonfinite readings display as unavailable. Three failed polling cycles
close the session and invalidate all readings. No automatic reconnect or
automatic re-homing occurs.

Disconnect, application shutdown, and connection-loss cleanup attempt to stop
channels commanded to move by this session, then close the device and release
the SDK. Read-only connections send no stop command on close. UI test mode
cannot start while a real motion session is open or connecting, and its write
barrier blocks motion connections/commands. Port and model choices use the
normal settings write barrier; tests isolate settings with the shared sandbox.

## Verification

`emfwc3_session_test` loads an isolated C-ABI test DLL to exercise channel
routing, relative/absolute commands, speed readback, explicit EEPROM writes,
SDK errors, invalid inputs, nonfinite feedback, failed handshakes, lost-device
cleanup, and stopping owned movement on shutdown.

`emfwc3_ui_test` uses that same isolated SDK to verify explicit model
confirmation, startup without motion, operation from the real main window,
independence from Direct/FPGA switching, page navigation, the UI test barrier,
and channel layout at 100%/130% font scales in light/dark themes.

These automated tests do not access physical hardware. Actual mechanism
movement and physical travel limits require a separate operator-controlled
bench check.

For an explicit read-only bench check using the production session, run:

```powershell
build/Release/emfwc3_session_test.exe --hardware-readonly "<absolute SDK directory>" <COM port> CEM-FPM
```

This opt-in mode initializes the SDK, opens the port, sets the confirmed motor
types, reads all three angles and the common speed, then disconnects and
releases the SDK. It never issues a move, home, stop, speed change, or EEPROM
write. Ordinary CTest runs use the isolated test DLL instead.
