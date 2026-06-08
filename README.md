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
  **Nothing to install on the device** — it already has Qt 6.8.2, the `epaper`
  plugin, and `libqsgepaper.so`. It only needs NATS credentials, later (TLS/NGS).
- **Qt6 on the Tux64 for the desktop preview.** Use Qt **6.8.2** to match the device
  (Ubuntu's apt Qt is 6.4.2, below the 6.5 floor). Easiest is `aqtinstall`:
  ```sh
  python3 -m venv ~/.venvs/aqt && ~/.venvs/aqt/bin/pip install aqtinstall
  ~/.venvs/aqt/bin/aqt install-qt linux desktop 6.8.2 linux_gcc_64 --outputdir ~/Qt
  ```
  `scripts/run-desktop.sh` looks in `~/Qt/6.8.2/gcc_64` (override with `QT_ROOT`).
- **For local protocol testing:** a NATS server (`nats-server` on `PATH`) and the
  Python NATS client for the test stub / example agents:
  ```sh
  python3 -m venv ~/.venvs/natstest && ~/.venvs/natstest/bin/pip install nats-py
  ```

## 1. Inspect the connected device (do this first)
```sh
scripts/inspect-device.sh        # read-only; set RM_DEVICE if not root@10.11.99.1
```
This reports the OS version, available Qt/QML modules, the epaper platform +
scenegraph plugins, input devices (the keyboard question), screen/fonts, and memory.
Use it to ground module and input decisions in what's actually on the device.

## 2. Iterate the UI on the desktop (no device needed)
```sh
scripts/run-desktop.sh           # builds with CMake (Qt from ~/Qt) and runs the app
```
The app registers C++ types and bundles its QML as a module, so the preview is the
real binary (not the bare `qml` runtime). Touch arrives as mouse events, so it
behaves the same here. Remember: this is a logic/layout preview, not an e-paper
fidelity preview.

## 2b. Test the protocol end-to-end (no device, no display needed)
```sh
# One-shot: starts nats-server, runs an echo stub, drives the app's headless
# prompt path, and asserts the streamed reply.
QT_ROOT=~/Qt/6.8.2/gcc_64 ~/.venvs/natstest/bin/python scripts/smoke-test.py
```
For a *real, spec-compliant* counterparty (registers on `$SRV`, heartbeats), run an
example agent from the Synadia SDK against a local `nats-server`:
```sh
python examples/01-echo.py --url nats://127.0.0.1:4222 --owner local --session-name test
# -> serves agents.prompt.echo.local.test, which is in the bundled roster
```

## 3. Cross-compile for the device
```sh
# Install the ferrari SDK matching the device's OS version (5.7.121 / scarthgap;
# from inspect-device.sh). Login-gated download:  https://developer.remarkable.com/links
./meta-toolchain-remarkable-5.7.121-ferrari-public-x86_64-toolchain.sh -d ~/rm-sdk/ferrari

scripts/build-device.sh          # set RM_SDK_ENV if the env file is elsewhere
```

## 4. Deploy and run
```sh
scripts/deploy.sh                # scp + prints the on-device run sequence
```
On the device: `systemctl stop xochitl` → run with `-platform epaper` → Ctrl-C →
`systemctl start xochitl`.

## Notes
- Pure Qt Quick only — no Qt Widgets, no WebEngine. UI is hand-rolled flat QtQuick.
- Protocol is the **Synadia Agent Protocol v0.3** (service name `agents`, verb-first
  subjects, streaming chunk responses). Spec lives at
  `/home/m64/space/synadia-ai/nats-ai-docs/core-protocol.md`.
- Device holds NATS credentials only; no Anthropic API key on the device.
- Paper Pro does not need the rm1/rm2 touch rotate/invert env vars.
- `libqsgepaper.so` is already present on this device (5.7.121) — no copy needed.
  (The old 3.17 copy step in `scripts/deploy.sh` is only for older software.)

## References
- SDK: https://developer.remarkable.com/documentation/sdk
- Qt Quick on e-paper: https://developer.remarkable.com/documentation/qt_epaper
- Official examples: https://github.com/reMarkable/remarkable-developer-examples
