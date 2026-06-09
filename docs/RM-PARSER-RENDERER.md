# RM-PARSER-RENDERER.md — rendering `.rm` notes to PDF/PNG inside our Qt app

Implementation reference for an **in-app C++ renderer** that turns a reMarkable
handwritten page (`.rm` v6) into a **PNG or PDF** we can attach and send to an agent.
Companion to [FILE-STORE.md](FILE-STORE.md) (storage) and
[READING-NOTES.md](READING-NOTES.md) (why thumbnails are the zero-dep v1).

Format details below are distilled from the reference implementations (see
[References](#references)); byte-level facts re-verify against `rmscene` at port time.
Device facts probed live on `ferrari` (Paper Pro) **2026-06-08/09**.

## Why in-app C++ (not an external tool)

Verified on the device — **no scripting/runtime is present**:

```
python: absent   python3: absent   java: absent   node: absent   perl: absent   lua: absent
```

So every off-the-shelf converter is unusable on-device: `rmc`/`rmscene` need **Python**,
`drawj2d` needs **Java**, `lines-are-rusty` would need a shipped static binary. We already
**cross-compile a Qt/C++ app with the reMarkable SDK** — so we implement the parser +
renderer in-process. No new runtime, no extra binary to maintain.

### Output: what we can emit on-device (verified)
- **PNG** — `QImage` + `QPainter`, in **QtGui** (already linked). Always available. ✅
- **PDF** — **`QPdfWriter`**, also in **QtGui** (NOT the missing `libQt6Pdf`, which is the
  *viewer* module QtPdf — we only need the *writer*). ✅
- **SVG** — would need `QtSvg`/`QSvgGenerator`; **not yet verified** present. Skip for now;
  PDF/PNG are better vision inputs anyway. ⚠️ verify before relying on it.

> We control the raster size, so rendering at full page resolution gives a far sharper
> result than the device's 512×384 thumbnails — the whole point of doing this.

## The `.rm` v6 binary format (what the parser must read)

A `.rm` file = **fixed header** + a sequence of **tagged blocks**. One file = one page.

### File header
A fixed-width ASCII string, **43 bytes**, padded with spaces:
```
"reMarkable .lines file, version=6          "
```
We confirmed `version=6` on real Paper Pro notes. Parser should read the 43-byte header
and assert the version digit (handle `5` and `6`; reject others gracefully).

### Tagged-value encoding (the core primitive)
Every field is prefixed by a **tag** read as a `varuint`:
`tag = (field_index << 4) | tag_type`. The reader checks both the expected index and type.

| tag_type | meaning | payload |
|----------|---------|---------|
| `0xF` | ID (CrdtId) | `uint8` author + `varuint` timestamp |
| `0xC` | Length4 / **subblock** | `uint32` byte-length, then nested tagged data |
| `0x8` | Byte8 | 8-byte `double` |
| `0x4` | Byte4 | 4-byte (`int`/`float32`) |
| `0x1` | Byte1 | 1-byte (`bool`) |

> **Correction (v2 impl, 2026-06-09):** an earlier draft of this table had the
> tag_type nibbles inverted (`ID=0x0`, `Length4=0x1`, …). The values above are the
> authoritative ones from rmscene's `TagType` enum (`tagged_block_common.py`) and
> are what the byte-validated fixture actually decodes with — e.g. the optional Line
> value subblock(6) appears on the wire as `0x6C = (6<<4)|Length4`. The wrong nibbles
> made every `SceneLineItem` parse as value-less (0 strokes); the corrected ones
> reproduce the exact expected counts (663 strokes / 10,268 points).

Primitive readers needed: `varuint`, `uint8/16/32`, `float32`, `float64`, `bool`,
`crdt_id`, plus tagged wrappers `read_id/int/float/double/string/subblock(expected_tag)`.

### Block header (precedes every block)
```
uint32  block_length      // bytes of this block's body
uint8   unknown           // = 0
uint8   min_version
uint8   current_version   // <-- selects sub-formats (e.g. point layout, see below)
uint8   block_type        // table below
```
Iterate: read header → dispatch on `block_type` → skip `block_length` bytes if unknown
(robust forward-compat — unknown/new blocks must NOT abort the parse).

### Block types
| ID | Block | Relevance to rendering |
|----|-------|------------------------|
| `0x00` | MigrationInfo | skip |
| `0x01` | SceneTree | structure |
| `0x02` | TreeNode | **layers/groups** (name, visibility) |
| `0x03` | SceneGlyphItem | text glyph (PDF highlights) |
| `0x04` | SceneGroupItem | group reference |
| `0x05` | **SceneLineItem** | **the strokes — primary render target** |
| `0x06` | SceneTextItem | typed text fragment |
| `0x07` | **RootText** | document typed-text block → extract as Markdown/plain |
| `0x08` | SceneTombstoneItem | **deletion marker — skip these items** |
| `0x09` | AuthorIds | author UUIDs |
| `0x0A` | PageInfo | page stats |
| `0x0D` | SceneInfo | scene metadata |

### SceneLineItem (`0x05`) — the stroke  ✅ verified byte-for-byte (see below)
A SceneLineItem is a **SceneItem** — it does **NOT** start with the Line. The block body
is a CRDT-sequence envelope, and the Line lives inside an **optional** subblock(6):
```
// --- SceneItem envelope (all SceneItem blocks share this) ---
read_id(1)    parent_id        CrdtId
read_id(2)    item_id          CrdtId
read_id(3)    left_id          CrdtId
read_id(4)    right_id         CrdtId
read_int(5)   deleted_length   uint32
if has_subblock(6):            // OPTIONAL — value-less items exist (25/688 in our test note)
  read_subblock(6) {
    uint8     item_type        // == the subclass ITEM_TYPE
    <Line value>               // tags below
  }
// --- Line value (inside subblock 6) ---
tag 1  tool_id        int    -> Pen enum
tag 2  color_id       int    -> PenColor enum
tag 3  thickness_scale double // global width multiplier for the stroke
tag 4  starting_length float
tag 5  points         subblock (array of points; count = sublen / point_size)
tag 6  timestamp      CrdtId
tag 7  move_id        CrdtId  (optional)
tag 8  color_rgba     uint32  (optional; explicit ARGB, sw >= 3.3 / Paper Pro color)
```
**Two things the real bytes corrected** (a from-references-only doc gets these wrong):
1. the **CRDT envelope precedes the Line** — read it first, then subblock(6).
2. **subblock(6) is optional** — guard with `has_subblock(6)`; ~3.6% of items had no value
   and unconditionally reading it derails the whole parse.

`CrdtId = uint8 author + varuint value` (matters for byte accounting). `read_id/int/
double/float/subblock(idx)` each consume a 1-byte tag (`(idx<<4)|type`) then the payload.
Detect optional Line tags (7, 8) via *bytes-remaining-in-block*, not assumption.

### Point layout — selected by the block's `current_version`
**v1 point = 24 bytes**, all `float32`:
`x, y, speed, direction, width, pressure`
**v2 point = 14 bytes** (quantized — the common Paper Pro case):
```
x        float32
y        float32
speed    uint16   // = value * 4
width    uint16   // = value * 4
direction uint8   // = value * 255/(2π)
pressure uint8    // = value * 255
```
For rendering we mainly need **x, y, width** (and optionally pressure for opacity).

### Pen + color enums
`tool_id` → pen family (ballpoint, fineliner, marker, pencil, paintbrush, calligraphy,
highlighter, eraser, …; each has v1/v2 variants with distinct ints). `color_id` → palette
(black/gray/white + Paper Pro colors); when **tag 8** is present, prefer that explicit
**ARGB**. **Do not hardcode the integer tables here** — copy them verbatim from
`rmscene`'s `scene_items.Pen` / `PenColor` at implementation time to avoid drift.

## Validation — checked byte-for-byte against a real note (`AI Workslop`)

Not theory: a from-scratch decoder built from this spec parsed both pages of the real
Paper Pro note to the **exact final byte** (189237/189237 and 1230/1230, no leftover).

**Test fixture (in repo, gitignored):** `test-data/rm-sample-AI-Workslop/` — the full note
bundle. Use `…/7e25d0b9…34/ddd1a165-…e3fc.rm` (688 strokes) as the handwriting parser test,
`…/19caa375-…706f.rm` as the typed/RootText test, and the matching `.thumbnails/*.png` as
ground-truth for geometry calibration. (Personal content — not committed; copy also at
`~/rm-export/AI-Workslop/`.)

| Check | Result |
|-------|--------|
| Header | 43-byte `"reMarkable .lines file, version=6"` + space padding ✅ |
| Block header layout (`u32 len, u8 unk=0, u8 minv, u8 curv, u8 type`) | ✅ exact |
| Block types observed | `0x00,01,02,04,05,07,08,09,0A,0D` — all matched the table |
| Handwriting page | 688 `SceneLineItem` (663 with strokes, **25 value-less**), all `curv=2` |
| Point layout | **v2 / 14-byte**, `data_length % 14 == 0` held for every stroke ✅ |
| Decoded sanity | `tool=17` (Fineliner-v2), `color=0` (black); 10,268 points, ~15.5/stroke |
| Typed page | one `RootText (0x07)`, **zero** SceneLineItems — matches "typed → text lane" |

**Measured geometry (calibration input):** stroke coords were **center-origin** —
`X ∈ [−890, +942]`, `Y ∈ [+83, +2500]`; per-point `width` raw `uint16` 16–24 (× the
stroke's `thickness_scale`, here 2.0). So `map()` must place origin at page-center-X (not
top-left) and the page is tall (Y up to ~2500). Confirm full page extents on more notes
before freezing constants. Colors beyond black/gray appeared as ids 11/12 → pull the
exact `PenColor` table from rmscene.

> Takeaway: the **block framing and point format are solid**; the only things references
> alone got wrong were the **SceneItem envelope** and the **optional value subblock** —
> both now baked into the spec above.

## Parser design (`src/rm/`)

```
src/rm/
  RmTaggedReader.{h,cpp}   // QByteArray cursor: varuint, fixed ints, f32/f64, bool,
                           //   crdt_id, tag dispatch, subblock bounds
  RmTypes.h                // Block/Pen/PenColor enums, Point, Stroke, Layer, Page structs
  RmParser.{h,cpp}         // header + block loop -> Page{ layers[ strokes[ points ] ], text }
  RmRenderer.{h,cpp}       // Page -> QImage (PNG) / QPdfWriter (PDF)
```
- Pure parsing, no Qt types except `QByteArray`/`QIODevice` → unit-testable headless
  (fits the existing `AGENT_CHAT_SMOKE`-style no-display verification pattern).
- **Robustness rule (from rmscene):** keep going on unknown block/tag types — skip by
  recorded length. Newer firmware adds blocks; a note must still render.

## Renderer design

**Raster (PNG):**
```cpp
QImage img(W, H, QImage::Format_RGB32);  img.fill(Qt::white);
QPainter p(&img);  p.setRenderHint(QPainter::Antialiasing);
for (const Layer& L : page.layers) if (L.visible)
  for (const Stroke& s : L.strokes) {
    QPen pen(colorFor(s));                       // color_id/ARGB -> QColor
    pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
    // per-segment width = s.thickness_scale * pt.width (pressure optional -> alpha)
    QPainterPath path; path.moveTo(map(s.points[0]));
    for (...) path.lineTo(map(pt));
    p.strokePath(path, pen);
  }
img.save(out, "PNG");
```
**Vector (PDF):** same loop onto a `QPdfWriter`+`QPainter`; **one page per `.rm`**, so a
selected page range → a single multi-page PDF (ideal single attachment for the agent).

### Coordinate mapping / geometry (calibration step)
`.rm` coordinates are a centered float space (origin near page center; x roughly
`[-W/2, +W/2]`). reMarkable classic page = **1404×1872**; **Paper Pro differs** (larger,
color — our probe earlier noted the panel reports an odd scaled mode, so do **not**
hardcode). Plan: implement `map()` with a configurable page size + x/y offset + scale,
then **calibrate by rendering a known note and diffing against its thumbnail** until they
line up. Record the confirmed Paper Pro constants here once measured.

**Calibration results (v2 M1, 2026-06-09 — verified against the AI Workslop note):**
`RmRenderer` (src/rm) **auto-fits to the inked bounding box** (+`margin`, default 40
rm-units) at `scale=1` → a `1913×2497` px PNG for the handwriting page, far sharper
than the `512×384` thumbnail. Key findings:
- **No rotation needed.** Strokes are stored **already upright** (reading orientation).
  Rendering at `rotation=0` reproduces the thumbnail's content exactly (title top-left,
  two columns, cyan/magenta ink). `orientation: landscape` in `<uuid>.content` governs
  only the **page viewport shape**, not stroke rotation — so it is *not* applied as an
  image rotation. (Re-verify on a **portrait** note before assuming this holds for all.)
- **The thumbnail is the page viewport, not the full content.** It crops at the page
  fold; our auto-fit captures everything that scrolls below it (the last to-do lines are
  cut off in the thumbnail but fully rendered by us). This is a feature: the agent sees
  *all* the writing.
- **Pen width:** per-segment `point.width × thickness_scale × scale × penScale`
  (`point.width = raw_u16/4`). With `thickness_scale=2.0` (Fineliner-v2 "medium") the
  default knobs give natural, legible strokes; expose `penScale` for tuning.
- **Colours:** `color_id 0/1/11/12` → black/gray/**cyan**/**magenta** (the "unmapped"
  11/12 are CYAN/MAGENTA in rmscene's `PenColor`), confirmed against the thumbnail's ink.
- Whether to also offer a **page-framed** render (fixed page W/H + grid, matching the
  thumbnail's margins) is deferred — auto-fit is the better vision input and is what the
  attach flow will use. Freeze Paper Pro page W/H here if/when page-framing is added.

## Typed text — RootText (✅ done 2026-06-09)
`RootText` (`0x07`) carries the page's typed text as a CRDT sequence. `RmParser` decodes
it into `Page{ text, textX, textY, textWidth }` (concatenating the text-item strings in
file order; `RmTaggedReader::readString` reads the `varuint len + flag + UTF-8` runs).
`RmRenderer` draws the typed text into the image/PDF with a sans-serif font, word-wrapped
to `textWidth`.

**Mixed pages (typed + handwritten).** A page can have both a RootText block and strokes
(verified: the "AI Workslop" typed page after adding handwriting below it). reMarkable's
typed-text *vertical* coordinates don't map cleanly into stroke space — rendering both at
their raw coordinates **overlaps** them, even though the device thumbnail shows them
separated. So the renderer uses a **stacked layout**: the typed-text block on top, the
strokes below it, sharing the horizontal (x) axis. This is robust and matches how mixed
pages read; it deliberately ignores the absolute text Y (only font size / line advance /
wrap width are calibrated — `textFontPx≈64`, `textLineHeight≈70` rm-units). Verified
end-to-end: the NGS `pi` vision agent transcribed the typed text **and** the handwriting
from one rendered image, as separate blocks. (Bullet/checkbox paragraphs come through as
the `▎` glyph; per-paragraph styles/headings are a later refinement.)

> Pure-typed pages could alternatively attach the extracted string as **Markdown/plain
> text** (no image, no OCR — cheapest agent input). `Page.text` is already available for
> that; wiring it into the attach flow is a possible follow-up.

## Milestones (incremental, each shippable)
1. **v1 — thumbnails (no parser):** pick note → page range → attach existing
   `<UUID>.thumbnails/<page>.png`. Works today; low-res. (See READING-NOTES.md.)
2. **v2 — C++ raster renderer:** `RmTaggedReader` + `RmParser` (SceneLineItem only) +
   `RmRenderer`→PNG at full res. Calibrate geometry vs thumbnail. ✅ **DONE (2026-06-09)** —
   `src/rm/`, byte-exact parse of the fixture, upright full-res PNG matching the
   thumbnail. Headless via `AGENT_CHAT_TEST=render` (see below). Not yet wired into the
   attach flow (that's the next step).
3. **v3 — PDF + multi-page:** `QPdfWriter`, page-range → one PDF; pen widths/colors/layers.
   ✅ **DONE (2026-06-09)** — `RmRenderer::renderToPdf(vector<Page>)`: **vector** strokes
   (not embedded rasters), one PDF page per input page, strokeless pages skipped. Shares
   the stroke-drawing route with the PNG path (uniform width by default). Compact: ~112 KB
   per handwriting page vs ~448 KB for the PNG — comfortably multi-page under `max_payload`.
   Cross-compiles **and runs on the device** (QPdfWriter confirmed in the sysroot + at
   runtime). Headless: `AGENT_CHAT_TEST=render` with a `.pdf` `AGENT_CHAT_RENDER_OUT` and a
   comma-separated `AGENT_CHAT_RENDER_IN`. Not yet wired into the attach flow.
4. **v4 — typed text extraction:** RootText → Markdown for typed pages.
5. polish: pressure→opacity, highlighter blend, eraser handling, Paper Pro palette.

## Open items to verify before/while building
- [x] ~~Confirm point layout version on our notes~~ → **v2 / 14-byte** (verified on AI Workslop).
- [x] ~~Confirm block framing & SceneLineItem structure~~ → **verified byte-for-byte** (incl.
      SceneItem envelope + optional value subblock — spec updated).
- [ ] `QtSvg`/`QSvgGenerator` presence (sysroot + device) — only if we want SVG.
- [x] ~~`QPdfWriter` link/run check on-device~~ → confirmed: links in the sysroot and
      generates a valid multi-page PDF on the device (vector strokes).
- [ ] Full Paper Pro page extents + exact origin/scale — only needed if page-framing is
      added; auto-fit (M1) sidesteps it. coords are **center-origin**, `X∈[−890,942]
      Y∈[83,2500]` on the one note.
- [x] ~~Copy authoritative Pen/PenColor integer tables from `rmscene`~~ → done in
      `src/rm/RmTypes.h`; ids 11/12 = **CYAN/MAGENTA**.
- [ ] Confirm `rotation=0` (no stroke rotation) also holds for a **portrait**-orientation
      note — M1 only verified a landscape note.

## Privilege note (unchanged)
Rendering + attaching means the app reads the user's library and ships page images
off-device to an agent — a real consent/scope decision, same as in FILE-STORE.md.

## References
- **`rmscene`** (authoritative v6 reader, Python) — block types, tagged encoding, point
  layouts, Pen/PenColor enums, text CRDT: https://github.com/ricklupton/rmscene
- **`rmc`** (rmscene-based CLI → SVG/PDF/Markdown), the behavior we mirror in C++:
  https://github.com/ricklupton/rmc
- **`remarkable_lines`** (Rust, v3–v6) — second cross-check of the format:
  https://lib.rs/crates/remarkable_lines
- **reMarkable-kaitai** — precise grammar but **v3/v5 ONLY (no v6)**; use for the older
  base structures, not v6: https://github.com/matomatical/reMarkable-kaitai
- **lines format origins** (v3/v5 background):
  https://plasma.ninja/blog/devices/remarkable/binary/format/2017/12/26/reMarkable-lines-file-format.html
- **drawj2d** (Java; Paper Pro color export) — alt reference for color handling:
  https://drawj2d.sourceforge.io/
