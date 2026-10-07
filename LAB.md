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
started on `feature/correctness-and-captures`, followed by `feature/measurement-spans`.
The current development branch is `feature/capture-workbench`, building on both.
These additions are not silently bundled into the correctness-only release candidate.

Open **View → Capture history & measurements** (`Ctrl+H`). The scope above remains
live when selecting a historical record below. **Follow latest** returns the browser
to the latest acquisition. **Record history** pauses/resumes retention without stopping
the scope. Clear history also resets the statistics; a pinned reference survives it.
The dark Capture Lab workbench groups controls into Measure, Reference, Mask test
and Session log tabs. Waveform and Trend are separate views; Measurements contains
the detailed table. Compact reading cards remain visible beneath the views. Smaller
windows scroll rather than forcing the main application beyond the screen. Like
other Qt docks, the workbench can be detached for a larger standalone view.

- History holds at most 128 displayed acquisitions or 64 MiB of sample/metadata
  payload. A selected record and pinned reference may retain up to two additional
  records (each limited to one million samples). This is not a continuous recorder.
- Set reference pins the selected record. Solid traces are the selection; dashed
  traces are the reference. Channels share an amplitude scale only with the matching
  reference channel; different units are not compared. The default aligns record
  starts. Trigger alignment requires both captures to be triggered. A manual shift
  moves the reference in seconds. Differences use linear interpolation within overlap,
  never extrapolation or automatic phase fitting. No anti-alias resampling is performed.
  Scroll over the plot to zoom, drag to pan, and double-click to reset. In the default
  Whole record mode, zoom changes only the view. See measurement spans below.
- Measurements include min/max-derived peak-to-peak, mean, RMS, interpolated
  period/frequency, positive and negative duty/width and 10–90% rise/90–10% fall times. Timing uses
  5% peak-to-peak hysteresis and median complete cycles. It is an estimate, not a
  calibrated bandwidth correction or an IEEE 181 top/base implementation. Rise/fall
  below two sample intervals and periods below ten samples are withheld. Clipped and
  irregular records are flagged. DC/insufficient cycles show a dash, not 0 Hz.
- Live statistics cover recorded, unclipped displayed acquisitions, not unsampled
  time or missing acquisitions. Repeated redraws and history navigation do not
  contribute. See statistics reset rules below.
- Analysis has a one-pending-record mailbox: newest wins under overload. Skipped
  acquisition tags are reported in the browser, but cannot detect USB-internal loss.

**Save capture / Open capture** preserve calibrated samples and timing, including
channel units and clipping flags. Open works in demo mode without a device. This
initial format is `.ohl.json`, deliberately not the proposed ZIP/NumPy `.ohcap` format.
See [the format specification](docs/lab-capture-format.md).

### Measurement spans and expanded readings

The **Measure** selector applies one time gate to every reading in the capture
browser, including reference differences and live statistics:

- **Whole record** (default): all retained samples, independent of zoom.
- **Visible window**: only sample centres inside the browser's visible time axis.
  Wheel zoom and drag pan change the gate. This is the capture-browser window, not
  the legacy oscilloscope view above it.
- **Between cursors**: enter A/B in seconds, or Shift-click to place A and Ctrl-click
  to place B on the plot. Reversed cursors are accepted. Zooming does not move them.

The gate is shaded, with dashed boundaries when visible. Time zero is record start,
or the trigger when **Align trigger** is enabled and that capture has a trigger.
Every channel uses the same time axis; its row reports the actual first/last sample
times, count and duration. Boundaries include samples exactly on the cursor. Empty
gates and gates without enough cycles show dashes for unavailable readings. Clipping
remains a record-level flag: narrowing the gate does not clear it. Reference deltas
use only overlapping samples inside the gate; their tooltip reports that count.

**More measurements** opens the Measurements view and reveals minimum, maximum, AC RMS, period, negative duty/width,
cycle mean/RMS and crest factor (peak absolute amplitude divided by RMS). Scroll the
table horizontally to reach the added columns. Cycle mean/RMS use complete rising-
edge-to-rising-edge cycles inside the gate and analytically integrate the linearly
interpolated waveform, rather than rounding edges to sample indices. Their tooltips
give the cycle count and duration. Ordinary mean/RMS still use every sample in the
gate; cycle values can therefore differ, especially at low sampling resolution.
These controls do not change the legacy measurement labels in the main scope view.

### Live statistics

Choose **Vpp**, **RMS** or **Frequency** for count, mean, minimum, maximum and sample
standard deviation (σ; unavailable until two valid values). **Reset statistics**
starts a new run without clearing history or the pinned reference. It starts with
the next newly recorded acquisition; it does not replay old captures. Changing the
displayed statistic does not reset the run.

Changing the gate or trigger-alignment mode resets the run. Units, channel names,
sample interval/count or capture setup metadata changes also reset it automatically,
including same-unit math-mode changes and changes to either input of a math channel.
Frozen/imported captures show live statistics only if their setup and gate match;
these are still live-run statistics, not a historical summary of that selection.
Setup metadata is receipt-time UI context, not an atomic hardware snapshot. Reset
manually after calibration, external circuit changes or any change not represented
by this metadata. Save/Open preserves captures, not cursor positions or accumulated
statistics.

### Reference mask testing

Pin a known-good capture with **Set reference**, then open **Mask test**, choose a
channel and tolerance, and enable **whole-record mask** testing. The tolerance is
either an absolute amount in that channel's base units (V, V², W or dimensionless),
or a percentage of the reference's full peak-to-peak value. It is not a percentage
of the instantaneous voltage; a 2 Vpp reference at 5% permits ±0.1 V, even near zero.
A constant reference has zero percentage tolerance; use an absolute tolerance for DC.

The green band shows the reference ± tolerance. Every selected sample must fall
inside it to pass. Testing uses the same linear interpolation and reference time
shift/alignment as comparison, but always tests the **whole retained record**, not
just the measurement gate or visible window. This deliberately prevents zooming
away a failure. The visual band is pixel-decimated; the test checks every sample.
It makes no statement about waveform excursions between samples or missing captures.

- **PASS / FAIL** requires matching units, finite unclipped data, and reference
  coverage of every selected sample. Partial overlap, empty channels, incompatible
  units, invalid samples or missing required triggers are **NOT TESTABLE**.
- New-record counters count acquisitions retained in history, not redraws or history
  navigation. Changing the reference, channel, tolerance, alignment or shift resets
  the counters. **Reset counters** also starts a new run without clearing history.
- The history filter shows failures, passes or not-testable captures under the current
  criterion. Historical re-evaluation does not contribute to new-record counters.
  Filtering only changes the list; it does not change the selected waveform.
- **Freeze view on failure** pins the first failing new capture while Follow latest
  is enabled. It does not stop the scope, history recording or session logging.
  Use Save capture to keep that failure, or Follow latest to rearm the browser.

No EEPROM, calibration or acquisition commands are sent by this feature. There is
no automatic failure-file writing, audible alarm or unattended hardware control.

### Session measurement log and trends

In **Session log**, choose a channel and enable **Collect measurements (RAM)**.
The log records all available measurements for that channel from newly displayed
captures, using the measurement span selected in Measure. It works independently
of Record history. A zero minimum interval records every displayed capture;
otherwise the next available capture at least that many seconds later is recorded.
It does not create synthetic readings on a timer. Repeated or paused redraws do not
count. Changing capture setup or the log criterion records a new segment immediately.

The buffer retains at most **10,000 readings**, evicting oldest entries with a visible
counter. This is a **RAM-only session log**, not a crash-safe or gapless recorder.
Closing the main window or clearing an unexported log prompts to export, discard or
cancel; collection pauses during that decision and resumes if canceled. A crash or
forced process exit still loses the RAM log. **Export log CSV**
saves a snapshot of the retained rows atomically; acquisition may continue while
the file dialog is open. Export again to save subsequently collected readings.

CSV contains UTC completion timestamps, acquisition tags, segment IDs, channel names,
base units, actual measurement sample ranges, count, measurements and validity flags.
Unavailable numeric results are empty; duty is a fraction, time is seconds, frequency
is hertz. The context JSON field preserves receipt-time setup metadata and the gate,
alignment and interval settings; it is not an atomic hardware-settings snapshot.
Channel names are quoted and protected against spreadsheet formula interpretation.
Clipped rows retain their clipping flag; do not treat their level readings as valid.

The **Trend** view shows Vpp, RMS, frequency or mean for the **latest segment only**,
using acquisition completion times on the horizontal axis. Clipped/unavailable
readings break the trace, and pixel min/max envelopes retain narrow extrema. Setup,
units, channel, gate, interval, alignment, resumed collection or a backward wall-clock
jump starts a new segment, preventing incompatible readings from being joined.
Older retained segments remain in CSV. Lines between readings are visualization,
not evidence of continuous sampling. Reset/restart manually after calibration or
external circuit changes that the receipt-time metadata cannot identify.

### Next implementation milestones

The next useful work is a folder-based capture library with names/notes/tags and
explicit saving of selected failures, followed by replay fixtures and stronger
acquisition-settings provenance. Protocol decoding and gapless USB recording remain
separate projects; they require dependency/licensing checks or hardware validation.
The installed upstream app, firmware and factory calibration remain untouched.

For an integrated GUI smoke test with a working OpenGL display:

```console
QT_QPA_PLATFORM=xcb OH_GUI_SCREENSHOT=/tmp/openhantek-lab.png build/openhantek/oh_regression integratedGuiSmoke
```

Local validation (2026-10-07): regression suite passed with Qt 6.10.2; integrated
NVIDIA/OpenGL 4.6 GUI smoke test passed; AddressSanitizer, UndefinedBehaviorSanitizer
and LeakSanitizer regression run passed. No real-device acquisition or firmware
changes were used for these checks. CI also builds/tests on Ubuntu 24.04.
