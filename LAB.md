# OpenHantek Lab

Experimental fork of OpenHantek6022. Upstream remains
https://github.com/OpenHantek/OpenHantek6022; this fork is
https://github.com/CargoCultScientist/OpenHantek6022.

## Correctness baseline

- Opening a setup never clears or rewrites it. Unsupported setup versions are
  rejected; unsupported automatic device settings are retained with autosave off.
- Calibration corrections are local-only, retain full precision and retain their
  EEPROM-baseline selection across unplug/reconnect. Shutdown never writes factory
  calibration EEPROM. Clipped, noisy and excessive-offset calibration is rejected.
- CSV omits locale grouping separators, quotes channel names and leaves missing
  cells empty. JSON escapes names, uses null for missing/non-finite values and stops
  the frequency axis at the available spectrum. Both save atomically and report errors.
- FFT plans are rebuilt for their exact length, including odd lengths, and planning
  cannot erase the first input. THD uses linear power before display-floor clipping.
- SINGLE accepts triggers only from an acquisition started after arming. Settings
  changes while armed invalidate in-flight captures. Queued analysis owns its samples.
- Command objects have a virtual destructor for safe shutdown. Demo devices reuse
  the registered model rather than leaving dangling registry entries.

## Build and test (Linux)

Requires a C++17 compiler, CMake, Qt6 Widgets/OpenGLWidgets/PrintSupport/Test, FFTW3
and libusb development packages. Qt LinguistTools is optional (English-only without it).

```console
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
build/openhantek/OpenHantekLab --demoMode
```

The binary and settings organization are `OpenHantekLab`. Upstream settings and
calibration files are not automatically imported or modified. Explicitly open a
compatible setup to reuse it. Existing installations remain untouched. For this
development phase, run the binary directly; upstream packaging assets are not a
validated Lab installer. Windows/macOS are not yet validated.

These tests use synthetic samples and temporary configuration. Passing them is not
hardware certification: real USB timing, unplug/reconnect and SINGLE rearming still
need bench validation. The existing acquisition/display pipeline is not gapless.

## Capture history, reference comparison and measurements

The correctness-only release is on the fork's `main` branch. The additions below
are on `feature/correctness-and-captures`; they are not silently bundled into the
correctness-only release candidate.

Open **View → Capture history & measurements** (`Ctrl+H`). The scope above remains
live when selecting a historical record below. **Follow latest** returns the browser
to the latest acquisition. **Record history** pauses/resumes retention without stopping
the scope. Clear history also resets the statistics; a pinned reference survives it.

- History holds at most 128 displayed acquisitions or 64 MiB of sample/metadata
  payload. A selected record and pinned reference may retain up to two additional
  records (each limited to one million samples). This is not a continuous recorder.
- Set reference pins the selected record. Solid traces are the selection; dashed
  traces are the reference. Channels share an amplitude scale only with the matching
  reference channel; different units are not compared. The default aligns record
  starts. Trigger alignment requires both captures to be triggered. A manual shift
  moves the reference in seconds. Differences use linear interpolation within overlap,
  never extrapolation or automatic phase fitting. No anti-alias resampling is performed.
  Scroll over the plot to zoom, drag to pan, and double-click to reset. Measurements
  remain full-record while zooming; zoom changes only the comparison view.
- Measurements use the full record: min/max-derived peak-to-peak, mean, RMS, interpolated
  period/frequency, positive duty/width and 10–90% rise/90–10% fall times. Timing uses
  5% peak-to-peak hysteresis and median complete cycles. It is an estimate, not a
  calibrated bandwidth correction or an IEEE 181 top/base implementation. Rise/fall
  below two sample intervals and periods below ten samples are withheld. Clipped and
  irregular records are flagged. DC/insufficient cycles show a dash, not 0 Hz.
- Vpp statistics cover recorded, unclipped displayed acquisitions since Clear history;
  repeated redraws of the same acquisition do not contribute. They do not describe
  unsampled time or missing acquisitions. Clear history when changing signal setup.
- Analysis has a one-pending-record mailbox: newest wins under overload. Skipped
  acquisition tags are reported in the browser, but cannot detect USB-internal loss.

**Save capture / Open capture** preserve calibrated samples and timing, including
channel units and clipping flags. Open works in demo mode without a device. This
initial format is `.ohl.json`, deliberately not the proposed ZIP/NumPy `.ohcap` format.
See [the format specification](docs/lab-capture-format.md).

For an integrated GUI smoke test with a working OpenGL display:

```console
QT_QPA_PLATFORM=xcb OH_GUI_SCREENSHOT=/tmp/openhantek-lab.png build/openhantek/oh_regression integratedGuiSmoke
```

Local validation (2026-10-07): regression suite passed with Qt 6.10.2; integrated
NVIDIA/OpenGL 4.6 GUI smoke test passed; AddressSanitizer, UndefinedBehaviorSanitizer
and LeakSanitizer regression run passed. No real-device acquisition or firmware
changes were used for these checks. CI also builds/tests on Ubuntu 24.04.
