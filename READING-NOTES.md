# READING-NOTES.md — making handwritten notes readable by an LLM

Companion to [FILE-STORE.md](FILE-STORE.md). That doc covers how documents are *stored*;
this one covers how to get a **handwritten note's content into an LLM**, and the
**key shortcut that makes the attachments feature easy**.

Probed live on the Paper Pro (`ferrari`) on **2026-06-08** with the note **"AI Workslop"**
(UUID `7e25d0b9-c321-4899-af2a-34b054962d34`). Re-verify before relying on details.

## TL;DR — the shortcut

**The device already renders every page to a PNG thumbnail.** A vision LLM can read those
PNGs directly — including handwriting — with **no `.rm` decoding and no OCR step**. So the
attachment flow is simply: *pick note → pick page range → attach the matching thumbnail PNGs.*

```
<UUID>.thumbnails/<PAGE_UUID>.png      ← rendered image of each page, LLM-readable
```

The page order is the order of `cPages.pages[]` in `<UUID>.content`; each entry's `id`
is the `<PAGE_UUID>` whose `.png` (and `.rm`) live in the bare-UUID dirs. So a page range
maps deterministically to a list of thumbnail filenames.

## The two ways to read a note (why thumbnails win for v1)

| Path | Input | Steps | LLM-readable? | Cost |
|------|-------|-------|---------------|------|
| **Thumbnails** (chosen) | `<UUID>.thumbnails/*.png` | copy the PNGs | **Yes, directly** (vision) | none — already rendered on device |
| On-device OCR | `<UUID>.textconversion` | read JSON if present | Yes (text) | only exists for pages the user converted on-device |
| Full render | `<UUID>/<page>.rm` | decode (`rmscene`/`rmc`/`drawj2d`) → SVG/PNG → (OCR for text) | after decode | new dependency; needed only for higher fidelity |

For **"AI Workslop"** there was **no `.textconversion`**, and the host had **no `.rm`
renderer** (`rmc`/`rmscene`/`drawj2d`/ImageMagick all absent) — yet the thumbnails were
enough to read the whole note. That's the whole point: thumbnails need nothing extra.

## What was actually verified

- Note = `fileType: "notebook"`, **2 pages**, strokes in `.rm` **version 6** (binary,
  **not** directly readable by an LLM).
- Thumbnails were **512×384 RGB PNG** — low-res, but legible.
- Reading them back: a vision LLM transcribed both pages with only a few uncertain words
  on the handwritten page. (One page was handwriting; the other was typed text — both read
  fine.) So **low-res thumbnails are good enough for a useful read**, with occasional
  ambiguity on messy handwriting.

## How to find a note by its human name

Filenames are UUIDs; the human name is `visibleName` in `<UUID>.metadata`. To locate a
note (device userland is BusyBox, no `python3`):

```sh
DEV=${RM_DEVICE:-root@10.11.99.1}
ssh "$DEV" 'cd /home/root/.local/share/remarkable/xochitl
  for f in *.metadata; do
    case "$(grep visibleName "$f")" in *[Ww]orkshop*|*[Ww]orkslop*) echo "$f -> $(grep visibleName "$f")";; esac
  done'
```

## How the bundle was copied off the device

```sh
DEV=${RM_DEVICE:-root@10.11.99.1}
U=7e25d0b9-c321-4899-af2a-34b054962d34
OUT=~/rm-export/AI-Workslop; mkdir -p "$OUT"
# grabs <U>.metadata, <U>.content, <U>/ (.rm pages), <U>.thumbnails/ (.png pages)
scp -q -r "$DEV:/home/root/.local/share/remarkable/xochitl/${U}*" "$OUT/"
```

## Design for the attachments feature (thumbnail-based)

1. **List/pick:** walk `*.metadata` (skip `deleted`/folders), show `visibleName` + folder
   path (see FILE-STORE.md), filter by `fileType`.
2. **Page range:** read `<UUID>.content` → `cPages.pages[]` to get ordered page UUIDs and
   count; let the user pick a range (e.g. pages 2–5 → those page UUIDs).
3. **Attach:** copy the corresponding `<UUID>.thumbnails/<PAGE_UUID>.png` files and attach
   them to the prompt. Done — no decode, no OCR.
4. **PDF/EPUB docs:** attach the original `<UUID>.pdf` / `<UUID>.epub` instead (already
   text-readable).

### Quality / upgrade path
- Thumbnails are ~512×384 — fine for "read my note," lossy for dense/small handwriting.
- If a sharper read is needed later: render strokes at full res via `rmscene`/`rmc`
  (v6-aware) or `drawj2d` → PNG/SVG, and/or prefer an existing `<UUID>.textconversion`.
- Keep the thumbnail path as the default; treat full render as an optional "high fidelity"
  toggle.

### Caveat (same as FILE-STORE.md)
Reading thumbnails still means the app touches the user's whole library and ships page
images off-device to an LLM — a real privilege/consent step. Decide scope deliberately.
