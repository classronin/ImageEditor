#include "nodes/OverlayRecord.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPath.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "nodes/Style.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace ie {
namespace {

SkColor sk(Color32 c)
{
    return SkColorSetARGB((c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255);
}

SkPaint strokePaint(Color32 col, float width, bool aa)
{
    SkPaint p;
    p.setAntiAlias(aa);
    p.setColor(sk(col));
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(width);
    return p;
}

SkPaint fillPaint(Color32 col, bool aa)
{
    SkPaint p;
    p.setAntiAlias(aa);
    p.setColor(sk(col));
    return p;
}

Rect inkBounds(float l, float t, float r, float b, float inflate)
{
    if (!std::isfinite(l) || !std::isfinite(t) || !std::isfinite(r) || !std::isfinite(b)) return Rect{};
    const int x0 = (int)std::floor(std::min(l, r) - inflate);
    const int y0 = (int)std::floor(std::min(t, b) - inflate);
    const int x1 = (int)std::ceil(std::max(l, r) + inflate);
    const int y1 = (int)std::ceil(std::max(t, b) + inflate);
    return Rect::fromLTRB(x0, y0, x1, y1);
}

void pathInk(const Vec2f* pts, int count, float& l, float& t, float& r, float& b)
{
    l = r = pts[0].x;
    t = b = pts[0].y;
    for (int i = 1; i < count; ++i) {
        l = std::min(l, pts[i].x);
        r = std::max(r, pts[i].x);
        t = std::min(t, pts[i].y);
        b = std::max(b, pts[i].y);
    }
}

}

void OverlayRecorder::line(float x0, float y0, float x1, float y1, Color32 col, float width, bool aa)
{
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) return;
    OverlayOp op;
    op.kind = OvKind::Line;
    op.antiAlias = aa ? 1 : 0;
    op.color = col;
    op.x0 = x0; op.y0 = y0; op.x1 = x1; op.y1 = y1;
    op.width = width;
    op.bounds = inkBounds(x0, y0, x1, y1, width * 0.5f + 1.0f);
    r_->ops.push_back(op);
}

void OverlayRecorder::rect(float x0, float y0, float x1, float y1, Color32 col, float width, float rounding)
{
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) return;
    OverlayOp op;
    op.kind = OvKind::RectStroke;
    op.color = col;
    op.x0 = x0; op.y0 = y0; op.x1 = x1; op.y1 = y1;
    op.width = width;
    op.rounding = rounding;
    op.bounds = inkBounds(x0, y0, x1, y1, width * 0.5f + 1.0f);
    r_->ops.push_back(op);
}

void OverlayRecorder::rectFilled(float x0, float y0, float x1, float y1, Color32 col, float rounding)
{
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) return;
    OverlayOp op;
    op.kind = OvKind::RectFill;
    op.color = col;
    op.x0 = x0; op.y0 = y0; op.x1 = x1; op.y1 = y1;
    op.rounding = rounding;
    op.bounds = inkBounds(x0, y0, x1, y1, 1.0f);
    r_->ops.push_back(op);
}

void OverlayRecorder::circle(float cx, float cy, float radius, Color32 col, float width)
{
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(radius)) return;
    OverlayOp op;
    op.kind = OvKind::Circle;
    op.color = col;
    op.x0 = cx; op.y0 = cy; op.x1 = radius;
    op.width = width;
    op.bounds = inkBounds(cx - radius, cy - radius, cx + radius, cy + radius, width * 0.5f + 1.0f);
    r_->ops.push_back(op);
}

void OverlayRecorder::polyline(const Vec2f* pts, int count, Color32 col, float width, bool closed)
{
    if (!pts || count < 2) return;
    OverlayOp op;
    op.kind = OvKind::PathStroke;
    op.closed = closed ? 1 : 0;
    op.color = col;
    op.width = width;
    op.ptBegin = (uint32_t)r_->pts.size();
    r_->pts.insert(r_->pts.end(), pts, pts + count);
    op.ptEnd = (uint32_t)r_->pts.size();
    float l = 0, t = 0, r = 0, b = 0;
    pathInk(pts, count, l, t, r, b);
    op.bounds = inkBounds(l, t, r, b, width * 0.5f + 1.0f);
    r_->ops.push_back(op);
}

void OverlayRecorder::polygonFilled(const Vec2f* pts, int count, Color32 col)
{
    if (!pts || count < 3) return;
    OverlayOp op;
    op.kind = OvKind::PathFill;
    op.closed = 1;
    op.color = col;
    op.ptBegin = (uint32_t)r_->pts.size();
    r_->pts.insert(r_->pts.end(), pts, pts + count);
    op.ptEnd = (uint32_t)r_->pts.size();
    float l = 0, t = 0, r = 0, b = 0;
    pathInk(pts, count, l, t, r, b);
    op.bounds = inkBounds(l, t, r, b, 1.0f);
    r_->ops.push_back(op);
}

void OverlayRecorder::triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color32 col,
                               float width)
{
    const Vec2f pts[3] = {{x0, y0}, {x1, y1}, {x2, y2}};
    polyline(pts, 3, col, width, true);
}

void OverlayRecorder::triangleFilled(float x0, float y0, float x1, float y1, float x2, float y2, Color32 col)
{
    const Vec2f pts[3] = {{x0, y0}, {x1, y1}, {x2, y2}};
    polygonFilled(pts, 3, col);
}

void OverlayRecorder::text(float x, float y, Color32 col, const char* str)
{
    if (!str || !*str || !std::isfinite(x) || !std::isfinite(y)) return;
    const size_t len = std::strlen(str);
    OverlayOp op;
    op.kind = OvKind::Text;
    op.color = col;
    op.x0 = x; op.y0 = y;
    op.textBegin = (uint32_t)r_->text.size();
    r_->text.insert(r_->text.end(), str, str + len + 1);
    op.textEnd = (uint32_t)r_->text.size();
    op.bounds = inkBounds(x, y, x + ui::textWidth(std::string(str, len)), y + ui::lineHeight(), 1.0f);
    r_->ops.push_back(op);
}

float OverlayRecorder::textWidth(const char* str)
{
    if (!str || !*str) return 0.0f;
    return ui::textWidth(std::string(str, std::strlen(str)));
}

float OverlayRecorder::textHeight()
{
    return ui::lineHeight();
}

bool overlayOpSame(const OverlayRecording& a, size_t i, const OverlayRecording& b, size_t j)
{
    const OverlayOp& x = a.ops[i];
    const OverlayOp& y = b.ops[j];
    if (x.kind != y.kind || x.closed != y.closed || x.antiAlias != y.antiAlias || x.color != y.color)
        return false;
    if (x.x0 != y.x0 || x.y0 != y.y0 || x.x1 != y.x1 || x.y1 != y.y1) return false;
    if (x.width != y.width || x.rounding != y.rounding || x.bounds != y.bounds) return false;
    if (x.ptEnd - x.ptBegin != y.ptEnd - y.ptBegin || x.textEnd - x.textBegin != y.textEnd - y.textBegin)
        return false;
    for (uint32_t k = 0; k < x.ptEnd - x.ptBegin; ++k) {
        const Vec2f& p = a.pts[x.ptBegin + k];
        const Vec2f& q = b.pts[y.ptBegin + k];
        if (p.x != q.x || p.y != q.y) return false;
    }
    const size_t n = x.textEnd - x.textBegin;
    if (n && std::memcmp(a.text.data() + x.textBegin, b.text.data() + y.textBegin, n) != 0) return false;
    return true;
}

void replayOverlay(SkCanvas& canvas, const OverlayRecording& rec)
{
    for (const OverlayOp& op : rec.ops) {
        const int count = (int)(op.ptEnd - op.ptBegin);
        const Vec2f* pts = count > 0 ? rec.pts.data() + op.ptBegin : nullptr;
        switch (op.kind) {
            case OvKind::Line:
                canvas.drawLine(op.x0, op.y0, op.x1, op.y1, strokePaint(op.color, op.width, op.antiAlias != 0));
                break;
            case OvKind::RectStroke: {
                const SkRect r = SkRect::MakeLTRB(std::min(op.x0, op.x1), std::min(op.y0, op.y1),
                                                  std::max(op.x0, op.x1), std::max(op.y0, op.y1));
                const SkPaint p = strokePaint(op.color, op.width, true);
                if (op.rounding > 0.0f)
                    canvas.drawRRect(SkRRect::MakeRectXY(r, op.rounding, op.rounding), p);
                else
                    canvas.drawRect(r, p);
                break;
            }
            case OvKind::RectFill: {
                const SkRect r = SkRect::MakeLTRB(std::min(op.x0, op.x1), std::min(op.y0, op.y1),
                                                  std::max(op.x0, op.x1), std::max(op.y0, op.y1));
                const SkPaint p = fillPaint(op.color, true);
                if (op.rounding > 0.0f)
                    canvas.drawRRect(SkRRect::MakeRectXY(r, op.rounding, op.rounding), p);
                else
                    canvas.drawRect(r, p);
                break;
            }
            case OvKind::Circle:
                canvas.drawCircle(op.x0, op.y0, op.x1, strokePaint(op.color, op.width, true));
                break;
            case OvKind::PathStroke:
            case OvKind::PathFill: {
                if (!pts || count < 2) break;
                SkPath path;
                path.moveTo(pts[0].x, pts[0].y);
                for (int i = 1; i < count; ++i) path.lineTo(pts[i].x, pts[i].y);
                if (op.kind == OvKind::PathFill || op.closed) path.close();
                const bool fill = op.kind == OvKind::PathFill;
                const SkPaint p = fill ? fillPaint(op.color, true) : strokePaint(op.color, op.width, true);
                if (fill && count < 3) break;
                canvas.drawPath(path, p);
                break;
            }
            case OvKind::Text: {
                if (op.textEnd <= op.textBegin + 1) break;
                const std::string s(rec.text.data() + op.textBegin, (size_t)(op.textEnd - op.textBegin - 1));
                ui::drawText(canvas, op.x0, op.y0, op.color, s);
                break;
            }
        }
    }
}

}
