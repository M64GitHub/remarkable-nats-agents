# Implementation notes

Engineering rationale and the non-obvious gotchas behind this app. `README.md` is
the setup/usage guide; `CLAUDE.md` is the agent brief; this file is for the human
developer who needs to know *why the build is shaped this way* and *what bit us*.

## Architecture in one breath

```
QML (hand-rolled flat)  ─▶  AppController (QML facade: `App`)
                                 │  owns AgentModel + ChatModel
                                 ▼
                            AgentProtocol  (Synadia Agent Protocol v0.3)
                                 ▼
                            INatsConnection  ◀── the swap seam (→ nats.zig later)
                                 ▼
                            NatsClient  (NATS text protocol over QTcpSocket)
```

The QML layer never touches NATS. Everything above the wire depends only on
`INatsConnection`, so the transport can be replaced without touching the protocol
or UI. `main.cpp` owns the object graph and injects `App` as a single context
property.

## Qt / build gotchas (each cost real time)

- **A `pragma Singleton` QML file must ALSO be flagged in CMake.** `pragma
  Singleton` in `Theme.qml` is necessary but not sufficient — without
  `set_source_files_properties(... QT_QML_SINGLETON_TYPE TRUE)` before
  `qt_add_qml_module`, the generated `qmldir` lists `Theme` as a plain type, and
  every `Theme.<prop>` resolves to `undefined` at runtime (the binary builds and
  even AOT-compiles fine; it only fails when instantiated). Symptom: a flood of
  `Unable to assign [undefined] to QColor/int/QString`.

- **A header-only `QObject` needs to be in the target's sources or AUTOMOC skips
  it.** `INatsConnection.h` (pure interface, `Q_OBJECT`, signals, no `.cpp`)
  produced *undefined reference to vtable / staticMetaObject / signals* at link
  time until it was listed in `qt_add_executable(...)`. AUTOMOC keys off
  same-named `.cpp` files; a header with no `.cpp` is invisible unless listed.

- **The desktop preview builds the real binary** — it is not `qml Main.qml`. Once
  the app registers C++ types and bundles QML as a module, the bare `qml` runtime
  can't load it. `run-desktop.sh` runs CMake + the binary.

- **Qt version: use 6.8.2 on the host, matching the device.** Ubuntu 24.04's apt
  Qt is 6.4.2 (below our 6.5 floor); `aqtinstall` into `~/Qt` gives an exact
  device match with no `sudo`. The device itself needs nothing installed.

## Verifying without a display

This repo is often built on a host with no display. Two checks cover most of it:

- **`qmlcachegen`** (runs during the build) AOT-compiles every QML file, which
  statically validates types, properties, and signal handlers.
- **Headless instantiation:** `QT_QPA_PLATFORM=offscreen ./hello_remarkable` for a
  couple of seconds, with stderr watched for QML warnings — this is what caught the
  singleton bug. AOT compilation alone did **not** catch it (it's a runtime
  resolution failure).
- **`AGENT_CHAT_SMOKE=<text>`** runs a headless prompt round-trip with no QML at
  all (see `main.cpp`), exercising the full transport + protocol stack. Driven by
  `scripts/smoke-test.py`.

## NATS wire — what the client must get right (v0.3)

- The client speaks **MSG and HMSG** (headered messages). HMSG is not optional:
  service errors arrive as `Nats-Service-Error-Code` / `Nats-Service-Error`
  headers, and the **stream terminator is precisely a zero-byte body with no
  headers** — you can't detect end-of-stream without distinguishing "no header
  block" from "empty header block".
- `CONNECT` sends `headers:true` (to receive HMSG) and `no_responders:true` (so a
  prompt to a subject with no subscriber returns an immediate 503 instead of
  hanging to the inactivity timeout).
- A prompt reply is a **stream**, not one message: `{type:"status",data:"ack"}`
  first, then `{type:"response",data:<string|{text}>}` chunks, then the terminator.
  `response.data` may be a bare string *or* an object — accept both. Concatenate
  `response` text in arrival order.
- Apply a **per-stream inactivity timeout** (60 s default); core NATS is
  at-most-once and any chunk — including the terminator — can be lost.
- Learn the prompt subject from `$SRV.INFO` (M2); never construct it from identity.

## Testing fixtures

- `scripts/echo-responder.py` — a dependency-light (nats-py only) stub that speaks
  the §6 chunk protocol. Not a real agent: no `$SRV` registration, no heartbeat.
- The **real** counterparty is the Synadia SDK echo example
  (`agent-sdk/python/examples/01-echo.py`), which registers on `$SRV` and emits
  heartbeats — use it once M2 (discovery) lands. Run with
  `--owner local --session-name test` to match the bundled roster entry.

## Test-environment quirk (this sandbox only)

Under the agent's Bash sandbox, a process that binds a listening socket (or any
backgrounded `&` / `run_in_background` job) is killed with **exit 144**. To run a
local `nats-server` + agent + the binary, orchestrate them in a **single
foreground call with the sandbox disabled**, owned by one process that tears the
children down before exit (see `scripts/smoke-test.py`). This does not affect a
normal developer shell — only the agent harness.

## E-paper specifics (verified on the device)

- **Geometry:** the real panel is **1620×2160** (the inspection probe's `405×1084`
  was a virtualized stand-in). The window fills it on-device via `Screen.width/height`
  + `Window.FullScreen`, gated on `Qt.platform.pluginName === "epaper"`; the desktop
  preview stays a 1080×1440 portrait window. Still size off `Screen`/proportions —
  never hardcode.
- **Run sequence:** `systemctl stop xochitl` → `QT_QUICK_BACKEND=epaper
  ./hello_remarkable -platform epaper` → quit → `systemctl start xochitl`. Use the
  `trap "systemctl start xochitl" EXIT` one-liner in `deploy.sh` so the stock UI is
  always restored.
- **Quitting:** there is no reliable Ctrl-C — `ssh` without a PTY doesn't forward
  SIGINT to the app. So: an in-app **Exit** button (`Qt.quit()`), plus a SIGINT/
  SIGTERM/SIGHUP handler (flag + 200ms poll, the only async-signal-safe way to reach
  `app.quit()`) so closing the session also exits cleanly.
- **Refresh / typing latency.** The panel is colour (ACeP); full grayscale refresh
  is slow. `libqsgepaper` drives `EPFrameBuffer::sendUpdate(rect, WaveformMode,
  UpdateMode)` automatically and picks the waveform from content — but there is **no
  public header in the SDK**, so don't reverse-engineer it. Instead bias the backend
  toward its fast monochrome waveform from the app:
  - `app.styleHints()->setCursorFlashTime(0)` — a blinking cursor repaints the field
    ~2×/sec; kill it.
  - `QFont::NoAntialias` on the app font — anti-aliased glyphs have gray edges that
    force the slow grayscale waveform; 1-bit glyphs stay 2-colour → fast mono path.
  - Keep content pure black/white in hot paths; greys (e.g. `Theme.mute` subtitles)
    are fine off the typing path. Press-invert on keys only dirties that key.
  - Input is decoupled from display: keystrokes are never dropped even when the
    repaint lags.
- **Deploy while running:** scp over the live binary fails with ETXTBSY; `deploy.sh`
  copies to a temp name and `mv -f`s over it (rename swaps the dir entry; the running
  process keeps its inode; next launch gets the new build).
