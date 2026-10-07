# OpenHantek Lab correctness release candidate

This is an experimental, Linux-first correctness release based on upstream
OpenHantek6022 3.4.1-rc2. It builds a separate `OpenHantekLab` executable and uses
separate settings/calibration storage. Existing OpenHantek installations are untouched.

## Changes

- Preserve incompatible settings and files opened for reading; guard against later
  autosave overwriting unsupported device settings. Explicit Save As remains available.
- Keep calibration corrections local, preserve precision and baseline selection,
  initialize calibration buffers, validate live offset inputs and remove automatic
  factory EEPROM writes, including during unplugged shutdown.
- Save CSV/JSON atomically. Fix locale grouping, escaping, missing/non-finite cells,
  frequency-axis padding, colliding JSON channel names and CSV math units.
- Own samples across queued processing/export; serialize export dialogs on the GUI
  thread and protect export lists against nested modal event loops.
- Rebuild FFT plans at the exact record length, preserve the first input, handle odd
  lengths/tiny windows and calculate harmonic power before display-floor clipping.
- Accept SINGLE triggers only from the current arm generation; reject old/in-flight
  acquisitions and clear stale trigger state. Keep held NORMAL traces' original identity.
- Correct polymorphic command destruction and reuse the registered demo model safely.

## Validation and limits

Includes automated Qt regression tests for settings, calibration, export serialization,
FFT plan resizing/THD and SINGLE freshness. Build and run instructions are in `LAB.md`.
Local testing uses synthetic samples and isolated configuration. This is **not** bench
certification. Physical USB timing, repeated real-device SINGLE captures, unplug/reconnect
and known-source measurement accuracy still need validation. Windows/macOS packaging
has not been validated. The acquisition pipeline is not gapless.

Capture history, reference comparison and the expanded measurement panel are developed
separately on `feature/correctness-and-captures`, on top of this correctness base.
