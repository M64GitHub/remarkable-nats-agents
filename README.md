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
Install the ferrari SDK (a near OS version is fine — `build-device.sh` cross-checks
the sysroot Qt against the device). Login-gated download:
https://developer.remarkable.com/links
```sh
chmod u+x meta-toolchain-remarkable-<ver>-ferrari-public-x86_64-toolchain.sh
./meta-toolchain-remarkable-<ver>-ferrari-public-x86_64-toolchain.sh -d ~/rm-sdk

scripts/build-device.sh          # sources the SDK env; set RM_SDK_ENV to override
```
Output `build-device/hello_remarkable` is an aarch64 ELF (verify with `file`).

## 4. Deploy and run on the reMarkable
The device must be reachable by SSH (USB `root@10.11.99.1`, or set `RM_DEVICE`) and,
to reach your NATS server, on the **same Wi-Fi/LAN** as the machine running it.

**Step 1 — copy the app (and tell it where your NATS server is).**
The device has no on-screen keyboard yet (that's a later milestone), so pass the
server address at deploy time; it's written to `~/agents.json` on the device and
read on launch.
```sh
RM_SERVER=nats://192.168.178.35:4222 scripts/deploy.sh   # use YOUR server's IP:port
```

**Step 2 — run it on the panel.** The app draws straight to the e-paper, so the
stock UI (`xochitl`) must be stopped first, then restored when you quit.
```sh
ssh root@10.11.99.1
systemctl stop xochitl                                   # blanks the stock UI
QT_QUICK_BACKEND=epaper ./hello_remarkable -platform epaper
#   ↑ the app now shows on the panel: tap an agent, send a prompt.
#   Tap "Exit" (top-right of the roster) when done — Ctrl-C usually does NOT
#   work over ssh (no PTY forwards the signal).
systemctl start xochitl                                  # ALWAYS restore the stock UI
```

> ⚠️ Always run `systemctl start xochitl` when you're finished, or the device will
> sit on a blank screen until reboot. Safer one-liner that restores it no matter how
> the app exits (Exit button, closed session, or crash):
> ```sh
> ssh root@10.11.99.1 'systemctl stop xochitl; trap "systemctl start xochitl" EXIT; \
>   QT_QUICK_BACKEND=epaper ./hello_remarkable -platform epaper'
> ```
> Tapping **Exit** quits the app cleanly, which fires the trap and restores xochitl.

**Changing the server later:** re-run Step 1 with a new `RM_SERVER`, or edit
`~/agents.json` on the device. (Once the on-screen keyboard milestone lands, you'll
be able to type the address in the app directly.)

**Sanity check without stopping xochitl** (headless; verifies the device reaches
your server and lists agents):
```sh
ssh root@10.11.99.1 'AGENT_CHAT_DISCOVER=1 AGENT_CHAT_SMOKE_HOST=192.168.178.35 \
  QT_QPA_PLATFORM=offscreen ./hello_remarkable'
```

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
