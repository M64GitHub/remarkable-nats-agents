# CLAUDE.md — context for Claude Code

## What this is
A multi-agent chat client for the **reMarkable Paper Pro**, talking to agents over
the **Synadia Agents Protocol on NATS**. UI = an agent roster + a chat pane (the
e-paper equivalent of our web UI).

## Status (checkpoint)
Working and verified on the real device:
- **M1** — NATS transport + roster + chat + streaming prompt.
- **M2** — `$SRV` discovery + heartbeat liveness (dynamic roster).
- **M3** — on-screen keyboard (`src/qml/Keyboard.qml`) + fills the 1620×2160 panel.
- **M5** — TLS + NKEY/JWT auth to **NGS / Synadia Cloud** (`tls://connect.ngs.global`).
  Verified discovering + prompting NGS agents from both desktop and the device.
  Plus an in-roster **NATS context picker** (`~/.config/nats/context/*.json`).
- **M6 (v1)** — **attachments**: Attach key → note browser (notebooks only) → page
  range → device-rendered page **thumbnails** as multiple attachments (§5.2 allows an
  array; enforce `attachments_ok` + `max_payload`). Verified on NGS — the pi vision
  agent read a 512×384 thumbnail's handwriting, so the full-res `.rm` renderer
  (docs/RM-PARSER-RENDERER.md) is **deferred as likely unnecessary**. Caveat: thumbnails
  are **lazy** — only already-rendered pages are attachable (NoteStore filters the
  rest; open a page on the device to render it).
- UI polish — 3-across roster cards, chat bubbles + timestamps, in-app **Exit** button.

Known issue — **typing refresh latency**: inherent to the panel. `setCursorFlashTime(0)`
+ `QFont::NoAntialias` did **not** improve it; the real lever (`EPFrameBuffer::sendUpdate`
waveform/partial control in `libqsgepaper`) has no public SDK header, so it's deferred.
See docs/IMPLEMENTATION-NOTES.md.

Not done yet: mid-stream queries (§7), `audit.agents.*` tail.

- **V2 M1 (renderer core)** — in-app `.rm` v6 parser + raster renderer (`src/rm/`):
  `RmTaggedReader` + `RmParser` (SceneLineItem strokes) + `RmRenderer`→PNG. Parses the
  fixture **byte-exact** (663 strokes / 10,268 points) and renders an **upright,
  full-res** PNG matching the device thumbnail (colours incl. cyan/magenta). Headless via
  `AGENT_CHAT_TEST=render`. Renderer defaults to **uniform stroke width** (colours were
  drawn thicker than the black ink; `RenderOptions::uniformWidth`). Fixed a real spec bug
  along the way — the `TagType` nibbles in `docs/RM-PARSER-RENDERER.md` were inverted;
  corrected from rmscene. **Device-verified** (rendered a real page on the Paper Pro). See
  that doc for calibration findings.
- **V2 M2 (PDF + multi-page)** — `RmRenderer::renderToPdf(vector<Page>)`: **vector** strokes
  via `QPdfWriter`, one PDF page per input page, strokeless pages skipped; shares the
  stroke route with PNG. Compact (~112 KB/page vs ~448 KB PNG) → multi-page fits under
  `max_payload`. Cross-compiles + **runs on device**.
- **V2 attach-flow wiring** — `NoteStore` now lists any page with a `.rm` (not just ones
  with a lazy thumbnail) and exposes its path; `AppController::stageNotePages(row,from,to,
  format)` renders the range to a temp dir — **PNG per page** (dimension-capped) or **one
  multi-page PDF** — chosen by a **PNG/PDF toggle** in the note browser. End-to-end
  **verified against the live NGS `pi` vision agent**: it transcribed our rendered PNG and
  read both pages of a 2-page PDF. (Interactive on-device GUI pass still to do by hand.)

**Next up: typed-text (RootText 0x07 → Markdown)** for typed pages (cheapest agent input),
then polish (pressure→opacity, layers, eraser). Spec: `docs/RM-PARSER-RENDERER.md`.

## Machines & topology
- **Build + dev host:** the Linux laptop (x86_64, Ubuntu 24.04). The reMarkable
  SDK runs natively here — no Docker/emulation. All work happens on this machine.
- **Target device:** reMarkable Paper Pro, code name **`ferrari`**, arch
  **aarch64** (cortex-a53-crypto), color e-paper. OS **`5.7.121` (scarthgap)**,
  Qt **6.8.2**. Developer mode enabled, **SSH-reachable** (default
  `root@10.11.99.1`; override with `RM_DEVICE`).
- **Agents/compute:** run elsewhere on the same NATS (e.g. my Claude Code channel).
  The device is a thin client and never runs inference itself.

## Toolchain (who installs what)
- **Device: install nothing.** Qt 6.8.2, the `epaper` platform plugin, and
  `libqsgepaper.so` are already present. The device only ever needs a NATS
  **credentials** file, and not until the TLS/NGS milestone.
- **Host desktop preview/build:** Qt **6.8.2** under `~/Qt` (installed via
  `aqtinstall`, matches the device) — Ubuntu's apt Qt is 6.4.2, below our floor.
  `scripts/run-desktop.sh` finds it via `QT_ROOT` (default `~/Qt/6.8.2/gcc_64`).
- **Host cross-compile:** the ferrari SDK is installed at **`~/rm-sdk`** (SDK
  `5.7.119`; its sysroot Qt is **6.8.2** — verified == device, all needed QML
  modules present). Its env file exports `CMAKE_TOOLCHAIN_FILE`, so
  `scripts/build-device.sh` cross-compiles after sourcing it. Cross-built aarch64
  binary verified running on the device (discovery over Wi-Fi → laptop NATS).

## Inspect the device — don't assume
The device is connected, so capability questions are answered by looking, not
guessing. Run `scripts/inspect-device.sh` (read-only) before deciding which Qt/QML
modules to use, whether `libqsgepaper.so` must be copied, and how text input works.
Cross-check two surfaces — both matter:
- **SDK sysroot** (what you can compile/link against at build time).
- **The live device** (what actually exists at runtime).
If a module is in one but not the other, that's a bug waiting to happen.

**Findings (probed 2026-06-08; re-verify, don't trust this blindly):**
- On device: QtQuick, QtQuick.Window, QtQuick.Layouts, **QtQuick.Controls** (Basic
  + others), QtQuick.Shapes, **QtNetwork**, **QtWebSockets**, QtQml/QtCore present.
- `epaper` platform plugin (`libepaper.so`) and `libqsgepaper.so` both present —
  **no copy needed** (unlike the older 3.17 note).
- **No virtual keyboard** (no `platforminputcontexts`, no VirtualKeyboard module) —
  on-device typing → implemented as our own `src/qml/Keyboard.qml` (M3); an external
  BT/USB kbd also works.
- Input: pen (Elan marker), touch (Elan touch), power key. No HW keyboard attached.
- Fonts: Noto Sans / Noto Sans UI / Noto Serif / **Noto Mono** / EB Garamond.
- Screen: the DRM panel reported a non-physical `405×1084` mode (looks like a
  scaled/virt stand-in, not the real 1620×2160). **Size off `Screen.width/height`
  and proportions — never hardcode pixels;** confirm real geometry on first deploy.

## Hard constraints
- Pure Qt Quick / QML, Qt6 (>= 6.5). **No Qt Widgets, no QtWebEngine.**
- Only use Qt modules confirmed present in BOTH the sysroot and on the device.
  Safe baseline: QtQuick, QtQuick.Window, QtQuick.Layouts, QtNetwork. Verify
  anything beyond that (Controls, WebSockets, VirtualKeyboard, ...) before using it.
- **UI is hand-rolled flat QtQuick** (custom Rectangle/Text/MouseArea components),
  **not** Qt Quick Controls — chosen for full control over e-paper rendering (no
  focus rings, no implicit animation). Controls (Basic) is available as a fallback;
  if ever used, force the **Basic** style so the desktop preview matches the device.
- **E-paper-first UI:** no animations/transitions/spinners/gradients; high contrast
  (black on white); large fonts; large touch targets; discrete state changes. The
  desktop preview is a logic+layout check only, never a fidelity check.
- **Text input:** the probe confirmed **no system/virtual keyboard** on the device.
  Implemented (M3): a self-contained on-screen QML keyboard (`src/qml/Keyboard.qml`);
  the hardware-keyboard path stays working for desktop/BT (`TextEdit`, Enter to send).
- **NATS transport:** minimal NATS client in C++ over `QSslSocket` (QtNetwork —
  present on host and device), exposed to QML behind the `INatsConnection` interface
  so it can later be swapped for `nats.zig`. It speaks the text protocol incl.
  **HMSG (headers)** — needed to see service errors and the header-less stream
  terminator. The protocol layer turns a reply inbox into the v0.3 streaming
  contract (ack → response chunks → terminator). **TLS + NKEY/JWT (NGS) implemented**
  (M5): `tls://` upgrades after INFO, then CONNECT signs the nonce via OpenSSL
  Ed25519 (`src/nats/NatsCreds.*`). No native deps that don't cross-compile
  (OpenSSL `libcrypto` is in the sysroot + device).
- **Secrets:** device holds NATS credentials ONLY. No Anthropic API key anywhere.
- Bundle all QML/assets via `qt_add_qml_module` (no absolute paths).

## Protocol — Synadia Agent Protocol **v0.3** (authoritative spec below)
Spec: `/home/m64/space/synadia-ai/nats-ai-docs/core-protocol.md`. Follow it; the
older "Synadia Agents" service name was **v0.1** and is wrong for v0.3.
- Agents register as NATS micro services with service **`name = "agents"`**;
  `metadata.protocol_version = "0.3"`. Required endpoints: `prompt`, `status`.
- **Discovery:** `$SRV.PING.agents` / `$SRV.INFO.agents` (multi-response,
  scatter-gather). Callers MUST use the `prompt` endpoint's `subject` from the
  discovery record — **don't construct it from identity.** Default subject is
  `agents.prompt.{agent}.{owner}.{name}` (verb-first); used as the static fallback.
- **Prompt = request + streaming reply** (not single reply): request envelope
  `{"prompt": "..."}` (plain text also valid) to the prompt subject with a reply
  inbox. Reply inbox receives `{type:"status",data:"ack"}` first, then one or more
  `{type:"response",data:<string|{text}>}` chunks, ended by a **zero-byte,
  header-less terminator**. Errors: a message with `Nats-Service-Error-Code` /
  `Nats-Service-Error` headers before the terminator. Per-stream inactivity
  timeout: 60s. Unknown chunk `type`s: ignore.
- **Liveness:** heartbeats pub/sub on `agents.hb.*.*.*` (payload has `instance_id`,
  `interval_s`); subscribe before first discovery. `status` endpoint = on-demand
  heartbeat-shaped request/reply. **Discovery + heartbeat liveness are implemented
  (M2)**; the `status` request/reply bootstrap is not yet used.
- **Testing fixtures** (real, spec-compliant — register on `$SRV` + heartbeat):
  `/home/m64/space/synadia-ai/synadia-agents/agent-sdk/python/examples/01-echo.py`
  (and the TS twin). Run with `--owner local --session-name test` → subject
  `agents.prompt.echo.local.test` (matches the bundled roster + smoke test).
- **Secrets:** device holds NATS credentials ONLY. No Anthropic API key anywhere.
- Reference web UI (protocol patterns, NOT UX) is at (on this host)
  `/home/m64/space/synadia-ai/synadia-agents/examples/agent-web-ui` — but it uses
  the JS SDK, which hides the wire; the spec above is the source of truth.

## Code layout
- `src/nats/INatsConnection.h` — transport interface (the swap seam for nats.zig).
- `src/nats/NatsClient.{h,cpp}` — NATS-over-QSslSocket: handshake, PING/PONG,
  PUB/SUB/UNSUB, MSG + **HMSG** parsing, `_INBOX` factory, TLS upgrade for `tls://`.
- `src/nats/NatsCreds.{h,cpp}` — parse a `.creds` file (JWT + NKEY seed) and
  Ed25519-sign the server nonce (OpenSSL `libcrypto`) for NGS auth.
- `src/agents/AgentProtocol.{h,cpp}` — v0.3 layer: `sendPrompt` → streaming
  ack/response/terminator/error signals; `discover()` ($SRV scatter-gather) +
  `startHeartbeatWatch()` (agents.hb.*.*.*) → `agentsDiscovered`/`heartbeat`.
- `src/agents/{AgentModel,ChatModel}.{h,cpp}` — roster + conversation list models.
- `src/notes/NoteStore.{h,cpp}` — lists notebooks from the xochitl store for the
  attachment browser; per page exposes the **`.rm` path** (+ thumbnail fallback) and
  includes any page with a `.rm` (v2 renders it; no longer thumbnail-gated). Root:
  `$AGENT_CHAT_XOCHITL` or the device default.
- `src/rm/*` (v2) — in-app `.rm` v6 renderer. `RmTaggedReader` (byte cursor: varuint,
  tagged values, subblocks), `RmTypes.h` (Block/Pen/PenColor enums + Point/Stroke/Layer/
  Page), `RmParser` (header + block loop → strokes; bounds each block body in its own
  reader so a bad item can't poison the loop), `RmRenderer` (`Page`→`QImage` PNG via
  QtGui, auto-fit bbox). Pure parsing depends only on `QByteArray` → headless-testable.
- `src/agents/AppController.{h,cpp}` — QML-facing facade (`App` context property);
  loads static roster (`$AGENT_CHAT_CONFIG` / `./agents.json` / bundled / built-in).
- `src/qml/*` — hand-rolled flat UI (roster grid of `AgentDelegate` cards, chat
  `MessageDelegate` bubbles, on-screen `Keyboard.qml`). `Theme.qml` is a **singleton**
  (must be flagged `QT_QML_SINGLETON_TYPE` in CMake or `Theme.*` resolves to undefined).
- `src/main.cpp` — wires it together; sets e-paper hints (no cursor blink, no text
  AA). Headless verification (no QML/display): `AGENT_CHAT_SMOKE=<text>` = prompt
  round-trip; `AGENT_CHAT_DISCOVER=1` = $SRV discovery + heartbeat probe;
  `AGENT_CHAT_TEST=chat` = ChatModel self-test; `AGENT_CHAT_TEST=notes` = NoteStore
  self-test (`$AGENT_CHAT_XOCHITL`); `AGENT_CHAT_TEST=render` = parse one or more `.rm`
  (`AGENT_CHAT_RENDER_IN=<f.rm>[,<f2.rm>…]`) + save to `AGENT_CHAT_RENDER_OUT` (a `.pdf`
  ext → one multi-page **vector PDF**; else first page → **PNG**); knobs
  `_ROT/_SCALE/_PEN/_UNIFORM`; prints byte-exactness + stroke/point/bbox stats. Add
  `AGENT_CHAT_TLS=1 AGENT_CHAT_CREDS=<.creds>`
  + `AGENT_CHAT_SMOKE_HOST=connect.ngs.global` to target NGS; `AGENT_CHAT_ATTACH=p1,p2`
  to attach files in the smoke prompt.

## Scripts
- `scripts/inspect-device.sh` — read-only capability probe of the connected device.
- `scripts/run-desktop.sh`    — build + run the binary on the host (Qt under `~/Qt`;
  override `QT_ROOT`). It builds because the preview now needs the C++ types.
- `scripts/build-device.sh`   — cross-compile (set `RM_SDK_ENV` to the SDK env file).
- `scripts/deploy.sh`         — scp to device + on-device run steps.
- `scripts/echo-responder.py` — zero-config §6 echo stub (nats-py only) for tests.
- `scripts/smoke-test.py`     — headless end-to-end: nats-server + stub + the binary's
  `AGENT_CHAT_SMOKE` path; asserts the streamed reply. Needs `nats-server` + nats-py.

## On-device run sequence
`systemctl stop xochitl` → `QT_QUICK_BACKEND=epaper ./rm-agents -platform epaper`
→ **tap Exit** → `systemctl start xochitl`. Ctrl-C usually isn't delivered over
ssh-without-PTY; the app also quits on SIGINT/SIGTERM/SIGHUP. Always restore xochitl
(a `trap ... EXIT` one-liner is in `deploy.sh` / README §4).
- Deploy with `RM_SERVER=nats://<host>:4222 scripts/deploy.sh` — the device has no
  keyboard yet (M3), so the server is written to `~/agents.json` and read on launch;
  the roster then fills via discovery.
- Non-disruptive check (no xochitl stop): `AGENT_CHAT_DISCOVER=1
  AGENT_CHAT_SMOKE_HOST=<host> QT_QPA_PLATFORM=offscreen ./rm-agents`.

## Dev loop
inspect device → propose architecture (wait for OK) → implement a milestone →
keep `run-desktop.sh` working → cross-compile with `build-device.sh` → deploy &
test on real e-paper → iterate. Ask before any decision affecting portability or
the protocol.

## Refs
- **Protocol spec (authoritative):** `/home/m64/space/synadia-ai/nats-ai-docs/core-protocol.md`
- Synadia agent/client SDKs + example agents: `/home/m64/space/synadia-ai/synadia-agents`
- SDK: https://developer.remarkable.com/documentation/sdk
- Qt Quick on e-paper: https://developer.remarkable.com/documentation/qt_epaper
- Examples: https://github.com/reMarkable/remarkable-developer-examples
