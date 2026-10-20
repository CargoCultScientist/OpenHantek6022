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

After all jobs pass, the workflow exposes Linux development binaries and workspace
previews as artifacts (14-day retention). These binaries require compatible
system Qt6, FFTW and libusb libraries; they are not standalone installers.
They contain the build's source revision in their version string.

## Before an installer or tagged public release

- Choose a Lab version and write release notes that match
  [the implemented feature set](feature-audit.md).
- Finish Lab package identity: CPack still names upstream packages and installs
  upstream desktop/udev assets; the desktop launcher still invokes `OpenHantek`
  while the actual executable is `OpenHantekLab`. macOS bundle identity also
  remains upstream-derived. Validate side-by-side installation and removal.
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
The project setup and audit do not create a release, install packages or change
device firmware/calibration.
