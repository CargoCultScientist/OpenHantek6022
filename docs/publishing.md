# Publishing OpenHantek Lab

The existing public fork,
[CargoCultScientist/OpenHantek6022](https://github.com/CargoCultScientist/OpenHantek6022),
is the publishing home. Preserve its upstream history, copyright notices and
[LICENSE](../LICENSE). A second repository is not required for VS Code.

## Source and development builds

Work on a `codex/…` or `feature/…` branch, push it and open a pull request to
`main`. The **Lab regression tests** workflow uses the same CMake presets as
developers: Debug, Release and Address/UndefinedBehavior sanitizers. Debug and
Release also render the integrated workspace under Xvfb/software OpenGL.

After all jobs pass, the workflow exposes Linux development binaries, an Ubuntu
24.04 Debian package, and workspace previews as artifacts (14-day retention).
Release CI extracts and installs the package, validates its desktop entry and
starts the installed executable with `--help`. These binaries require compatible
system Qt6, FFTW and libusb libraries; they are not standalone installers.
They contain the build's source revision in their version string.

## Before an installer or tagged public release

- Choose a Lab version and write release notes that match
  [the implemented feature set](feature-audit.md).
- Validate side-by-side installation and removal on real supported systems.
  CPack now uses `openhanteklab`, a Lab desktop/icon/bundle identity, separate
  documentation and udev filenames/labels, and the `OpenHantekLab` executable.
  Debian dependencies are calculated from linked libraries. The USB rules still
  address the same hardware; two applications must not claim one device together.
- Review package metadata, maintainership, license declarations and runtime
  dependency resolution. Do not reuse upstream's release badges as Lab claims.
- Complete a known-source hardware checklist: amplitude/frequency, probe factors,
  calibration persistence, repeated SINGLE rearming and unplug/reconnect.
- Verify a clean installation of each advertised platform. Windows/macOS support
  must remain unvalidated until it is actually tested.
- Review the final source and assets for private captures, machine-specific
  configuration or credentials; publish only the intended release files.
- Merge the reviewed branch, tag the reviewed commit, build from that tag, and
  attach the validated artifacts and checksums to a GitHub release.

The inherited **Build** packaging workflow is manual-only and is not an approved
Lab release pipeline. Do not run it as a substitute for the packaging work above.
No tagged release or local package installation is performed by these changes.
Firmware and calibration are unchanged. RPM, macOS and Windows packaging remain
unvalidated; the Debian artifact targets the CI distribution, not every Linux.
