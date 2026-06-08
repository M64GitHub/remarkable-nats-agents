# Claude Code build prompt

Paste this when starting Claude Code in the repo on the Linux laptop.

---

This repo is a Qt Quick starter for the reMarkable Paper Pro (code name `ferrari`,
aarch64, color e-paper). Read CLAUDE.md, README.md, and the existing src/ and
scripts/ before doing anything.

We are developing entirely on this machine (a Linux laptop, x86_64 Ubuntu) — it is the SDK's
native host. The Paper Pro is connected and SSH-reachable. I will give you the SSH
command for it; the scripts default to `root@10.11.99.1` and honor `RM_DEVICE`.

GOAL: Turn this into a multi-agent chat client for the Synadia Agents Protocol over
NATS — an agent roster plus a chat pane — the e-paper equivalent of our web UI.

STEP 0 — INSPECT THE DEVICE FIRST. Do not assume capabilities; the device is right
there. Run `scripts/inspect-device.sh` and explore further over SSH as needed, then
report back what you found before proposing any design. Specifically determine:
  - reMarkable OS version (so I install the matching ferrari SDK).
  - Which Qt/QML modules exist ON THE DEVICE (QtQuick, Controls, Layouts, Network,
    WebSockets, VirtualKeyboard, ...).
  - Whether the epaper platform plugin and `libqsgepaper.so` are present, or must be
    copied from the SDK.
  - Input devices and whether ANY on-screen/virtual keyboard exists — this decides
    how a user types a prompt and is the riskiest unknown.
  - Screen geometry, fonts, CPU/RAM.
Then cross-check against the SDK sysroot (build-time view). A module must exist in
BOTH the sysroot and on the device to be safe; flag any mismatch.

REFERENCE (read for protocol patterns, NOT UX): our web UI at
`/Users/m64/space/synadia-ai/synadia-agents/examples/agent-web-ui` (it's on the Mac;
I'll copy it onto this machine, or I'll paste relevant parts). Reuse how it discovers
agents and frames prompt/response on the protocol; redesign the UX for e-paper.

PROTOCOL:
- Agents are NATS micro services named "Synadia Agents" with a "prompt" endpoint.
- Send prompts to agents.prompt.{agent}.{owner}.{name} (verb-first), request/reply.
- Roster via NATS micro service discovery ($SRV) where possible; static fallback.
- Optionally tail audit.agents.* for activity.
- Device holds NATS credentials ONLY (NKEY/JWT/creds). NO Anthropic API key on the
  device — agents run elsewhere and own their own auth.

HARD CONSTRAINTS:
- Pure Qt Quick / QML, Qt6 (>=6.5). No Qt Widgets, no QtWebEngine.
- Only use modules confirmed present in BOTH the sysroot and the device (Step 0).
  Safe baseline: QtQuick, QtQuick.Window, QtQuick.Layouts, QtNetwork.
- If using Qt Quick Controls, force the Basic style so the desktop preview matches.
- E-paper-first: NO animations/transitions/spinners/gradients; high contrast;
  large fonts; large touch targets; discrete state changes. The desktop `qml`
  preview is a logic+layout check only, never a fidelity check.
- Text input: choose the approach based on the device probe. If no usable system
  keyboard exists, build a self-contained on-screen QML keyboard; keep a
  hardware-keyboard path working for desktop/BT.
- NATS transport: minimal NATS client in C++ over QTcpSocket (QtNetwork, present on
  host and device), exposed to QML as a QObject (connect/publish/subscribe/request +
  signals), behind a clean interface so it can later be swapped for nats.zig. No
  native deps that don't cross-compile. Local plaintext nats-server first; add
  TLS/NGS behind the same interface afterward.
- Bundle all QML/assets via qt_add_qml_module (no absolute paths).

WORKFLOW:
1. Inspect the device, report findings, then propose architecture + file plan +
   milestones. Wait for my OK before large changes.
2. Keep scripts/run-desktop.sh working at every step.
3. Milestone 1: roster (static config ok) + chat pane; select an agent, send one
   prompt via NATS request, render the reply. Test against a local nats-server with
   a simple echo responder.
4. Then: $SRV discovery, streaming/audit tail, the keyboard, e-paper refresh tuning.
5. Cross-compile with scripts/build-device.sh and deploy to the real device to
   verify on e-paper before calling a milestone done. Update CLAUDE.md/README.md as
   the design solidifies.

Ask me whenever a decision affects portability or the protocol.
