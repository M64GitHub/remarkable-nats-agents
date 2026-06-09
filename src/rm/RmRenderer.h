#pragma once

// Rasterises a parsed `Page` of strokes to a full-resolution image (PNG). Output
// uses only QtGui (QImage/QPainter), which is already linked and cross-compiles —
// no new dependency, far sharper than the device's 512x384 thumbnails.
//
// `.rm` coordinates are a centre-origin float space; we raster in that space
// (offset by the page bounding box) and then rotate the finished image for
// landscape notes — the canvas itself is always portrait. Geometry knobs are
// exposed so the result can be calibrated against a known note's thumbnail.

#include "rm/RmTypes.h"

#include <QImage>
#include <QString>

#include <vector>

namespace rm {

struct RenderOptions {
    double scale = 1.0;       // rm units -> output pixels (1.0 == full canvas res)
    double penScale = 1.0;    // multiplier on the rendered stroke width
    double margin = 40.0;     // padding around the content bbox, in rm units
    double minPenPx = 1.0;    // floor on rendered pen width, in output pixels
    int rotation = 0;         // post-raster rotation in degrees (0/90/180/270)

    // When > 0, every stroke is drawn at this single width (rm units) regardless
    // of pen/colour/pressure — uniform, clean lines (the e-paper-first default,
    // and what avoids coloured pens looking heavier than the black handwriting).
    // Set <= 0 to fall back to the faithful per-point width (thickness_scale *
    // decoded point width), the basis for a later pressure->opacity pass.
    double uniformWidth = 6.0;

    // Typed text (RootText) rendering, in rm coordinate units. Defaults calibrated
    // against the device thumbnail of a mixed typed+handwritten page. The typed
    // block is stacked above the strokes (reMarkable's typed-text vertical
    // coordinates don't map cleanly into stroke space), so only the font size,
    // line advance, and wrap width matter — not the absolute text Y.
    double textFontPx = 64.0;       // glyph size
    double textLineHeight = 70.0;   // per-line vertical advance
    bool drawText = true;           // render typed text into the image/PDF
};

class RmRenderer {
public:
    // Render the page to an RGB image (white background, black-on-white strokes).
    static QImage renderToImage(const Page &page, const RenderOptions &opt = {});

    // Render and save as PNG. Returns false on an empty page or a write failure.
    static bool renderToPng(const Page &page, const QString &path,
                            const RenderOptions &opt = {});

    // Render a sequence of pages into one multi-page PDF (vector strokes via
    // QPdfWriter — far more compact than embedding full-res rasters, which keeps
    // a page range under the agent's max_payload). One PDF page per input page;
    // pages with no strokes are skipped. Returns false if nothing was drawn or
    // the file could not be written. `pageCountOut` (optional) receives the
    // number of pages actually emitted.
    static bool renderToPdf(const std::vector<Page> &pages, const QString &path,
                            const RenderOptions &opt = {}, int *pageCountOut = nullptr);
};

}  // namespace rm
