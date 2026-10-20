# Develop OpenHantek Lab in VS Code

Open `OpenHantekLab.code-workspace` using **File → Open Workspace from File**, or:

```console
code OpenHantekLab.code-workspace
```

This is a native C++17/Qt6 application. The supplied tasks and GDB launch
configurations target Linux. Windows/macOS development instructions remain in
[build.md](build.md), but these editor configurations do not validate those platforms.

## Prerequisites

Install VS Code's recommended **C/C++** and **CMake Tools** extensions. On a
Debian/Ubuntu development machine, the dependencies are:

```console
sudo apt install build-essential cmake gdb qt6-base-dev qt6-tools-dev libqt6opengl6-dev libfftw3-dev libusb-1.0-0-dev
```

The presets require CMake 3.21 or newer. Qt LinguistTools enables translations;
the app can build in English without it. The separate GUI smoke task additionally
needs `xvfb` and `libgl1-mesa-dri`.

Opening the workspace does not automatically configure, compile or connect to a
scope. If the machine is unsuitable for sustained compilation, use the repository's
GitHub Actions builds. The two-job limit is a concurrency default, not a thermal
safety guarantee.

## Build, test and debug

1. Run **CMake: Select Configure Preset** and choose **Linux · Debug**.
2. **Ctrl+Shift+B** runs the default debug build, including configuration.
3. **Tasks: Run Test Task** runs the regression tests after building.
4. **F5**, with **OpenHantek Lab: demo (Linux)** selected, builds and starts a
   hardware-free debug session. Set breakpoints normally.

**Tasks: Run Task → Lab: run demo** runs without the debugger. The separately
named **connected scope** debug configuration opens the hardware path: close other
software using the USB scope first and ensure USB permissions are configured.

The fixed tasks and F5 configurations always use `linux-debug`. To build another
configuration, use CMake Tools' matching configure/build/test presets or the CLI:

```console
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
build/linux-release/openhantek/OpenHantekLab --demoMode
```

Replace `linux-release` with `linux-debug` or `linux-sanitizers` as needed. CTest
runs both regression executables with an offscreen Qt platform. Its standard run
deliberately skips the integrated OpenGL smoke case; the **GUI smoke test** task
runs that case separately using Xvfb and saves previews under `build/linux-debug/`.
CI performs that extra check for debug and release.

Builds live in separate ignored folders under `build/`. CMake Tools supplies
IntelliSense with the actual compiler configuration. For machine-specific compiler
or Qt paths, use an ignored `CMakeUserPresets.json`; do not add local absolute
paths to the shared workspace. If an activated Conda environment selects incompatible
Qt libraries, configure from a clean system terminal or set
`CMAKE_IGNORE_PREFIX_PATH` to that environment's prefix in a user preset.

The public repository is
[CargoCultScientist/OpenHantek6022](https://github.com/CargoCultScientist/OpenHantek6022).
Use a feature branch and the [Lab checks](https://github.com/CargoCultScientist/OpenHantek6022/actions/workflows/lab-tests.yml)
before merging. The presets follow the
[CMake schema](https://cmake.org/cmake/help/v3.29/manual/cmake-presets.7.html)
and [VS Code CMake workflow](https://code.visualstudio.com/docs/cpp/cmake-linux).
