#include "rm/RmRenderer.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace rm {

namespace {

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

}  // namespace

QImage RmRenderer::renderToImage(const Page &page, const RenderOptions &opt)
{
    if (!page.hasContent)
        return QImage(8, 8, QImage::Format_RGB32);

    const double scale = opt.scale > 0 ? opt.scale : 1.0;
    const double m = opt.margin;
    const double originX = double(page.minX) - m;
    const double originY = double(page.minY) - m;
    const double spanX = double(page.maxX - page.minX) + 2 * m;
    const double spanY = double(page.maxY - page.minY) + 2 * m;

    const int w = std::max(1, int(std::ceil(spanX * scale)));
    const int h = std::max(1, int(std::ceil(spanY * scale)));

    QImage img(w, h, QImage::Format_RGB32);
    img.fill(Qt::white);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    auto map = [&](const Point &pt) {
        return QPointF((double(pt.x) - originX) * scale, (double(pt.y) - originY) * scale);
    };

    for (const Layer &layer : page.layers) {
        if (!layer.visible)
            continue;
        for (const Stroke &s : layer.strokes) {
            if (s.points.empty())
                continue;
            QPen pen(colorForStroke(s));
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);

            const bool uniform = opt.uniformWidth > 0.0;
            const double uniformPx =
                std::max(opt.minPenPx, opt.uniformWidth * scale * opt.penScale);
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
                p.setPen(pen);
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
    p.end();

    if (opt.rotation % 360 != 0)
        img = img.transformed(QTransform().rotate(opt.rotation), Qt::SmoothTransformation);

    return img;
}

bool RmRenderer::renderToPng(const Page &page, const QString &path, const RenderOptions &opt)
{
    const QImage img = renderToImage(page, opt);
    if (img.isNull())
        return false;
    return img.save(path, "PNG");
}

}  // namespace rm
