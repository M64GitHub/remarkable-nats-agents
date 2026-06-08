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

`ChatModel` is **multi-conversation** — one message list per agent prompt subject —
so switching agents preserves history (capped at 20 messages each). Streaming
replies route by request id to the right conversation even when it isn't on screen.
`AgentModel` is the roster (a `GridView` of cards); liveness comes from heartbeats
with a stale sweep.

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

- **Don't shadow a FINAL property of `Item`.** `Keyboard.qml` declared `property
  string layer` for its letter/symbol state and failed at load with *Cannot override
  FINAL property* — `Item` already has the `layer` (effects) group. Renamed to
  `keyLayer`. Same trap exists for `state`, `data`, `children`, `anchors`, `opacity`,
  etc. AOT-compiles fine; fails only at instantiation (caught by the offscreen run).

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
- **Headless instantiation:** `QT_QPA_PLATFORM=offscreen ./rm-agents` for a
  couple of seconds, with stderr watched for QML warnings — this is what caught the
  singleton bug. AOT compilation alone did **not** catch it (it's a runtime
  resolution failure).
- **Env-gated headless modes in `main.cpp`** (no QML/display, good for CI):
  - `AGENT_CHAT_SMOKE=<text>` — full prompt round-trip over the transport+protocol
    stack (driven by `scripts/smoke-test.py`).
  - `AGENT_CHAT_DISCOVER=1` — `$SRV` discovery + heartbeat probe; prints the roster
    (target a server with `AGENT_CHAT_SMOKE_HOST`).
  - `AGENT_CHAT_TEST=chat` — self-test of the multi-conversation `ChatModel`
    (per-agent history retention + off-screen streaming routing).

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

## TLS + NGS auth (M5)

Connecting to Synadia Cloud (`tls://connect.ngs.global`) adds TLS + decentralized
JWT/NKEY auth on the same `QSslSocket`:

- **Flow:** connect plain TCP → read server `INFO` (carries `nonce`, `tls_required`)
  → `startClientEncryption()` → on `encrypted()` send `CONNECT`. (NGS is "INFO then
  upgrade"; `tls_first` isn't used.) Plaintext `nats://` skips the upgrade entirely.
- **Auth:** `NatsCreds` parses the `.creds` file (USER JWT + NKEY SEED). The seed is
  base32-decoded — bytes `[2,34)` are the 32-byte Ed25519 seed — and used to **sign
  the `nonce`** via OpenSSL `EVP_PKEY_ED25519`; `CONNECT` then carries `jwt` +
  base64url(sig). The seed is a secret: in memory only, never logged.
- **Crypto dep:** OpenSSL `libcrypto` (`find_package(OpenSSL)` → `OpenSSL::Crypto`),
  present in sysroot + device + host. TLS itself needs no direct link — Qt's
  `libqopensslbackend.so` plugin handles it at runtime.
- **Cert verification:** uses the system CA bundle; the device has
  `/etc/ssl/certs/ca-certificates.crt`, so `connect.ngs.global` verifies. We do NOT
  `ignoreSslErrors` — a failed check is surfaced as an error.
- **Creds on the device:** `RM_CREDS=… scripts/deploy.sh` scp's the `.creds` (chmod
  600) and writes its path into `~/agents.json` (`"creds"`). The device holds creds
  only — no other secrets.
- **Headless test:** `AGENT_CHAT_TLS=1 AGENT_CHAT_CREDS=<.creds>
  AGENT_CHAT_SMOKE_HOST=connect.ngs.global` with the SMOKE/DISCOVER modes. Verified
  discovering + prompting NGS agents from both desktop and device.

## Attachments (M6 v1 — thumbnails)

Attach notebook pages to a prompt as images.

- **Protocol reality:** `attachments` is an **array** (§5.2) — multiple files are
  fine, so we send one PNG per page. No need for SVG, image concatenation, or (yet)
  a `.rm` renderer. The real limit is `max_payload` (1 MB on the agents we use),
  which the caller enforces locally (§5.4): we sum the encoded size and **block**
  oversize sends with a notice.
- **v1 source = device thumbnails.** `NoteStore` reads the xochitl store
  (`<uuid>.metadata` + `.content` + `.thumbnails/<pageId>.png`), notebooks only
  (`type==DocumentType`, `content.fileType==notebook`), reconstructing folder paths
  from `CollectionType` parents.
- **Thumbnails are LAZY** (corrects READING-NOTES.md's "every page"): pages never
  opened recently have no PNG, and some notes have no `.thumbnails` dir at all (on
  the real device, 36 notebooks → 13 with any rendered page). `NoteStore` filters to
  pages that actually have a thumbnail, so the picker only offers attachable pages.
  Full coverage would need the v2 `.rm` renderer (RM-PARSER-RENDERER.md).
- **The big v1 result:** the NGS **pi vision agent read a 512×384 thumbnail's
  handwriting** correctly. So low-res thumbnails are good enough, and the full-res
  renderer is **deferred as likely unnecessary** — exactly why we tested v1 first.
- **Wire:** `AgentProtocol::sendPrompt(subject, text, attachments)` →
  `{prompt, attachments:[{filename, content:<standard base64>}]}`. Prompt must still
  be non-empty (§5.1) even with attachments.
- **Device-first:** the browser reads the *device's* library; on the desktop point
  `$AGENT_CHAT_XOCHITL` at a copied sample (`test-data/`, gitignored). Sending page
  images off-device is a real consent decision (the input is confirmed before send).

## Testing fixtures

- `scripts/echo-responder.py` — a dependency-light (nats-py only) stub that speaks
  the §6 chunk protocol. Not a real agent: no `$SRV` registration, no heartbeat.
- The **real** counterparty is the Synadia SDK echo example
  (`agent-sdk/python/examples/01-echo.py`), which registers on `$SRV` and emits
  heartbeats — used to verify M2 discovery + liveness. Run with
  `--owner local --session-name test` to match the bundled roster entry. The `pi`
  example is a real LLM agent — good for exercising many-chunk token streaming.

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
  ./rm-agents -platform epaper` → quit → `systemctl start xochitl`. Use the
  `trap "systemctl start xochitl" EXIT` one-liner in `deploy.sh` so the stock UI is
  always restored.
- **Quitting:** there is no reliable Ctrl-C — `ssh` without a PTY doesn't forward
  SIGINT to the app. So: an in-app **Exit** button (`Qt.quit()`), plus a SIGINT/
  SIGTERM/SIGHUP handler (flag + 200ms poll, the only async-signal-safe way to reach
  `app.quit()`) so closing the session also exits cleanly.
- **Refresh / typing latency — measured, still slow (open).** The panel is colour
  (ACeP) and the refresh itself is the bottleneck. We tried two app-level mitigations
  expected to bias the backend toward a fast monochrome waveform:
  - `app.styleHints()->setCursorFlashTime(0)` — steady (non-blinking) cursor.
  - `QFont::NoAntialias` — 1-bit glyphs (no grey edges to trip the grayscale path).

  **Result on the device: neither measurably improved typing latency.** So the cost
  is in `libqsgepaper`'s refresh (likely a fixed full/grayscale update per change),
  not in content classification. We kept both anyway — the steady cursor avoids
  pointless repaints and the crisp 1-bit text is preferred — but they are **not** the
  fix. Don't re-try them expecting a speedup.
  - The only real lever is `EPFrameBuffer::sendUpdate(rect, WaveformMode, UpdateMode)`
    in `libqsgepaper` (fast A2/DU waveforms + partial updates). There is **no public
    header in the SDK**, so using it means reverse-engineering the class (community
    `epframebuffer.h` exists). Deferred unless the latency is judged worth that
    fragility on the user's device.
  - Silver lining (not the bottleneck): input is decoupled from display, so
    keystrokes are never dropped even when the repaint lags.
- **Deploy while running:** scp over the live binary fails with ETXTBSY; `deploy.sh`
  copies to a temp name and `mv -f`s over it (rename swaps the dir entry; the running
  process keeps its inode; next launch gets the new build).
