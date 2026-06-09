# remarkable-nats-agents

> A multi-agent **AI chat client that runs natively on the reMarkable Paper Pro**
> e-paper tablet, talking to AI agents over **NATS** (the Synadia Agent Protocol).
> Discover agents on the bus, chat with replies that **stream in token-by-token**,
> type on a **built-in on-screen keyboard**, and **turn your notes — handwritten, typed,
> or both — into full-resolution PNGs or PDFs with our own on-device `.rm` renderer** and
> attach them, all on the panel. Or run the very same app as a desktop preview for fast
> iteration.

Pure **Qt Quick / QML** (Qt 6.8), **e-paper-first** (monochrome, high-contrast, no
animations, large touch targets), cross-compiled with the official reMarkable SDK.
It connects to a local `nats-server` on your LAN **or** to **Synadia Cloud (NGS)**
over TLS with NKEY/JWT credentials — and you pick which right inside the UI.

Under the hood, the **NATS client is written from scratch in Qt/C++** — the wire
protocol spoken directly over a socket, with **no NATS client library** and no
native dependencies that don't cross-compile to the device.

> 🛠️ **Setting up?** See **[Prerequisites](#prerequisites)**. To build for the device
> you'll need the reMarkable **ferrari SDK** —
> [download it here](https://developer.remarkable.com/links) (matches your device's
> OS version).

---

## Features

- **A from-scratch NATS client in Qt/C++** — no NATS library. The wire protocol is
  implemented directly over `QSslSocket`: INFO/CONNECT handshake, PING/PONG,
  PUB/SUB/UNSUB, MSG **+ HMSG (headers)**, the request/reply inbox + streaming
  consumer, the TLS upgrade, and **NKEY/JWT (Ed25519)** auth for NGS. It sits behind
  a clean `INatsConnection` seam (swappable later, e.g. for `nats.zig`).
- **Agent roster** via NATS micro-service discovery (`$SRV`), shown as a grid of
  cards with **live heartbeat status** — agents light up as they come online and
  fade as they go.
- **Streaming chat** — prompts go out request/reply; the agent's answer streams back
  chunk-by-chunk into the bubble. **Per-agent history** is preserved as you switch.
- **Built-in on-screen keyboard** — the device ships no system keyboard, so this one
  is hand-rolled in QML (a hardware / Bluetooth keyboard works too).
- **Your notes → image, rendered on the device** ✍️ — a **from-scratch C++ parser +
  renderer for the reMarkable `.rm` v6 format** turns your strokes into a **full-resolution
  PNG** or a compact **multi-page vector PDF**. It renders **handwriting, typed text, and
  mixed pages** (typed notes with handwritten annotations) — the typed text comes straight
  from the file, so it's pixel-sharp, not OCR'd. Browse notebooks, pick a **page range**,
  choose **PNG or PDF**, and attach it; a **vision agent reads your notes back to you**.
  Renders *any* page (not just ones the device happened to thumbnail) and far sharper than
  the device's 512×384 thumbnails. No Python/Java/Node — none exist on the device — just
  QtGui. See **[the renderer](#the-handwriting-renderer-rm--png--pdf)** below.
- **Local _or_ cloud** — a plaintext LAN `nats-server`, or **Synadia Cloud (NGS)**
  over TLS + NKEY/JWT. Switch endpoints with the in-app **NATS context picker**
  (reads your `~/.config/nats/context/*.json`).
- **E-paper-tuned** — message bubbles + timestamps, discrete repaints, steady cursor,
  non-anti-aliased text, and an in-app **Exit** (the device has no reliable Ctrl-C).
- **Two ways to run** — on the e-paper panel, and as a desktop window for fast dev.

## Architecture

```
        QML  (hand-rolled flat e-paper UI: roster, chat, keyboard, note browser)
                 │   talks only to ↓  (the "App" context property)
        AppController
           ├── AgentModel   — roster + heartbeat liveness
           ├── ChatModel    — per-agent conversations (streaming)
           ├── NoteStore    — notebook browser for attachments (lists .rm pages)
           └── rm/          — .rm v6 parser + renderer → PNG / multi-page PDF
                 │
        AgentProtocol   — Synadia Agent Protocol v0.3:
                          $SRV discovery · streaming prompts · heartbeats
                 │
        INatsConnection ◀── clean transport seam (swappable, e.g. nats.zig)
                 │
        NatsClient      — NATS wire over QSslSocket
                          (TLS + NKEY/JWT for NGS; plaintext for LAN)
```

The QML layer never touches NATS; everything above the wire depends only on
`INatsConnection`. See **[IMPLEMENTATION-NOTES.md](docs/IMPLEMENTATION-NOTES.md)** for the
design rationale and the hard-won gotchas.

## The handwriting renderer (`.rm` → PNG / PDF)

The device has **no Python, Java, or Node**, so every off-the-shelf `.rm` converter
(`rmc`/`rmscene`, `drawj2d`, …) is unusable on it. So we wrote our own, in C++, against
the **byte-validated reMarkable `.rm` v6 ("lines") format** — it runs in-process and
cross-compiles with the rest of the app, no new runtime.

```
src/rm/
  RmTaggedReader   — .rm byte cursor: varuint, fixed ints, f32/f64, tagged values, subblocks
  RmParser         — 43-byte header + block loop → Page{ layers[ strokes[ points ] ], text }
                     decodes SceneLineItem strokes + RootText typed text; skips unknown blocks
  RmRenderer       — Page → QImage (PNG)  ·  vector multi-page PDF via QPdfWriter
```

- **Full resolution, both formats** — a sharp **PNG per page**, or one compact
  **multi-page vector PDF** (a page range → a single ~100 KB-per-page attachment, vs a
  ~450 KB PNG). Pick **PNG or PDF** right in the attach dialog.
- **Handwriting, typed text, and mixed pages** — strokes are rendered from the pen data;
  **typed text** is parsed straight from the file's `RootText` block (so it's exact and
  pixel-sharp, not OCR'd) and drawn into the same image. A **mixed page** (a typed note
  with handwritten annotations) renders both — the typed block above, the handwriting
  below — just like it looks on the tablet.
- **Any page is attachable** — it reads the raw `<pageId>.rm`, so pages the device never
  rendered a thumbnail for work too (thumbnails are lazy; many pages never get one).
- **Faithful** — colours (incl. the Paper Pro palette), uniform clean stroke weight,
  upright for both portrait and landscape notes.
- **Verified end-to-end** against a live **vision agent** on NGS: it transcribed a
  rendered handwriting page, read back a multi-page PDF, and — from one rendered mixed
  page — returned the typed text *and* the handwriting as separate blocks.

Render headlessly (no display) for testing or thumbnail diffing:
```sh
# one PNG (first page):
AGENT_CHAT_TEST=render AGENT_CHAT_RENDER_IN=page.rm AGENT_CHAT_RENDER_OUT=/tmp/out.png \
  QT_QPA_PLATFORM=offscreen ./build-desktop/rm-agents
# one multi-page PDF from several pages, in order:
AGENT_CHAT_TEST=render AGENT_CHAT_RENDER_IN=p1.rm,p2.rm AGENT_CHAT_RENDER_OUT=/tmp/out.pdf \
  QT_QPA_PLATFORM=offscreen ./build-desktop/rm-agents
```
Format details and the calibration findings live in
**[RM-PARSER-RENDERER.md](docs/RM-PARSER-RENDERER.md)**.

## Status

| Milestone | What | State |
|-----------|------|-------|
| M1 | NATS transport + agent roster + chat + streaming prompt | ✅ |
| M2 | `$SRV` discovery + heartbeat liveness (dynamic roster) | ✅ |
| M3 | On-screen keyboard + full-panel layout | ✅ |
| M5 | TLS + NKEY/JWT for NGS / Synadia Cloud + context picker | ✅ |
| M6 | Attach notebook pages as images (v1: device thumbnails) | ✅ |
| v2 | In-app **`.rm` renderer** → full-res **PNG / multi-page PDF**: handwriting, **typed text & mixed pages**, page ranges, wired into the attach flow ([details](docs/RM-PARSER-RENDERER.md)) | ✅ |
| — | Render polish: per-paragraph text styles (headings/bullets), pressure→opacity, layers, eraser | planned |
| M4 | Mid-stream queries (§7) + `audit.*` activity feed | planned |

## Hardware & topology

- **Device** — reMarkable **Paper Pro** (code name `ferrari`, aarch64 cortex-a53,
  color e-paper). OS `5.7.121` (scarthgap), **Qt 6.8.2**, panel **1620×2160**.
  Developer mode + SSH (USB default `root@10.11.99.1`; override with `RM_DEVICE`).
- **Build host** — a **Linux laptop** (x86_64). The reMarkable SDK runs natively here;
  no Docker/emulation.
- **Agents / compute** — run elsewhere on the same NATS; the device is a thin client
  that never runs inference and **holds NATS credentials only** (no API keys).

---

## Prerequisites

- reMarkable **Developer Mode** + SSH access
  ([guide](https://developer.remarkable.com/documentation/developer-mode)). The device
  itself needs **nothing installed** — Qt, the `epaper` plugin and `libqsgepaper` are
  already on it.
- **Qt 6.8 on the Linux laptop** for the desktop preview/build (Ubuntu's apt Qt is
  6.4, below our floor). Easiest is `aqtinstall`:
  ```sh
  python3 -m venv ~/.venvs/aqt && ~/.venvs/aqt/bin/pip install aqtinstall
  ~/.venvs/aqt/bin/aqt install-qt linux desktop 6.8.2 linux_gcc_64 --outputdir ~/Qt
  ```
- **reMarkable ferrari SDK** (only for on-device builds) — a login-gated download from
  **<https://developer.remarkable.com/links>**; pick the build matching your device's
  OS version (a near version is fine — `build-device.sh` cross-checks the sysroot Qt).
  ```sh
  ./meta-toolchain-remarkable-<ver>-ferrari-public-x86_64-toolchain.sh -d ~/rm-sdk
  ```
- **For local protocol testing** — a `nats-server` on `PATH`, plus the Python NATS
  client for the test stub / example agents:
  ```sh
  python3 -m venv ~/.venvs/natstest && ~/.venvs/natstest/bin/pip install nats-py
  ```

---

## Quick start

### 1. Inspect the connected device (do this first)
```sh
scripts/inspect-device.sh        # read-only; set RM_DEVICE if not root@10.11.99.1
```
Reports OS, Qt/QML modules, the epaper plugins, input devices, screen, fonts, memory.

### 2. Iterate the UI on the desktop (no device needed)
```sh
scripts/run-desktop.sh           # builds with CMake (Qt from ~/Qt) and runs rm-agents
```
The preview is the real binary (it registers C++ types + bundles its QML). Touch
arrives as mouse events. It's a logic/layout check — never an e-paper fidelity check.

To browse notebooks in the desktop preview, point the note store at a sample copy of
the device's library:
```sh
AGENT_CHAT_XOCHITL=/path/to/xochitl-sample scripts/run-desktop.sh
```

### 3. Test the protocol end-to-end (no device, no display)
```sh
QT_ROOT=~/Qt/6.8.2/gcc_64 ~/.venvs/natstest/bin/python scripts/smoke-test.py
```
Starts `nats-server`, runs an echo stub, drives the app's headless prompt path, and
asserts the streamed reply. For a real, spec-compliant counterparty, run an example
agent from the Synadia SDK (registers on `$SRV`, heartbeats):
```sh
python examples/01-echo.py --url nats://127.0.0.1:4222 --owner local --session-name test
```

### 4. Connect to Synadia Cloud (NGS)
The client speaks TLS + NKEY/JWT, so it can reach agents on `tls://connect.ngs.global`.
In the UI, tap the **context picker** to choose an NGS context, or set the server to
`tls://connect.ngs.global` and provide a `.creds` file (via config or
`AGENT_CHAT_CREDS`). Cert verification uses the system CA bundle.

---

## Cross-compile & run on the device

### Build
```sh
scripts/build-device.sh          # sources the SDK env; set RM_SDK_ENV to override
```
Produces `build-device/rm-agents`, an aarch64 ELF (verify with `file`).

### Deploy (binary + where to connect)
The device has no on-screen keyboard until you're in the app, so set the NATS server
at deploy time — it's written to `~/agents.json` and read on launch.
```sh
# local plaintext server:
RM_SERVER=nats://192.168.1.50:4222 scripts/deploy.sh        # use YOUR server's IP:port
# …or Synadia Cloud (the .creds file is copied too, chmod 600):
RM_SERVER=tls://connect.ngs.global RM_CREDS=~/.config/nats/ngs-premium.creds scripts/deploy.sh
```
To get the in-app **NATS context picker** on the device (it has no NATS CLI), push
selected contexts — their creds paths are rewritten + copied automatically:
```sh
RM_CONTEXTS=ngs-premium,work scripts/deploy.sh   # then the picker appears in the roster
```

### Run on the panel
The app draws straight to the e-paper, so stop the stock UI (`xochitl`) first, then
restore it when you quit. Safest is the one-liner that **always** restores xochitl —
quit cleanly with the in-app **Exit** button (top-right of the roster):
```sh
ssh root@10.11.99.1 'systemctl stop xochitl; trap "systemctl start xochitl" EXIT; \
  QT_QUICK_BACKEND=epaper ./rm-agents -platform epaper'
```
> ⚠️ Ctrl-C usually isn't delivered over `ssh` without a PTY — use **Exit** in the app
> (it quits cleanly, firing the trap that brings `xochitl` back).

**Sanity check without stopping xochitl** (headless; confirms the device reaches your
server and lists agents):
```sh
ssh root@10.11.99.1 'AGENT_CHAT_DISCOVER=1 AGENT_CHAT_SMOKE_HOST=<server-ip> \
  QT_QPA_PLATFORM=offscreen ./rm-agents'
```

---

## Documentation

- **[CLAUDE.md](CLAUDE.md)** — project brief, current status, constraints, protocol.
- **[IMPLEMENTATION-NOTES.md](docs/IMPLEMENTATION-NOTES.md)** — architecture rationale and
  the gotchas (Qt/QML, NATS wire, TLS/NGS, e-paper refresh, attachments).
- Attachments + renderer: **[RM-PARSER-RENDERER.md](docs/RM-PARSER-RENDERER.md)** (the
  `.rm` v6 format + the renderer's design and calibration),
  **[FILE-STORE.md](docs/FILE-STORE.md)** (how the device stores documents),
  **[READING-NOTES.md](docs/READING-NOTES.md)** (thumbnails as LLM input — the v1 shortcut).
- Protocol: the **Synadia Agent Protocol** (v0.3) the client implements.

## Notes & constraints

- Pure Qt Quick only — **no Qt Widgets, no WebEngine**. UI is hand-rolled flat QtQuick.
- E-paper-first: no animations/gradients/spinners; high contrast; large targets.
- The device holds **NATS credentials only** — no Anthropic/API keys on the device.
- `libqsgepaper.so` is already on this device (5.7.121) — no copy needed.
- Paper Pro doesn't need the rm1/rm2 touch rotate/invert env vars.

## References

- reMarkable SDK: <https://developer.remarkable.com/documentation/sdk>
- Qt Quick on e-paper: <https://developer.remarkable.com/documentation/qt_epaper>
- SDK downloads (ferrari toolchain): <https://developer.remarkable.com/links>
- Official examples: <https://github.com/reMarkable/remarkable-developer-examples>
