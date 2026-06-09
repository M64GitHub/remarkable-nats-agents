#include "rm/RmRenderer.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QMarginsF>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPointF>
#include <QSize>
#include <QSizeF>
#include <QStringList>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rm {

namespace {

// reMarkable panel density — gives the PDF a sensible physical page size
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

// Font for typed text — a sans-serif at the configured size (rm units; 1 unit == 1
// px at scale 1). Used for both wrapping (metrics) and drawing.
QFont typedFont(double px)
{
    QFont f(QStringLiteral("Noto Sans"));
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(std::max(1, int(std::lround(px))));
    return f;
}

// Word-wrap the typed text to the box width (rm units). Paragraphs split on '\n';
// an empty paragraph keeps a blank line so vertical spacing matches the original.
std::vector<QString> wrapTypedText(const Page &page, const RenderOptions &opt)
{
    std::vector<QString> lines;
    if (!page.hasText || !opt.drawText)
        return lines;
    const QFontMetricsF fm(typedFont(opt.textFontPx));
    const double width = page.textWidth > 0 ? page.textWidth : 1e9;

    const QStringList paragraphs = page.text.split(QLatin1Char('\n'));
    for (const QString &para : paragraphs) {
        if (para.isEmpty()) {
            lines.push_back(QString());
            continue;
        }
        QString cur;
        const QStringList words = para.split(QLatin1Char(' '));
        for (const QString &w : words) {
            const QString candidate = cur.isEmpty() ? w : cur + QLatin1Char(' ') + w;
            if (!cur.isEmpty() && fm.horizontalAdvance(candidate) > width) {
                lines.push_back(cur);
                cur = w;
            } else {
                cur = candidate;
            }
        }
        lines.push_back(cur);
    }
    return lines;
}

double maxLineAdvance(const std::vector<QString> &lines, const RenderOptions &opt)
{
    const QFontMetricsF fm(typedFont(opt.textFontPx));
    double m = 0;
    for (const QString &l : lines)
        m = std::max(m, fm.horizontalAdvance(l));
    return m;
}

// A laid-out page ready to paint. We render typed text and strokes in a STACKED
// layout — the text block on top, the handwriting below it, sharing the horizontal
// axis (x kept in page coordinates). reMarkable's typed-text vertical coordinates
// don't map cleanly into stroke space, so stacking gives a clean, non-overlapping
// result that matches how mixed pages actually read (typed note + annotations).
struct PagePlan {
    bool valid = false;
    std::vector<QString> lines;   // wrapped typed-text lines
    double originX = 0.0;         // rm x mapped to output x = 0
    double strokeOriginY = 0.0;   // rm y mapped to output y = 0 for strokes
    double textTopRm = 0.0;       // output-local rm y of the text block's top
    double ascentRm = 0.0;        // font ascent in rm units
    QSize content;                // unrotated output size in px
};

PagePlan planPage(const Page &page, const RenderOptions &opt, double scale)
{
    PagePlan plan;
    plan.lines = wrapTypedText(page, opt);
    const bool hasText = !plan.lines.empty();
    const bool hasStroke = page.hasContent;
    if (!hasText && !hasStroke)
        return plan;

    const double margin = opt.margin;
    const QFontMetricsF fm(typedFont(opt.textFontPx));
    plan.ascentRm = fm.ascent();

    double xMin = std::numeric_limits<double>::max();
    double xMax = std::numeric_limits<double>::lowest();
    if (hasStroke) { xMin = std::min(xMin, double(page.minX)); xMax = std::max(xMax, double(page.maxX)); }
    if (hasText) {
        const double tw = page.textWidth > 0 ? page.textWidth : maxLineAdvance(plan.lines, opt);
        xMin = std::min(xMin, page.textX);
        xMax = std::max(xMax, page.textX + tw);
    }

    const double textH = hasText ? double(plan.lines.size()) * opt.textLineHeight : 0.0;
    const double strokeH = hasStroke ? double(page.maxY - page.minY) : 0.0;
    const double gap = (hasText && hasStroke) ? opt.textLineHeight : 0.0;

    plan.originX = xMin - margin;
    plan.textTopRm = margin;
    const double strokeTopRm = margin + textH + gap;
    plan.strokeOriginY = double(page.minY) - strokeTopRm;

    const double contentW = (xMax - xMin) + 2 * margin;
    const double contentH = margin + textH + gap + strokeH + margin;
    plan.content = QSize(std::max(1, int(std::ceil(contentW * scale))),
                         std::max(1, int(std::ceil(contentH * scale))));
    plan.valid = true;
    return plan;
}

// Draw a page's strokes, mapping rm coordinates to pixels with the given origin.
void drawStrokes(QPainter &p, const Page &page, const RenderOptions &opt,
                 double originX, double originY, double scale)
{
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

// Draw the wrapped typed text, line by line, in the stacked layout. All positions
// are output-local rm units (× scale at draw time); see PagePlan.
void drawTypedText(QPainter &p, const Page &page, const PagePlan &plan,
                   const RenderOptions &opt, double scale)
{
    if (plan.lines.empty())
        return;
    p.setFont(typedFont(opt.textFontPx * scale));
    p.setPen(QColor(0, 0, 0));

    const double x = (page.textX - plan.originX) * scale;
    for (size_t i = 0; i < plan.lines.size(); ++i) {
        if (plan.lines[i].isEmpty())
            continue;
        const double baselineRm = plan.textTopRm + double(i) * opt.textLineHeight + plan.ascentRm;
        p.drawText(QPointF(x, baselineRm * scale), plan.lines[i]);
    }
}

QSize rotatedSize(QSize content, int rotation)
{
    const int r = ((rotation % 360) + 360) % 360;
    return (r == 90 || r == 270) ? QSize(content.height(), content.width()) : content;
}

void applyRotation(QPainter &p, int rotation, QSize content, QSize out)
{
    const int r = ((rotation % 360) + 360) % 360;
    if (r == 0)
        return;
    p.translate(out.width() / 2.0, out.height() / 2.0);
    p.rotate(r);
    p.translate(-content.width() / 2.0, -content.height() / 2.0);
}

}  // namespace

QImage RmRenderer::renderToImage(const Page &page, const RenderOptions &opt)
{
    const double scale = opt.scale > 0 ? opt.scale : 1.0;
    const PagePlan plan = planPage(page, opt, scale);
    if (!plan.valid)
        return QImage(8, 8, QImage::Format_RGB32);

    const QSize out = rotatedSize(plan.content, opt.rotation);
    QImage img(out, QImage::Format_RGB32);
    img.fill(Qt::white);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    applyRotation(p, opt.rotation, plan.content, out);
    drawTypedText(p, page, plan, opt, scale);
    drawStrokes(p, page, opt, plan.originX, plan.strokeOriginY, scale);
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

    // Collect pages with any renderable content; an all-blank input yields no PDF.
    std::vector<const Page *> drawable;
    for (const Page &pg : pages)
        if (pg.hasContent || (pg.hasText && opt.drawText))
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
        const PagePlan plan = planPage(pg, opt, scale);
        if (!plan.valid)
            continue;

        const QSize out = rotatedSize(plan.content, opt.rotation);
        const QPageSize ps(QSizeF(out.width() / kPdfDpi, out.height() / kPdfDpi),
                           QPageSize::Inch, QString(), QPageSize::ExactMatch);

        writer.setPageSize(ps);
        if (i == 0) {
            if (!p.begin(&writer))
                return false;
        } else {
            writer.newPage();
        }

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        const double sx = double(writer.width()) / std::max(1, out.width());
        const double sy = double(writer.height()) / std::max(1, out.height());
        p.save();
        p.scale(sx, sy);
        applyRotation(p, opt.rotation, plan.content, out);
        drawTypedText(p, pg, plan, opt, scale);
        drawStrokes(p, pg, opt, plan.originX, plan.strokeOriginY, scale);
        p.restore();
    }
    p.end();
    return true;
}

}  // namespace rm
