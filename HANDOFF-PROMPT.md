# HANDOFF — build v2: in-app reMarkable `.rm` → image renderer

*Paste this to start a fresh Claude Code session in this repo. It assumes you know
nothing from prior sessions.*

## TL;DR
This repo (`remarkable-nats-agents`) is a **mature, working** multi-agent AI chat
client for the **reMarkable Paper Pro**, talking to agents over **NATS**. Milestones
**M1–M6 are done and verified** on real hardware and on Synadia Cloud (NGS).

**Your job (v2):** build an **in-app C++ renderer** that turns a reMarkable
handwritten page (`.rm` v6) into a **full-resolution PNG** (then PDF), and wire it
into the existing attachment flow so **any page** can be attached to a prompt — not
just pages that happen to have a low-res device thumbnail.

## Read first (in this order)
1. **`CLAUDE.md`** (auto-loaded) — project brief, status, constraints, protocol, code layout.
2. **`README.md`** — overview, features, build/run.
3. **`docs/RM-PARSER-RENDERER.md`** — *the v2 plan + a byte-for-byte-validated `.rm` v6
   format spec.* This is your primary spec.
4. **`docs/FILE-STORE.md`** — how the device stores notes (where `.rm` + thumbnails live).
5. **`docs/READING-NOTES.md`** — why thumbnails were the v1 shortcut, and their limits.
6. **`docs/IMPLEMENTATION-NOTES.md`** — architecture + gotchas (esp. headless verification, e-paper).

## Why v2 (context)
M6 v1 attaches the device's **512×384 page thumbnails**. A vision LLM *can* read them
(verified end-to-end against the NGS `pi` agent — it read handwriting back). But (a)
they're low-res, and (b) they're **lazy**: many pages have no thumbnail at all (live
device: 36 notebooks → only **13** with any rendered page; some have no `.thumbnails`
dir). The user has decided we want our **own full-res renderer**: better quality, and
every page becomes attachable.

## What v2 is (no external tools — device has no python/java/node)
Pure C++ parser + renderer; output via **QtGui** (`QPainter`→PNG, `QPdfWriter`→PDF) —
already available, no new deps, cross-compiles. Per `docs/RM-PARSER-RENDERER.md`:
- `src/rm/RmTaggedReader.{h,cpp}` — `QByteArray` cursor: varuint, fixed ints, f32/f64,
  bool, crdt_id, tag dispatch, subblock bounds.
- `src/rm/RmTypes.h` — Block / Pen / PenColor enums; Point / Stroke / Layer / Page structs.
- `src/rm/RmParser.{h,cpp}` — 43-byte header + block loop → `Page{ layers[ strokes[
  points ] ], text }`. **SceneLineItem (0x05)** is the primary target. Watch the two
  things references get wrong (both in the spec): the **SceneItem envelope precedes the
  Line**, and **subblock(6) is optional**. Point layout is chosen by the block's
  `current_version` (**v2 = 14-byte quantized**, the Paper Pro case). Skip unknown
  blocks/tags by length (forward-compat).
- `src/rm/RmRenderer.{h,cpp}` — `Page` → `QImage` (PNG) / `QPdfWriter` (PDF). Coords
  are **center-origin floats**; calibrate page W/H + origin + scale against a known
  note until it lines up with its thumbnail.
- Copy the authoritative **Pen/PenColor** integer tables from `rmscene` at impl time
  (don't hardcode guesses; ids 11/12 were unmapped in our sample).

**Milestones (each shippable):** (1) RmTaggedReader + RmParser + RmRenderer→PNG,
calibrate vs thumbnail; (2) PDF + multi-page (page range → one PDF, the compact
single attachment); (3) typed text (`RootText` 0x07 → Markdown) for typed pages;
(4) polish: pressure→opacity, colors, layers, eraser.

## Integration point (don't change the protocol/UX layers)
- `src/notes/NoteStore.{h,cpp}` lists notebooks and, per page, the **thumbnail path**.
  The raw stroke files live at `<xochitl>/<docUuid>/<pageId>.rm` (see FILE-STORE.md).
  v2: render `.rm` → PNG and feed *those* into the attach flow, and **include pages
  that have a `.rm` but no thumbnail** (NoteStore currently filters those out).
- `src/agents/AppController.cpp`: `stageNotePages()` builds the attachment list;
  `sendPrompt()` enforces `attachments_ok` + `max_payload` (1 MB). Full-res PNGs are
  bigger than thumbnails → mind the limit; a multi-page **PDF** is the compact path.

## Test fixture (desktop, no device needed)
- `test-data/rm-sample-AI-Workslop/` (gitignored) is a real note: a `<docUuid>/` dir
  with two `<pageId>.rm` files, plus `<uuid>.content` (page order) and
  `<uuid>.thumbnails/<pageId>.png` (**ground truth to calibrate against**). This note
  parsed byte-exact during spec validation.
- Point tests/app at it with `AGENT_CHAT_XOCHITL=$PWD/test-data/rm-sample-AI-Workslop`.

## Environment (this machine)
- **Build host:** Linux laptop (x86_64, Ubuntu 24.04).
- **Desktop Qt:** 6.8.2 at `~/Qt/6.8.2/gcc_64` (aqtinstall). `scripts/run-desktop.sh` uses it.
- **Cross SDK:** ferrari SDK at `~/rm-sdk`; env file
  `~/rm-sdk/environment-setup-cortexa53-crypto-remarkable-linux` (exports
  `CMAKE_TOOLCHAIN_FILE`). `scripts/build-device.sh`.
- **Device:** Paper Pro, `root@10.11.99.1` (USB), **on the same Wi-Fi** as the laptop.
  OS 5.7.121, Qt 6.8.2, panel **1620×2160**. Deploy: `scripts/deploy.sh` (atomic
  rename, safe while running). **Binary: `rm-agents`.** On-device run:
  `systemctl stop xochitl; trap "systemctl start xochitl" EXIT; QT_QUICK_BACKEND=epaper ./rm-agents -platform epaper`
  (quit via the in-app **Exit** button — Ctrl-C isn't delivered over ssh).
- **NATS for testing:** a local `nats-server` on the laptop
  (`nats://192.168.178.35:4222` on the LAN; `127.0.0.1` from the laptop). NGS: context
  `ngs-premium` → `tls://connect.ngs.global`, creds `~/.config/nats/ngs-premium.creds`.
  On NGS the **`pi`** agent (`agents.prompt.pi.m64.hello-remarkable`) answers prompts
  (a vision LLM — ideal for testing rendered attachments). Tools: `nats` CLI at
  `~/go/bin/nats`; nats-py + Synadia SDK in `~/.venvs/natstest`; example agents at
  `/home/m64/space/synadia-ai/synadia-agents/agent-sdk/python/examples/`.

## Build & verify headless (the agent env has NO display)
- Build: `scripts/run-desktop.sh`, or
  `cmake -S . -B build-desktop -DCMAKE_PREFIX_PATH=~/Qt/6.8.2/gcc_64 && cmake --build build-desktop --parallel`
  → `build-desktop/rm-agents`.
- `qmlcachegen` (in the build) catches QML type errors. For runtime QML errors, run
  offscreen: `QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=~/Qt/6.8.2/gcc_64/lib timeout 3 ./build-desktop/rm-agents` and watch stderr.
- Env-gated self-tests live in `src/main.cpp`: `AGENT_CHAT_TEST=chat`,
  `AGENT_CHAT_TEST=notes` (+ `AGENT_CHAT_XOCHITL`), `AGENT_CHAT_SMOKE=<text>`
  (+ `AGENT_CHAT_TLS=1 AGENT_CHAT_CREDS=… AGENT_CHAT_SMOKE_HOST=connect.ngs.global`
  for NGS; `AGENT_CHAT_ATTACH=p1,p2` to attach). **Add an
  `AGENT_CHAT_TEST=render <file.rm>` mode** to parse + save a PNG, for headless
  validation and thumbnail diffing.
- **Sandbox gotcha:** the Bash tool's sandbox kills processes that bind a listening
  socket or run as background jobs with **exit 144**. To run a local nats-server +
  agent + the binary, do it in ONE **foreground** call with
  `dangerouslyDisableSandbox: true`, orchestrated by a single process that tears
  children down (see `scripts/smoke-test.py`). Cross-compiling, scp/ssh deploy, and
  connecting to NGS also need `dangerouslyDisableSandbox: true`.

## Workflow the user expects
- Inspect/read → **propose architecture + file plan + milestones, wait for OK** before
  large changes. Ask before any portability/protocol decision.
- Implement a milestone → verify headless → cross-compile → deploy → **test on the
  real device** before calling it done. Keep `run-desktop.sh` working at every step.
- **Commit at each milestone**; keep `README.md` / `CLAUDE.md` / `docs/*` current.
- Constraints: pure Qt Quick/QML, Qt6 ≥ 6.5, no Widgets/WebEngine, e-paper-first
  (mono/high-contrast/no animation), bundle assets via `qt_add_qml_module`, only Qt
  modules present in BOTH the SDK sysroot and the device. The renderer uses QtGui
  (`QPainter`/`QImage`/`QPdfWriter`) — confirm Qt6::Gui is linked (transitive via
  Quick; add explicitly if needed).

## Suggested first steps
1. Read the files above (especially `docs/RM-PARSER-RENDERER.md`).
2. Confirm the fixture exists (`ls test-data/rm-sample-AI-Workslop`) and the desktop
   build runs (`scripts/run-desktop.sh`, then quit).
3. Propose the `src/rm/` file plan + milestones; wait for OK.
4. Implement `RmTaggedReader` + `RmParser`; add `AGENT_CHAT_TEST=render`; parse the
   fixture's `.rm` to the exact final byte (the spec says it's achievable).
5. `RmRenderer`→PNG; calibrate geometry vs the fixture thumbnail; then wire into
   `NoteStore` / the attach flow.

*(You can delete this file once v2 is underway.)*
