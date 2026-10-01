# EPSILON2-D4G Session PPK

VaporView records `MSG_RAW_SATELLITE` (`0x77`) as a formal navigation source.
Its default rate is the shared `Ppk::kDefaultObservationRateHz` (5 Hz), independent
of the main EPSILON output rate. Local configuration, Remote operations,
SkyConfig JSON, Ground settings and simulated devices support the same packet.
Existing configurations without this packet inherit 5 Hz.

The packet-rate bandwidth display includes the 0x77 single-observation minimum.
Complete epochs have variable satellite/frequency counts and span multiple
frames; their actual serial load must be measured rather than treating that
minimum as a full-epoch estimate. The default remains enabled at 5 Hz.

## Device and protocol sources

The device authority is EPSILON usage manual V1.2 (2025-04-24), sections 9.10,
11.2.35, 11.3, 11.4.5–11.4.6 and 11.5.12, together with the EPSILON2 D4G
datasheet. The January 2026 vendor MCU FDILink example supplies the packed
satellite/frequency record layout.

Main antenna capability: GPS L1/L2/L5, BDS B1I/B2I/B3I, GLONASS L1/L2,
Galileo E1/E5a/E5b and QZSS L1/L2/L5. The secondary antenna has GPS L1/L2,
BDS B1I/B2I/B3I, GLONASS L1/L2 and QZSS L1/L2; secondary observations are
preserved separately by receiver number.

FDILink uses little endian fields, CRC8 over the first four header bytes and
CRC16 over the payload. The parser validates both checksums and exact lengths.
It accepts the vendor's repeated 32-byte satellite/frequency records and the
manual's satellite header followed by multiple frequency records. All raw
fields, including clock offset, tracking, elevation and azimuth, are preserved.

The assembler keys epochs by receiver and UTC nanoseconds, accepts out of order
packets and detects duplicate, missing, conflicting and regressed epochs. It
supports contiguous zero or one based packet numbering, which the manual does
not explicitly distinguish. Incomplete epochs are logged and never published
as complete observations. Raw frames remain available for diagnosis.

## Recording and portable Session files

```text
session/
  raw/navigation.dat                 original FDILink frames, including 0x77
  sensors/sensor_summary.csv         original measurements/navigation
  ppk/
    rover/observations.bin           streamed complete observation epochs
    rover/attitudes.bin              device UTC + Session time + quaternion
    rover/metadata.json              recording UTC range/count/format
    rover/rover.obs                  generated RINEX 3.04
    rover/rinex_metadata.json        mapping, receiver, unmapped count/recovery
    base/base.obs                    archived Base observations
    nav/<imported filename>          archived broadcast navigation files
    solution.pos                    RTKLIB antenna solution
    trajectory.csv                  standardized, IMU corrected navigation
    ppk_config.json                 reproducible processing/source settings
    ppk_quality.json                processing status and quality summary
```

The compact observation format starts with `VVPPKOBS` and little endian version
1 (12 bytes total). Each epoch is a length/checksum/payload record. The payload
uses Qt 6.0 QDataStream, little endian, and stores UTC seconds/nanoseconds,
receiver clock offset, receiver number, host timestamp and the complete
observations. Carrier phase is cycles, pseudorange metres, Doppler Hz and SNR
dB-Hz. IEEE double precision preserves values through persistence; Doppler/SNR
originating as float32 retain their original value. Readers recover complete
records before an incomplete crash tail and reject corrupted complete records.

Attitudes use `VVPPKATT`, version 1, followed by 48-byte records: UTC nanoseconds,
Session timestamp microseconds and four double quaternion components (w,x,y,z).
They are generated from UTC and attitude in the same `0x50` System State packet.
An incomplete final attitude record is ignored during recovery.

Local recording uses the bounded raw recording worker, independent of GUI
updates. Sky forwards complete observations through its bounded raw event queue.
RINEX conversion happens when processing starts, in the PPK worker. A separate
PPK recording switch is unnecessary. Sessions without PPK files still open with
Original navigation.

## Session workflow

Open a Session in the Data Viewer, click **PPK Processing**, then select Base OBS
and one or more Navigation RINEX files in the dedicated PPK window. The main
Data Viewer only shows PPK status (with FIX percentage after completion) and the
current navigation source in Data Summary. Files are validated and copied into
the Session. EPSILON `XXX-BASE.NAV`
and CORS navigation files use the same import path. Recognized companion NAV
files beside imported Base OBS are archived automatically; navigation already
readable within Base input is also passed to RTKLIB. No ephemerides are fabricated
from `0x77`.

The PPK window shows Rover/Base/NAV availability, readiness, processing progress,
failures, FIX/FLOAT counts and percentages, UTC start/end, sample count, RMS of
the reported N/E/U standard deviations and solution path. It supports running,
rerunning, cancellation and clearing results. Clearing preserves inputs and
Original navigation. All controls follow the existing Session language switch
and Light/Dark theme. Repeated clicks reuse the window; closing it preserves
the selected navigation source and any running worker. Reopening restores its
current state. Opening or reloading a Session synchronizes the PPK window, and
Clear Page leaves it in No Session state (the retained path can still be reloaded).
During processing or input import, Open Data, Reload and Clear Page are disabled;
waveform and device data browsing remain available. Closing the Data Viewer
cancels and joins its PPK worker. Navigation source changes continue through
the shared Session navigation events used by 3D, Heat, Sensor and Export.

Main Antenna Lever Arm is snapshotted from the existing Ground RTK settings or
SkyConfig when recording begins. The Session keeps its own copy, so later device
configuration changes do not change historical positioning. Receiver selection
defaults to 1 and is explicit in the panel/configuration; select the actual main
receiver number present in the recorded data. Frequency count, elevation mask,
enabled constellations and body lever arm remain reproducible Session settings.

## RINEX mapping policy

`epsilonRinexSignal()` is the single tested mapping policy. The manual does not
uniquely identify constellation-specific signal components. The implementation
records the following D4G policy in both processing and RINEX metadata:

| System | EPSILON frequency → RINEX signal |
| --- | --- |
| GPS | 0→1C, 1→1X, 2→1W, 4→2X, 5→2W, 7→5X |
| GLONASS | 0→1C, 2→1P, 4→2C, 5→2P |
| BDS | 0→2I, 4→7I, 7→6I |
| Galileo | 0→1X, 7→5X, 8→7X |
| QZSS | 0→1C, 1→1X, 4→2X, 7→5X |

GPS indices follow the manual; other systems use the explicit D4G band ordering
policy where the manual omits a per-system table. `X` represents combined signal
components when they cannot be distinguished. Unmapped signals are counted and
remain in the compact recording. Undocumented tracking bits are never presented
as known cycle slips; original tracking bytes remain available. GLONASS channel
numbers are obtained from supplied navigation data, not guessed from 0x77.

RINEX epochs use GPST, obtained from observation UTC with RTKLIB leap seconds.
The header states the time scale. Pseudorange, phase, Doppler and SNR are emitted
for all mapped systems/frequencies. Original receiver clock offset remains in
the recording; RTKLIB estimates receiver clocks during positioning.

## Internal RTKLIB and navigation reference

VaporView calls the bundled RTKLIB 2.4.3 b34 `postpos()` library API in Kinematic
mode. No external solver executable is required. The upstream solver's static
state is serialized; processing cancellation uses RTKLIB's callbacks. UI progress
is bounded to percentage changes. See `third_party/rtklib/UPSTREAM.md` for the
pinned source and BSD license. Observation ABI definitions are shared with every
RTKLIB consumer.

A Session processing lock prevents duplicate runs or clearing in-flight results.
Waiting for another Session's solver remains cancellable. Source selection only
checks completed metadata on the GUI thread; background loaders validate and
read the full standardized trajectory.

The solution reader creates `PpkSample`/`PpkTrajectory`, including quality 1 FIX,
2 FLOAT and other legal RTKLIB qualities, UTC, Session time, LLH/ECEF, satellite
count, N/E/U standard deviations, age and ratio. UI/renderers consume this model,
not RTKLIB text.

Device UTC is mapped to Session time using recorded UTC/host pairs. Host time is
not substituted for GNSS time. Attitude uses normalized quaternions and shortest
path slerp; Euler angles are never interpolated across their wrap. Attitude gaps
above two seconds and extrapolation are rejected. Body axes are X forward,
Y right, Z down; body attitude rotates into NED. The main antenna lever arm points
from IMU to antenna. The correction is:

`P_IMU = P_ANT - R_body_to_NED * LeverArm_IMU_to_ANT`

NED lever offsets are converted through ENU into ECEF before subtraction.
`trajectory.csv` explicitly records `source=PPK`, `reference_point=IMU` and WGS84
ellipsoidal height. Reported solver standard deviations describe antenna solution
uncertainty and do not add a model for lever-arm/attitude uncertainty.

## One navigation selection for rendering, sensors and export

`SessionNavigationResolver` is the shared coordinate boundary used by both
SessionLoader and Geo SessionTrackReader. Selecting Original or PPK is persisted
in `ppk_config.json` and broadcasts a reload to Session and 3D windows. PPK is
available only with a completed, readable trajectory.

PPK positions are interpolated in ECEF at each sensor's Session timestamp.
Track points, heat/render samples, replay and trajectory export therefore use the
same corrected navigation. Sensor measurement values and original files are
unchanged. Uncovered timestamps have no position, rather than an implicit mixed
trajectory. Export includes source/reference columns. Switching back restores
the original position values.

## Verification and hardware boundary

`epsilon_raw_satellite_test` covers hand-built CRC-valid frames, exact binary
fields, five systems, multiple frequencies, packet reordering/duplication/loss,
receiver separation, UTC regression and crash-tail recovery.

`ppk_pipeline_test` wraps real upstream RTKLIB observations in multi-packet 0x77
frames, passes them through EpsilonCollector and both formal recorders, reopens
Session observations, writes/read-checks multi-system RINEX, runs actual Kinematic
PPK with archived Base/NAV, corrects the IMU lever arm and checks unified
Original→PPK→Original track/heat/export positioning. Separate analytic cases
cover zero/horizontal/vertical lever arms, 90-degree yaw, time interpolation and
yaw wrap. Fixtures and provenance are in `tests/fixtures/ppk`.

`session_ppk_ui_test` runs the real solver through the UI worker and checks
responsiveness, cancellation, readiness, languages and source selection.
`sky_device_manager_simulation_test` covers Remote 0x77=5 Hz and old SkyConfig.

This software validation uses published observations and simulated/fixture
FDILink, without claiming a connected D4G field survey. Before field delivery,
check actual receiver numbering, D4G firmware signal indices against the recorded
mapping policy, tracking bits, measured 5 Hz complete epochs, `#fmsg` save/readback,
GNSS UTC validity and surveyed main-antenna arm. Collect paired Base/NAV and
compare known checkpoints. Keep credential-bearing recordings/logs outside Git.
