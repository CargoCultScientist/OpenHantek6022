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
