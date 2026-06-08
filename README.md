# remarkable agent chat

A multi-agent chat client for the **reMarkable Paper Pro** (`ferrari`, aarch64),
talking to agents over the **Synadia Agents Protocol on NATS** — an agent roster
plus a chat pane. Starts as a minimal Qt Quick app and grows from there.

Development happens entirely on the **Tux64** (x86_64 Ubuntu), which is the SDK's
native host. The device is connected over SSH for inspection and deployment.

## 0. Prerequisites
- reMarkable **Developer Mode** enabled:
  https://developer.remarkable.com/documentation/developer-mode
- Device reachable over SSH (USB default: `root@10.11.99.1`).
- Qt6 on the Tux64 for desktop preview:
  ```sh
  sudo apt install qml6-module-qtquick qml6-module-qtquick-window qt6-declarative-dev
  ```
- A local NATS server for offline testing: `sudo apt install nats-server` (or grab a
  binary from the NATS releases), plus the `nats` CLI if you want to poke subjects.

## 1. Inspect the connected device (do this first)
```sh
scripts/inspect-device.sh        # read-only; set RM_DEVICE if not root@10.11.99.1
```
This reports the OS version, available Qt/QML modules, the epaper platform +
scenegraph plugins, input devices (the keyboard question), screen/fonts, and memory.
Use it to ground module and input decisions in what's actually on the device.

## 2. Iterate the UI on the desktop (no device needed)
```sh
scripts/run-desktop.sh           # or: qml src/Main.qml
```
Touch arrives as mouse events, so it behaves the same here. Remember: this is a
logic/layout preview, not an e-paper fidelity preview.

## 3. Cross-compile for the device
```sh
# Install the ferrari SDK matching the device's OS version (from inspect-device.sh):
#   https://developer.remarkable.com/links
./meta-toolchain-remarkable-<ver>-ferrari-public-x86_64-toolchain.sh -d ~/rm-sdk/ferrari

scripts/build-device.sh          # set RM_SDK_ENV if the env file is elsewhere
```

## 4. Deploy and run
```sh
scripts/deploy.sh                # scp + prints the on-device run sequence
```
On the device: `systemctl stop xochitl` → run with `-platform epaper` → Ctrl-C →
`systemctl start xochitl`.

## Notes
- Pure Qt Quick only — no Qt Widgets, no WebEngine.
- Device holds NATS credentials only; no Anthropic API key on the device.
- Paper Pro does not need the rm1/rm2 touch rotate/invert env vars.
- On software 3.17 you may need to copy `libqsgepaper.so` to the device (see
  `scripts/deploy.sh`); confirm from the inspection output whether it's present.

## References
- SDK: https://developer.remarkable.com/documentation/sdk
- Qt Quick on e-paper: https://developer.remarkable.com/documentation/qt_epaper
- Official examples: https://github.com/reMarkable/remarkable-developer-examples
