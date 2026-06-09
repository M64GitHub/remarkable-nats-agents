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
};

class RmRenderer {
public:
    // Render the page to an RGB image (white background, black-on-white strokes).
    static QImage renderToImage(const Page &page, const RenderOptions &opt = {});

    // Render and save as PNG. Returns false on an empty page or a write failure.
    static bool renderToPng(const Page &page, const QString &path,
                            const RenderOptions &opt = {});
};

}  // namespace rm
