# FILE-STORE.md — how the reMarkable stores documents

Reference for building **listing / filtering / attaching** of on-device files in our
app. Probed live on the connected Paper Pro (`ferrari`) on **2026-06-08** — re-verify
before relying on details; the format evolves across OS versions.

## Where

Everything lives in a single directory on the device:

```
/home/root/.local/share/remarkable/xochitl/
```

It is a **flat** directory keyed by **UUID** — there are *no* nested folders on disk.
The visible folder hierarchy is reconstructed from metadata (see below). Snapshot at
probe time: **416 items — 362 documents, 26 folders** (272 PDF, 38 EPUB, 74 notebook).

> Device userland is **BusyBox** (`head -n N`, not `head -N`; no `head -c`) and has
> **no `python3`**. Do heavy joins host-side (scp/tar the small JSON files over and
> process locally), or in C++ inside the app.

## Per-document files (sidecars sharing one UUID stem)

```
<UUID>.metadata        JSON — name, parent folder, type, dates, flags
<UUID>.content         JSON — fileType, page list, tool/zoom settings
<UUID>.pdf  | .epub    the original imported file (present only for pdf/epub docs)
<UUID>.pagedata        per-page template names (e.g. "Blank")
<UUID>.thumbnails/     page preview PNGs
<UUID>/                bare-UUID DIR — handwriting as *.rm stroke files (one per page)
<UUID>.textconversion  (sometimes) on-device handwriting-OCR output — machine-readable
<UUID>.highlights      (sometimes) highlight data
```

A given document has a subset of these. A pure notebook has no `.pdf`/`.epub`; an
imported PDF has a `.pdf` plus (if annotated) a `<UUID>/` stroke dir, etc.

## `.metadata` — names, folders, type

```json
{
  "visibleName": "esp32-wroom-32_datasheet_en",   // the HUMAN name (NOT the filename)
  "type": "DocumentType",                          // DocumentType | CollectionType(=folder)
  "parent": "e1c7bb49-9ea0-457a-a17a-aa1d882aff0b",// UUID of containing folder; "" = root
  "deleted": false,
  "pinned": false,
  "lastModified": "1663149056000",
  "createdTime": "",
  "lastOpened": "1663149056000",
  "lastOpenedPage": 0
}
```

- The on-disk filename is the **UUID** — the human-readable name is `visibleName`.
- **Folders are entries too**: `"type": "CollectionType"`, with their own `visibleName`
  and their own `parent`. There is no folder on disk — only this metadata record.
- Always skip records with `"deleted": true`.

### Reconstructing the folder path

Follow the `parent` UUID chain up to root (`parent == ""`), collecting each
ancestor's `visibleName`. This yields real paths like `Books/ITBooks/OReilly - SSH…`
or `Befunde/Radiologiebefund`. (Guard against cycles.)

## `.content` — what kind of document it is (the filter key)

The authoritative document-kind field is `fileType`:

| `fileType`   | meaning                                   | count @ probe |
|--------------|-------------------------------------------|---------------|
| `"pdf"`      | imported PDF                              | 272           |
| `"epub"`     | imported EPUB                            | 38            |
| `"notebook"` | pure handwritten note (no source file)   | 74            |

`.content` also holds `cPages.pages[]` (page UUIDs + template names), `coverPageNumber`,
zoom settings, and `extraMetadata` (last-used tool/color/size). For listing/filtering we
only need `fileType`; for attaching specific pages we'd use `cPages.pages[]`.

### How to filter PDFs vs EPUBs vs notes

Two equivalent strategies:
1. **Presence test:** a sibling `<UUID>.pdf` or `<UUID>.epub` exists.
2. **Authoritative:** read `<UUID>.content` → `fileType`.

Algorithm: iterate `*.metadata`; skip `deleted:true` and `CollectionType`; read the
matching `.content` `fileType`; bucket by kind; resolve folder path via the parent chain.
(This exact join was run host-side and cleanly separated the library.)

## Handwritten notes — the `.rm` "lines" format

Each notebook page is a binary stroke file inside the bare-UUID directory:

```
0c1ad240-…/a8c643af-….rm   →  header: "reMarkable .lines file, version=5"
0c1ad240-…/a8c643af-…-metadata.json   (small per-page metadata)
```

- Proprietary **`.rm` "lines"** binary: pen strokes (points, pressure, tilt, tool,
  color, layers). **Not** an image, **not** text. Probe device showed **version=5**;
  the Paper Pro also emits **v6** — handle both.
- **An LLM cannot read `.rm` directly.** Pipeline needed:
  1. **Decode** with a community lib — `rmscene`/`rmc` (Python, v6-aware) or
     `lines-are-rusty` (Rust) → render to **SVG / PNG / PDF**.
  2. For *text*, run **handwriting OCR** on that render (an OCR model, or reMarkable's
     own on-device "Convert to text").
- **Shortcuts:** existing `<UUID>.textconversion` files already hold on-device OCR text
  (machine-readable, no decoding); `<UUID>.highlights` holds highlight data.

## Implications for the attachments feature

Practical picker design:
1. Read `…/xochitl/` (read-only over SSH/scp, or via an on-device helper).
2. Walk `*.metadata` → build folder tree from `parent` chains; display `visibleName`.
3. Filter by `fileType`.
4. On pick:
   - **PDF / EPUB** → use the original `<UUID>.pdf` / `<UUID>.epub` as-is.
     **Works today**, no decoding — the easy win.
   - **Notebook** → use an existing `<UUID>.textconversion` if present, else run the
     `.rm → SVG → OCR` pipeline. v6 stroke decoding is the main new dependency.

### Privilege / design caveat

Today the app treats the device as a **thin client** that "never runs inference itself"
and holds **NATS credentials only**. Reading the xochitl store means the app (or a
helper) touches the user's **entire document library** — a materially larger privilege
surface. Make this a deliberate design decision (scope, consent, read-only access)
before building.

---

### Quick reference — commands used to probe (read-only)

```sh
DEV=${RM_DEVICE:-root@10.11.99.1}
X=/home/root/.local/share/remarkable/xochitl

# extension breakdown
ssh "$DEV" "ls $X | sed 's/.*\\.//' | sort | uniq -c | sort -rn"

# document kinds
ssh "$DEV" "cd $X && grep -ho '\"fileType\": *\"[a-z]*\"' *.content | sort | uniq -c"
ssh "$DEV" "cd $X && grep -h '\"type\"' *.metadata | sort | uniq -c"

# join names + types + folder paths host-side (device has no python3)
TMP=$(mktemp -d)
ssh "$DEV" "cd $X && tar cf - *.metadata *.content" | tar xf - -C "$TMP"
# …then walk $TMP/*.metadata in python/C++: skip deleted, resolve parent chain,
#   read matching .content fileType, bucket by kind.
```
