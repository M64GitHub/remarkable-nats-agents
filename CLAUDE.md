# CLAUDE.md — context for Claude Code

## What this is
A minimal Qt Quick (QML) demo app for the **reMarkable Paper Pro**, built to be
developed offline on a desktop and cross-compiled for the device.

## Hard constraints (do not violate)
- **Pure Qt Quick / QML only.** Qt Widgets are NOT supported on reMarkable.
- **Qt 6** (project requires >= 6.5).
- Target device: reMarkable Paper Pro, code name **`ferrari`**, arch **aarch64**
  (cortex-a53). This is different from rm1/rm2 (armv7) in the official docs.
- The official SDK is an **x86_64 Linux-host** cross toolchain. It cannot run on
  Apple silicon. Device builds happen on the x86_64 Linux machine.
- E-paper UI: high contrast, no animations/gradients, discrete state changes.
  Panel is color-capable but slow to refresh.

## Layout
- `src/main.cpp`   — launcher, loads the QML module.
- `src/Main.qml`   — the UI.
- `CMakeLists.txt` — one file serves both desktop and cross builds.
- `scripts/run-desktop.sh`  — fast UI iteration via the `qml` runtime (no device).
- `scripts/build-device.sh` — cross-compile (run on x86_64 Linux, SDK sourced).
- `scripts/deploy.sh`       — scp to device + on-device run steps.

## Workflows
- Iterate UI:      `scripts/run-desktop.sh`
- Build for device:`scripts/build-device.sh`   (set RM_SDK_ENV to the SDK env file)
- Deploy:          `scripts/deploy.sh`          (set RM_DEVICE if not 10.11.99.1)

## On-device run sequence
`systemctl stop xochitl` → `QT_QUICK_BACKEND=epaper ./hello_remarkable -platform epaper`
→ Ctrl-C → `systemctl start xochitl`.

## Refs
- SDK: https://developer.remarkable.com/documentation/sdk
- Qt Quick on e-paper: https://developer.remarkable.com/documentation/qt_epaper
- Examples: https://github.com/reMarkable/remarkable-developer-examples
- Marker/pen input is more involved and not handled in this starter.
