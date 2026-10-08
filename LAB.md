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
The workbench followed on `feature/capture-workbench` and `feature/instrument-ui-patterns`.
The library followed on `feature/capture-library`; the integrated application
workspace is on `codex/integrated-workspace`, building on those changes.
These additions are not silently bundled into the correctness-only release candidate.

### Integrated workspace

The application now opens directly into **Capture Lab**. It is the central
workspace, not a hidden bottom dock. **Live scope / FFT** retains the original
scope display, trigger-level/position sliders, FFT, XY, phosphor, zoom and scope
cursors. Switch with `Ctrl+1` (or `Ctrl+H`) and `Ctrl+2`. Both views share the
same acquisition and capture history; switching never starts/stops acquisition,
clears history, unpins a reference or changes a frozen selection.

- **Run / Stop** and **Single** remain at the top in either view. Single selects
  SINGLE trigger mode and arms a fresh acquisition using the existing source,
  slope and level. Repeat Single to rearm, including while already armed. A
  stopped SINGLE shows **Rearm**; choose Auto in Trigger to resume continuous
  operation. These controls do not turn Follow latest back on for a frozen view.
- The controller badge explicitly distinguishes **DEMO / SCOPE** and
  **RUNNING / STOPPED / SINGLE ARMED / DISCONNECTED**. This describes acquisition,
  not the age or selection of the waveform in Capture Lab. RUNNING is not a
  guarantee of uninterrupted or gapless samples.
- **Channels / probes** stays alongside analysis. **Timebase**, **Trigger** and
  **FFT** share a tabbed control area. They affect incoming acquisitions, not
  saved sample values. Control docks remain movable; **View → Reset workspace
  layout** restores their arrangement without touching acquisition or analysis.
- A persistent footer shows history recording, the analysis selection state,
  RAM log collection and unexported readings on either view. On disconnect,
  acquisition controls are disabled while saved captures remain usable.
- The live display retains the latest frame received before its first OpenGL
  initialization, so opening it after a stopped/Single acquisition is not blank.
  It does not invent another acquisition or add another history entry.

The dark shell and readable control labels match Capture Lab; saved trace colours,
font choices, scope settings and calibration are not rewritten by this change.
Window layout has its own version: older dock arrangements migrate to the new
default in memory, while compatible new layouts restore normally. Existing stored
layout bytes change only through normal Save/auto-save; acquisition settings do
not undergo a migration. The acquisition toolbar remains visible after restore.
Capture Lab is the startup view on every launch; the last selected tab is not saved.
**Export → Screenshot** captures the integrated workspace. The explicitly labelled
**Live scope hardcopy / Print live scope** actions first reveal the live scope view;
they do not export a historical selection. Use Capture Lab's Save capture or
Library for that selection. Revealing the live view does not unfreeze analysis.
The legacy CSV/JSON and other exporter menu entries are also explicitly labelled
**live acquisition**; their source does not follow Capture Lab's selected history.

**Follow latest** returns the browser
to the latest acquisition. **Record history** pauses/resumes retention without stopping
the scope. **More → Clear history** asks for confirmation and also resets the
statistics; saved files, a pinned reference and the session log survive it.
History retention pauses while that confirmation is open and resumes if it was on.
The dark Capture Lab workbench groups controls into Measure, Reference, Mask test
and Session log tabs under **Analysis settings**, collapsed initially. **Focus view**
hides history and settings without changing recording, logging or the selected view;
turn it off to restore the previous settings-panel state. The browser badge says
**FOLLOWING LATEST**, **VIEW FROZEN** or **HISTORY PAUSED**, never hardware Run/Stop.
RAM-log activity and unexported-data status remain visible even with panels hidden.
Waveform and Trend are separate views; Measurements contains the detailed table.
Channel-coloured reading cards retain explicit names, units, span and validity;
**Details** opens that channel's table row. **Fit record** resets waveform zoom/pan
without changing acquisition settings. **Clear reference** is in Analysis settings
→ Reference. Smaller
windows scroll rather than forcing the main application beyond the screen.
The workbench itself is now central and cannot be accidentally closed or detached.

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

**Save capture / More → Open capture** preserve calibrated samples and timing, including
channel units and clipping flags. Open works in demo mode without a device. This
initial format is `.ohl.json`, deliberately not the proposed ZIP/NumPy `.ohcap` format.
See [the format specification](docs/lab-capture-format.md).

### Named capture library

Open **Library…** in the Capture Lab toolbar. **Save selected…** stores the selected
waveform with a name, notes and comma-separated tags. The library takes a fixed
snapshot when the dialog opens: the acquisition number is shown beside Save selected,
and incoming frames cannot replace what you are saving. To keep a mask failure,
select it in history (or enable Freeze view on failure), then save it here explicitly.
Tags are user descriptions, not certified pass/fail results. Saving is not automatic.

The default folder is `~/OpenHantekLab/Captures`; **Choose folder…** selects another
existing folder and remembers that choice in Lab preferences. The default folder is
created only when you save, not when you browse. Each save creates a distinct bundle,
even with a repeated name; names and tags are never used as filesystem paths.
No database, cloud service or external plugin is required. Back up the entire folder.

- Search matches all space-separated terms, case-insensitively, across names, notes
  and tags. `Ctrl+F` focuses search. Enter in search focuses the list; Enter or a
  double-click on a list entry opens it. Clear search to return to all indexed entries.
- **Open for analysis** freezes the workbench on that capture. **Use as reference**
  replaces only the reference, leaving the selected waveform and Follow latest state
  unchanged. Both verify and load the samples, then use the existing measurement,
  alignment and mask rules. Neither applies the saved setup to the hardware.
- **Edit details…** updates only the description. Samples, completion timestamp and
  acquisition context remain unchanged. Cancel leaves the stored entry unchanged.
  Names allow 120 characters; notes 4096; tags up to 16 of 32 characters each. Duplicate
  tags are collapsed case-insensitively. Imported text is displayed as plain text.
- **Refresh** rereads metadata after another process changes the folder. Stale edits
  and opens are rejected with a refresh message rather than silently using a changed
  entry. File locking coordinates writers from this app.

The browser reads at most 1,000 immediate `capture-*` folders and at most 64 KiB of
metadata per entry; it does not parse every waveform while searching. This is a
bounded subset, not necessarily the newest 1,000 entries in a larger folder. Choose
smaller experiment folders when the scan-limit warning appears. Missing, malformed,
unsupported or symlinked entries are counted and reported; they are not deleted.
Waveform files are loaded only on Open/Use as reference and checked against their
SHA-256 checksum and manifest summary. A checksum detects changes/corruption, not
authenticity or calibration quality. Keep libraries on trusted local storage.

New saves publish a completed two-file bundle together; description updates use
[Qt's atomic QSaveFile replacement](https://doc.qt.io/qt-6/qsavefile.html), without
direct-write fallback. This is not a power-loss-durable database or backup guarantee.
A crash during creation may leave an ignored `.pending-*` folder; incomplete bundles
are never presented as saved entries. There is deliberately no in-app deletion yet.
Existing standalone `.ohl.json` files can be opened in the workbench and saved into
the library. The library's `capture.ohl.json` also remains independently readable.

### Measurement spans and expanded readings

The **Measure** selector applies one time gate to every reading in the capture
browser, including reference differences and live statistics:

- **Whole record** (default): all retained samples, independent of zoom.
- **Visible window**: only sample centres inside the browser's visible time axis.
  Wheel zoom and drag pan change the gate. This is the capture-browser window, not
  the separate Live scope / FFT view.
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

The basic folder-based library, names/notes/tags and explicit saving of selected
failures are now implemented. Next are waveform previews, richer comparison views,
replay fixtures and stronger acquisition-settings provenance. Protocol decoding and gapless USB recording remain
separate projects; they require dependency/licensing checks or hardware validation.
The installed upstream app, firmware and factory calibration remain untouched.

### UI benchmark and design direction (2026-10-07)

This is a cross-vendor benchmark of documented interaction patterns, not a market
survey or proof that one interface is more usable. Manufacturer claims such as
"intuitive" or a short learning curve are not treated as independent evidence.
The convergence worth adopting is **waveform-first, stable state, contextual
configuration**. Desktop products carry more weight here than touch-only gestures
from bench instruments. Product screenshots differ in theme: dark styling alone
is not the finding, and does not establish readability or accessibility.

| Reference | Observed pattern | OpenHantek application |
| --- | --- | --- |
| [Tektronix 4 Series B MSO quick start](https://www.tek.com/en/manual/oscilloscope/4-series-b-mso-quick-start-4-series-mso) | Stacked waveform slices, channel/settings badges, a collapsible results area, context-specific configuration. | Retain separate channel lanes; link trace, card and table identity; put detailed settings behind an explicit control. |
| [Keysight Infiniium display overview](https://helpfiles.keysight.com/csg/d9300a/Help/Infiniium-UG/Content/Topics/Home/Display_Overview.htm) | A focus mode removes peripheral panels; layouts and waveform operations have dedicated controls. | Add reversible Focus view and a discoverable Fit record button. Do not require a double-click gesture to reset zoom. |
| [Rohde & Schwarz MXO 4](https://www.rohde-schwarz.com/us/products/test-and-measurement/oscilloscopes/rs-mxo-4-oscilloscope_63493-1164992.html) | SmartGrid offers individual waveform layouts; important tools and settings have direct access. | Preserve the Qt dock and resizable split rather than hard-code a dashboard. A full drag-and-drop plot compositor is deferred. |
| [PicoScope 7 scope view](https://www.picotech.com/library/knowledge-bases/oscilloscopes/scope-view) | The signal takes most of the display; channel tools sit beside it, acquisition controls above it. | Reduce permanently expanded configuration; retain the existing acquisition controls outside the analysis workspace. |
| [Saleae Logic 2 navigation](https://www.saleae.com/support/logic-software/viewing-and-analyzing-data/navigating-the-software) | Separate connection/capture controls, named sessions, and task-specific analysis side panels. | Keep analysis state distinct from hardware acquisition; expose log activity when configuration is hidden. Named capture-library sessions remain future work. |

The inspected PicoScope, Saleae and Keysight screenshots reinforce these structural
patterns, but are examples supplied in their documentation, not measurements of
the latest installed releases. We borrow interaction ideas, not vendor artwork,
logos, exact layouts or an implication of identical hardware capability.

First implementation:

- Collapse optional configuration by default; keep Save/Open/Set reference visible.
  Move clearing history to More with a cancel-default confirmation. Disable actions
  that require a selected capture when none exists.
- Use readable numeric hierarchy and consistent channel accents. Keep names and
  validity text, so neither identity nor warnings depend solely on colour. Imported
  names remain plain text; long names must not expand the workspace.
- Keep capture-browser and hardware state distinct. Following latest does not prove
  the device is acquiring; freezing this view never stops USB acquisition. Logging
  and its RAM-only/unexported status remain visible during focus mode.
- Make common plot operations available as labelled controls as well as gestures.
  Keyboard activation uses ordinary Qt controls rather than a custom icon-only rail.

Acceptance checks are automated layout/interaction regressions plus synthetic GUI
previews: at 1100×700 with three populated channels and a long name, the default
workspace needs no scrollbars and the waveform widget occupies at least half the
window height. Focus mode expands it without changing retention/logging and restores
the previous panel state. Tests also cover keyboard activation, channel details,
Fit record, explicit clipping, action availability, and cancel/confirm history clear.
These checks do not substitute for user testing. Next evaluate real bench tasks:
find a failed capture, pin/compare it, gate a measurement, and export a session;
record misclicks and task time to refine the integrated workspace.

For an integrated GUI smoke test with a working OpenGL display:

```console
QT_QPA_PLATFORM=xcb OH_GUI_SCREENSHOT=/tmp/openhantek-lab.png build/openhantek/oh_regression integratedGuiSmoke
```

Local validation (2026-10-07): regression suite passed with Qt 6.10.2; integrated
NVIDIA/OpenGL 4.6 GUI smoke test passed; AddressSanitizer, UndefinedBehaviorSanitizer
and LeakSanitizer regression run passed. No real-device acquisition or firmware
changes were used for these checks. CI also builds/tests on Ubuntu 24.04.

Workspace regressions additionally cover default visibility, old-layout preservation,
new-layout restoration, independent browsing/acquisition, background history/logging,
disconnect availability, repeated Single arming and queued controller-thread order.
These checks exposed an existing uninitialized sample-rate target during channel
setup; it now starts with an explicit no-duration-request state until settings apply.
The integrated GUI check renders both tabs, verifies that a pre-initialization
capture reaches the actual OpenGL plot, and checks a 1280×800 default workspace.
