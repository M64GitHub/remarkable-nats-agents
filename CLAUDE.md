# CLAUDE.md — context for Claude Code

## What this is
A multi-agent chat client for the **reMarkable Paper Pro**, talking to agents over
the **Synadia Agents Protocol on NATS**. UI = an agent roster + a chat pane (the
e-paper equivalent of our web UI). Starts from a minimal Qt Quick scaffold and
grows into the real app.

## Machines & topology
- **Build + dev host:** the Tux64 laptop (x86_64, Ubuntu 24.04). The reMarkable
  SDK runs natively here — no Docker/emulation. All work happens on this machine.
- **Target device:** reMarkable Paper Pro, code name **`ferrari`**, arch
  **aarch64** (cortex-a53), color e-paper. Developer mode enabled, **SSH-reachable**
  (default `root@10.11.99.1`; override with `RM_DEVICE`).
- **Agents/compute:** run elsewhere on the same NATS (e.g. my Claude Code channel).
  The device is a thin client and never runs inference itself.

## Inspect the device — don't assume
The device is connected, so capability questions are answered by looking, not
guessing. Run `scripts/inspect-device.sh` (read-only) before deciding which Qt/QML
modules to use, whether `libqsgepaper.so` must be copied, and how text input works.
Cross-check two surfaces — both matter:
- **SDK sysroot** (what you can compile/link against at build time).
- **The live device** (what actually exists at runtime).
If a module is in one but not the other, that's a bug waiting to happen.

## Hard constraints
- Pure Qt Quick / QML, Qt6 (>= 6.5). **No Qt Widgets, no QtWebEngine.**
- Only use Qt modules confirmed present in BOTH the sysroot and on the device.
  Safe baseline: QtQuick, QtQuick.Window, QtQuick.Layouts, QtNetwork. Verify
  anything beyond that (Controls, WebSockets, VirtualKeyboard, ...) before using it.
- If using Qt Quick Controls, force the **Basic** style so the desktop preview
  matches the device.
- **E-paper-first UI:** no animations/transitions/spinners/gradients; high contrast
  (black on white); large fonts; large touch targets; discrete state changes. The
  desktop `qml` preview is a logic+layout check only, never a fidelity check.
- **Text input:** decide empirically from the device probe. Assume no system
  keyboard is guaranteed; the likely answer is a self-contained on-screen QML
  keyboard, with a hardware-keyboard path kept working for desktop/BT. Riskiest area.
- **NATS transport:** minimal NATS client in C++ over `QTcpSocket` (QtNetwork —
  present on host and device), exposed to QML as a QObject (connect/publish/
  subscribe/request + signals), behind a clean interface so it can later be swapped
  for `nats.zig`. No native deps that don't cross-compile. Local plaintext
  `nats-server` first; TLS/NGS (NKEY/JWT) behind the same interface afterward.
- **Secrets:** device holds NATS credentials ONLY. No Anthropic API key anywhere.
- Bundle all QML/assets via `qt_add_qml_module` (no absolute paths).

## Protocol
- Agents are NATS micro services named `"Synadia Agents"` with a `"prompt"` endpoint.
- Prompts go to `agents.prompt.{agent}.{owner}.{name}` (verb-first), request/reply.
- Roster from NATS micro service discovery (`$SRV`) where possible; static fallback.
- Optionally tail `audit.agents.*` to surface activity.
- Reference web UI (read for protocol patterns, NOT UX):
  `/Users/m64/space/synadia-ai/synadia-agents/examples/agent-web-ui` (on the Mac;
  copy it to the Tux64 or browse it there for the protocol details).

## Scripts
- `scripts/inspect-device.sh` — read-only capability probe of the connected device.
- `scripts/run-desktop.sh`    — fast UI iteration via the `qml` runtime (no device).
- `scripts/build-device.sh`   — cross-compile (set `RM_SDK_ENV` to the SDK env file).
- `scripts/deploy.sh`         — scp to device + on-device run steps.

## On-device run sequence
`systemctl stop xochitl` → `QT_QUICK_BACKEND=epaper ./hello_remarkable -platform epaper`
→ Ctrl-C → `systemctl start xochitl`.

## Dev loop
inspect device → propose architecture (wait for OK) → implement a milestone →
keep `run-desktop.sh` working → cross-compile with `build-device.sh` → deploy &
test on real e-paper → iterate. Ask before any decision affecting portability or
the protocol.

## Refs
- SDK: https://developer.remarkable.com/documentation/sdk
- Qt Quick on e-paper: https://developer.remarkable.com/documentation/qt_epaper
- Examples: https://github.com/reMarkable/remarkable-developer-examples
