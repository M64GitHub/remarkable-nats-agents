#include "rm/RmRenderer.h"

#include <QColor>
#include <QMarginsF>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPointF>
#include <QSize>
#include <QSizeF>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace rm {

namespace {

// reMarkable panel density — used to give the PDF a sensible physical page size
// (rm coordinate units are ~screen pixels, so px / dpi = inches).
constexpr double kPdfDpi = 226.0;

// Map a PenColor id to an on-screen colour. Black/gray/white plus the Paper Pro
// palette; an explicit per-stroke ARGB (Line tag 8) overrides this when present.
QColor colorForId(int id)
{
    switch (PenColor(id)) {
    case PenColor::White:       return QColor(255, 255, 255);
    case PenColor::Gray:
    case PenColor::GrayOverlap: return QColor(128, 128, 128);
    case PenColor::Yellow:
    case PenColor::Yellow2:     return QColor(240, 200, 0);
    case PenColor::Green:
    case PenColor::Green2:      return QColor(0, 150, 60);
    case PenColor::Pink:        return QColor(230, 90, 160);
    case PenColor::Blue:        return QColor(20, 80, 220);
    case PenColor::Red:         return QColor(210, 30, 30);
    case PenColor::Cyan:        return QColor(0, 170, 200);
    case PenColor::Magenta:     return QColor(190, 40, 160);
    case PenColor::Highlight:   return QColor(255, 235, 0);
    case PenColor::Black:
    default:                    return QColor(0, 0, 0);
    }
}

QColor colorForStroke(const Stroke &s)
{
    if (s.hasRgba) {
        const uint32_t v = s.rgba;   // ARGB
        return QColor(int((v >> 16) & 0xFF), int((v >> 8) & 0xFF),
                      int(v & 0xFF), int((v >> 24) & 0xFF));
    }
    return colorForId(s.color);
}

// Content size in output pixels, BEFORE any rotation (bbox + margin) * scale.
QSize contentSize(const Page &page, double scale, double margin)
{
    const double spanX = double(page.maxX - page.minX) + 2 * margin;
    const double spanY = double(page.maxY - page.minY) + 2 * margin;
    return QSize(std::max(1, int(std::ceil(spanX * scale))),
                 std::max(1, int(std::ceil(spanY * scale))));
}

// Rotate the painter so content of size `content` lands upright inside an output
// surface of size `out` (out has w/h swapped for 90/270). No-op for rotation 0.
void applyRotation(QPainter &p, int rotation, QSize content, QSize out)
{
    const int r = ((rotation % 360) + 360) % 360;
    if (r == 0)
        return;
    p.translate(out.width() / 2.0, out.height() / 2.0);
    p.rotate(r);
    p.translate(-content.width() / 2.0, -content.height() / 2.0);
}

// Draw a page's strokes into `p`, mapping rm coordinates to content pixels. The
// caller owns the surface, white fill, render hints, and any rotation transform.
void drawStrokes(QPainter &p, const Page &page, const RenderOptions &opt, double scale)
{
    const double m = opt.margin;
    const double originX = double(page.minX) - m;
    const double originY = double(page.minY) - m;

    auto map = [&](const Point &pt) {
        return QPointF((double(pt.x) - originX) * scale, (double(pt.y) - originY) * scale);
    };

    const bool uniform = opt.uniformWidth > 0.0;
    const double uniformPx = std::max(opt.minPenPx, opt.uniformWidth * scale * opt.penScale);

    for (const Layer &layer : page.layers) {
        if (!layer.visible)
            continue;
        for (const Stroke &s : layer.strokes) {
            if (s.points.empty())
                continue;
            QPen pen(colorForStroke(s));
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);

            auto widthPx = [&](const Point &pt) {
                if (uniform)
                    return uniformPx;
                const double base = double(pt.width) * s.thicknessScale * scale * opt.penScale;
                return std::max(opt.minPenPx, base);
            };

            if (s.points.size() == 1) {
                pen.setWidthF(widthPx(s.points[0]));
                p.setPen(pen);
                p.drawPoint(map(s.points[0]));
                continue;
            }
            // Uniform width: one pen for the whole polyline. Per-point width
            // (uniformWidth <= 0) instead tracks pressure/speed per segment.
            if (uniform) {
                pen.setWidthF(uniformPx);
                QPainterPath path(map(s.points[0]));
                for (size_t i = 1; i < s.points.size(); ++i)
                    path.lineTo(map(s.points[i]));
                p.strokePath(path, pen);
                continue;
            }
            for (size_t i = 1; i < s.points.size(); ++i) {
                pen.setWidthF(widthPx(s.points[i]));
                p.setPen(pen);
                p.drawLine(map(s.points[i - 1]), map(s.points[i]));
            }
        }
    }
}

// Output surface size after rotation (w/h swapped for 90/270).
QSize rotatedSize(QSize content, int rotation)
{
    const int r = ((rotation % 360) + 360) % 360;
    return (r == 90 || r == 270) ? QSize(content.height(), content.width()) : content;
}

}  // namespace

QImage RmRenderer::renderToImage(const Page &page, const RenderOptions &opt)
{
    if (!page.hasContent)
        return QImage(8, 8, QImage::Format_RGB32);

    const double scale = opt.scale > 0 ? opt.scale : 1.0;
    const QSize content = contentSize(page, scale, opt.margin);
    const QSize out = rotatedSize(content, opt.rotation);

    QImage img(out, QImage::Format_RGB32);
    img.fill(Qt::white);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    applyRotation(p, opt.rotation, content, out);
    drawStrokes(p, page, opt, scale);
    p.end();
    return img;
}

bool RmRenderer::renderToPng(const Page &page, const QString &path, const RenderOptions &opt)
{
    const QImage img = renderToImage(page, opt);
    if (img.isNull())
        return false;
    return img.save(path, "PNG");
}

bool RmRenderer::renderToPdf(const std::vector<Page> &pages, const QString &path,
                             const RenderOptions &opt, int *pageCountOut)
{
    const double scale = opt.scale > 0 ? opt.scale : 1.0;

    // Collect the pages that actually have strokes; an all-blank input yields no PDF.
    std::vector<const Page *> drawable;
    for (const Page &pg : pages)
        if (pg.hasContent)
            drawable.push_back(&pg);
    if (pageCountOut)
        *pageCountOut = int(drawable.size());
    if (drawable.empty())
        return false;

    QPdfWriter writer(path);
    writer.setResolution(int(kPdfDpi));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setTitle(QStringLiteral("reMarkable note"));

    QPainter p;
    for (size_t i = 0; i < drawable.size(); ++i) {
        const Page &pg = *drawable[i];
        const QSize content = contentSize(pg, scale, opt.margin);
        const QSize out = rotatedSize(content, opt.rotation);
        // Physical page size so the PDF matches the note's real dimensions.
        const QPageSize ps(QSizeF(out.width() / kPdfDpi, out.height() / kPdfDpi),
                           QPageSize::Inch, QString(), QPageSize::ExactMatch);

        // Page size must be set before newPage() (and before begin() for page 1).
        writer.setPageSize(ps);
        if (i == 0) {
            if (!p.begin(&writer))
                return false;
        } else {
            writer.newPage();
        }

        p.setRenderHint(QPainter::Antialiasing, true);
        // The painter device is in writer pixels; map our content (in px at
        // `scale`) onto it, accounting for any rounding in the page size.
        const double sx = double(writer.width()) / std::max(1, out.width());
        const double sy = double(writer.height()) / std::max(1, out.height());
        p.save();
        p.scale(sx, sy);
        applyRotation(p, opt.rotation, content, out);
        drawStrokes(p, pg, opt, scale);
        p.restore();
    }
    p.end();
    return true;
}

}  // namespace rm
