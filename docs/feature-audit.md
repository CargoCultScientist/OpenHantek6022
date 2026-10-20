# Feature audit — 2026-10-20

**The original plan is not fully implemented.** This audit compares the October 7
draft roadmap (sections 0.1–6.3 and its visual track) with source revision
`efd6291be496bf779a31e291a159a465590500e3`. The draft roadmap and findings remain in
the owner's original checkout; this document records every numbered feature
without publishing machine-specific investigation notes.

“Partial” means useful functionality exists but the full planned acceptance
criteria are unmet. “Missing” means no implementation was found in the inspected
source/build targets; upstream features or external tools do not count as the
planned integrated capability. This is a source/test audit, not hardware certification.

## Foundation

| Plan | Status | Evidence and remaining work |
| --- | --- | --- |
| 0.1 Baseline build | Implemented for Linux | `CMakeLists.txt`, `openhantek/CMakeLists.txt`, Lab CI; separate `OpenHantekLab` binary and demo. |
| 0.2 Fork identity | Partial | Separate executable/settings, CPack/desktop/icon/udev/documentation names and macOS bundle identity. Debian package install checks in CI; no first-run settings/calibration importer or cross-platform packaging certification. |
| 0.3 Build hygiene, tests, CI | Partial | C++17, explicit translation-update target, no release `-fno-rtti`, two regression executables, Debug/Release/ASan/UBSan CI. This project setup adds shared presets. No TSan preset/clean ten-minute run or scoped formatting CI. |
| 0.4 Standalone analysis library | Missing | Algorithms live under `src/lab` and `ohcore`; there is no independent widget-free `ohanalysis` target. |
| 0.5 Immutable frames/settings | Partial | Owned queued samples, immutable Lab capture pointers, SINGLE generation and calibration guards. `captureSetup` remains receipt-time UI context; no complete immutable acquisition settings snapshot or proven race-free pipeline. |
| 0.6 Every-frame analysis/display split | Partial | Bounded newest-pending mailbox and reduced hidden/stopped processing exist. History receives displayed acquisitions, not every acquired block; no complete every-frame analysis stream. |
| 0.7 Settings controller/undo | Missing | Existing dock/MainWindow/controller wiring remains. No `ScopeController`, `SettingsPatch` or unified undo/redo layer. |
| 0.8 Synthetic/playground/replay sources | Partial | Demo and synthetic regression data, plus offline capture analysis. No selectable signal playground, source interface or full acquisition-pipeline replay. |
| 0.9 Gapless streaming feasibility spike | Missing | No async-transfer benchmark tool or supported-rate continuity report. |
| 0.10 Capture format | Partial / alternative | Versioned `.ohl.json`, validation and atomic round trips exist; this is not the planned ZIP/NumPy `.ohcap`. No `.ohrec`, WAV or `.sr` export. See `lab-capture-format.md`. |
| 0.11 Scope overlay/texture layer | Partial / alternative | Capture Lab uses a QPainter plot with gates, references and masks. No planned shared legacy-GL overlay/texture layer and cross-GL acceptance suite. |
| 0.12 Measurement records | Partial | `Lab::Measurements` has spans, validity flags and expanded readings. No generic stable-ID measurement registry with the complete planned record API. |

## Beginner tools and measurements

| Plan | Status | Evidence and remaining work |
| --- | --- | --- |
| 1.1 Auto-setup | Missing | AUTO trigger mode exists but is not automatic channel/range/timebase setup. No planned Auto button/algorithm. |
| 1.2 Actionable setup hints | Missing | Clipping and measurement validity indicators exist; no debounced diagnosis banners with one-click setup fixes. |
| 1.3 Common measurement span | Partial | Whole record, visible window and cursor gate; shading, per-channel timing and cycle mean/RMS exist in Capture Lab. Legacy scope labels are separate; interactive measurement brackets are absent. |
| 1.4 “Measure a…” presets | Missing | Setup files exist; no guided preset gallery, wiring diagrams, measurement/decoder recipes or undoable patches. |
| 1.5 Calibration/probe wizard | Missing | Existing offset calibration has correctness guards; no guided range sweep, gain wizard or probe compensation/mismatch workflow. |
| 1.6 First run/tour/playground | Missing | Existing help/demo does not provide the planned guided onboarding, signal picker or lessons. |
| 2.1 Measurement engine | Partial | Min/max/Vpp, mean/RMS/AC RMS, cycle values, period/frequency, ±width/duty, 10–90 rise/fall, crest factor, validity, Vpp/RMS/frequency statistics. Missing IEEE-181 top/base, 20–80 edges, overshoot/preshoot/slew/burst metrics, inter-channel phase/delay/gain and jitter histograms. No complete frame-time budget validation. Legacy THD correctness was fixed. |
| 2.2 Measurement panel/meters | Partial | Channel cards, detailed table and trends exist. No configurable measurement registry with per-reading sparklines/brackets or the planned voltmeter/RPM/servo meter modes. |
| 2.3 Trigger types | Missing beyond upstream edge modes | AUTO/NORMAL/SINGLE and freshness fixes exist. No pulse-width/runt/window/dropout/rise-time/pattern/Nth-edge/holdoff strategy suite. |

## Protocol decoding

| Plan | Status | Evidence and remaining work |
| --- | --- | --- |
| 3.1 Analog-to-logic thresholds | Missing | No decoder input threshold/hysteresis pipeline. Existing logical math is not this feature. |
| 3.2 Decoder engine | Missing | No libsigrokdecode dependency, session worker or decode result model. |
| 3.3 Decoder UI | Missing | No annotation rows, packet table or decode wizard. |
| 3.4 Protocol trigger | Missing | No decoded-content stop condition. |
| 3.5 PulseView bridge | Missing in this app | No `.sr` export or integrated handoff. Separately installed launchers/tools do not implement the planned app feature. |
| 3.6 Decoder packaging | Missing | No optional decoding build configuration or bundled Python/decoder distribution. |

## Capture and recording

| Plan | Status | Evidence and remaining work |
| --- | --- | --- |
| 4.1 Waveform history | Partial | 128 displayed captures / 64 MiB, timestamps, browsing while acquisition runs, skipped-tag accounting and manual library save. No every-acquisition history or measurement-expression search. |
| 4.2 Library/references | Partial | Folder bundles, names/notes/tags, metadata search/edit, checksum validation and one pinned reference with alignment/differences. No thumbnails, two-reference overlays, side-by-side view, CH−REF math, hardware setup restoration or pipeline replay. |
| 4.3 Mask testing | Partial | Reference ± absolute units or percentage, full-record checks, NOT TESTABLE states, counters and freeze-view-on-failure. No envelope from N good records, beep or acquisition stop; saving failures is manual. |
| 4.4 Gapless recorder | Missing | No asynchronous continuous USB recorder, loss-accounted stream, chunked `.ohrec` storage or proven sustained rates. |
| 4.5 Measurement log/trends | Partial | Bounded 10,000-row RAM log, minimum interval, setup segments, explicit CSV export and extrema-preserving trend. Not continuous/crash-safe disk logging; no automatic persistence. |
| 4.6 Event/overnight capture | Missing as planned | Mask counters/freezing exist, but no generalized conditions, automatic capture/context saving, notifications or overnight recording mode. |

## Spectrum, labs and automation

| Plan | Status | Evidence and remaining work |
| --- | --- | --- |
| 5.1 Spectrum tools | Partial | Existing FFT/windows/THD plus plan/size/power correctness fixes. No new averaging, spectral min/max hold, peak/harmonic labeling or complete planned spectrum UI. |
| 5.2 Waterfall | Missing | No spectrum-time texture history or peak tracking. |
| 5.3 Bode plot | Missing | No automated source sweep, gain/phase analysis or Bode export. |
| 5.4 Fitting/component lab | Missing | No sine/RC/linear fitting workflows or guided component estimation. |
| 5.5 Formulas/units/filters | Partial | Upstream fixed math/filter modes and unit-aware Lab readings. No muparser formulas, arbitrary channel-unit transforms or expanded filter design. |
| 5.6 Lessons | Missing | No interactive lesson engine/checks. |
| 6.1 Local API | Missing | No versioned authenticated localhost WebSocket/JSON-RPC service. |
| 6.2 Python package | Missing | No `ohlab` package, API client or `.ohcap`/`.ohrec` readers/examples. |
| 6.3 Test sequences/reports | Missing | No user-facing automation runner and HTML test reports. Developer regression tests are separate. |
| Visual track | Partial | Integrated Capture Lab/Live scope tabs, shared run/stop/single controls, focus mode, movable controls, cards, zoom/pan, keyboard shortcuts and screenshots. No density-coloured persistence, command palette, full snapshot-report templates, complete light/dark design system or measurement-linked trace brackets. |

## Evidence and tests

- `openhantek/src/lab/{capture,measurements,mask,measurementlog,capturelibrary}.cpp`
  implement the analysis/storage features; `capturedock.cpp`, `librarydialog.cpp`
  and `trendplot.cpp` implement their UI.
- `tests/regression.cpp` covers known signals and gates, settings preservation,
  calibration, export serialization, FFT, SINGLE, mailbox/history, masks, logs,
  library integrity/UI, workspace state, layout and hardcopy waveform pixels.
- `tests/resource_regression.cpp` covers FFT scratch reuse, graph storage/output
  and saved FFT-plan preferences.
- The existing source baseline passed all three Lab CI jobs on October 9:
  [run 37869813690](https://github.com/CargoCultScientist/OpenHantek6022/actions/runs/37869813690).
  New project-setup changes must pass the same jobs using the shared presets.
- Ordinary CTest skips `integratedGuiSmoke` without `OH_GUI_SCREENSHOT`; the
  separate Xvfb debug/release CI step supplies it. A green unit-test result alone
  is not evidence that the OpenGL test ran.
- Physical USB continuity, calibrated amplitude accuracy, all legacy GL variants,
  cross-platform installers, golden hardware captures and TSan acceptance remain
  unverified by this audit.

None of the original broad milestones M0–M6 meets every listed acceptance
criterion. That does not make the existing workbench unusable: its bounded capture,
measurement, comparison and storage features are substantial and regression-tested.
Do not describe it as a finished decoder, gapless recorder or automation platform.

## Suggested next increments

1. Finish Lab packaging identity and known-signal bench acceptance for an honest
   experimental Linux release of the current workbench.
2. Establish the settings controller and acquisition provenance needed by Auto,
   guided presets and later automation.
3. Add beginner auto-setup and actionable hints, with synthetic acceptance tests.
4. Treat protocol decoding and gapless USB recording as separate feature projects;
   validate their dependencies and stream continuity before claiming support.

This audit reports missing features; it does not implement the whole roadmap.
