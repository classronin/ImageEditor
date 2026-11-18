#include "tools/Paint.h"
#include "tools/Tools.h"

#include <algorithm>
#include <cmath>

namespace ie {

namespace {

const char* const kShapeItems[] = {"矩形", "椭圆", "三角形", "直线"};

class ShapeTool final : public Tool {
public:
    ToolId id() const override { return ToolId::Shape; }
    const char* name() const override { return "形状"; }

    CursorShape cursor(const InputFrame&) const override { return CursorShape::Arrow; }
    bool busy() const override { return dragging_; }

    void onEnter(Editor&) override { dragging_ = false; }
    void onLeave(Editor&) override { dragging_ = false; }

    void onPress(Editor& e, const InputFrame& in, int btn) override
    {
        if (btn != 0 || !e.canvasHas(in.mouse.x, in.mouse.y)) return;
        if (!e.editableLayer()) return;
        e.hist.record(e.doc, e.sel, "形状");
        dragging_ = true;
        start_ = e.canvasImage(in.mouse.x, in.mouse.y);
        cur_ = start_;
        e.afterEdit("形状");
    }

    void onDrag(Editor& e, const InputFrame& in, int btn) override
    {
        if (!dragging_ || btn != 0) return;
        cur_ = e.canvasImage(in.mouse.x, in.mouse.y);
    }

    void onRelease(Editor& e, const InputFrame& in, int btn) override
    {
        if (!dragging_ || btn != 0) return;
        dragging_ = false;
        cur_ = e.canvasImage(in.mouse.x, in.mouse.y);
        commit(e, in.shift);
    }

    void props(Props& p, Editor& e) override
    {
        p.header("形状");
        p.combo("类型", &shape_, kShapeItems, 4, &ShapeTool::setShapeV, this);
        p.check("填充", &fill_, &ShapeTool::setFillV, this);
        if (!fill_) p.sliderF("线宽", &strokeW_, 1.0f, 80.0f, &ShapeTool::setStrokeWV, this, "%.0f px");
    }

    void overlay(Overlay* dl, const Editor& e, const CanvasViewState& v, const InputFrame& in) override
    {
        if (!dragging_) return;
        const Rect r = normalized(start_, cur_, in.shift);
        const Vec2f a = v.toScreen((float)r.x, (float)r.y, e.doc.width, e.doc.height);
        const Vec2f b = v.toScreen((float)r.right(), (float)r.bottom(), e.doc.width, e.doc.height);

        const Color32 col = IE_COL32(255, 255, 255, 230);
        const Color32 shadow = IE_COL32(0, 0, 0, 120);
        const float th = std::max(1.0f, strokeW_ * v.zoom * (fill_ ? 1.0f : 1.0f));

        if (shape_ == 0) {
            if (fill_) dl->rectFilled(a.x, a.y, b.x, b.y, IE_COL32(255, 255, 255, 60), 0.0f);
            dl->rect(a.x, a.y, b.x, b.y, shadow, th + 2.0f, 0.0f);
            dl->rect(a.x, a.y, b.x, b.y, col, th, 0.0f);
        } else if (shape_ == 1) {
            const Vec2f c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            const float rx = (b.x - a.x) * 0.5f;
            const float ry = (b.y - a.y) * 0.5f;
            std::vector<Vec2f> ell;
            const int n = 64;
            ell.reserve((size_t)n);
            for (int i = 0; i < n; ++i) {
                const float t = (float)i / (float)n * 6.2831853f;
                ell.push_back(Vec2f(c.x + std::cos(t) * rx, c.y + std::sin(t) * ry));
            }
            if (fill_) dl->polygonFilled(ell.data(), (int)ell.size(), IE_COL32(255, 255, 255, 60));
            dl->polyline(ell.data(), (int)ell.size(), shadow, th + 2.0f, true);
            dl->polyline(ell.data(), (int)ell.size(), col, th, true);
        } else if (shape_ == 2) {
            const float cx = (a.x + b.x) * 0.5f;
            const Vec2f t0(cx, a.y), t1(a.x, b.y), t2(b.x, b.y);
            if (fill_) dl->triangleFilled(t0.x, t0.y, t1.x, t1.y, t2.x, t2.y, IE_COL32(255, 255, 255, 60));
            dl->triangle(t0.x, t0.y, t1.x, t1.y, t2.x, t2.y, shadow, th + 2.0f);
            dl->triangle(t0.x, t0.y, t1.x, t1.y, t2.x, t2.y, col, th);
        } else {
            Vec2f a2 = start_;
            Vec2f b2 = cur_;
            if (in.shift) constrainLine(a2, b2);
            const Vec2f s0 = v.toScreen(a2.x, a2.y, e.doc.width, e.doc.height);
            const Vec2f s1 = v.toScreen(b2.x, b2.y, e.doc.width, e.doc.height);
            dl->line(s0.x, s0.y, s1.x, s1.y, shadow, th + 2.0f);
            dl->line(s0.x, s0.y, s1.x, s1.y, col, th);
        }
    }

    float strokeW() const { return strokeW_; }

private:
    void edge(Image& img, int ox, int oy, const uint8_t* mask, int mw, int mh, Rect& dirty, float x0, float y0, float x1,
              float y1, Color c, bool lockAlpha) const
    {
        if (strokeW_ < 1.5f) {
            hairline(img, ox, oy, mask, mw, mh, dirty, x0, y0, x1, y1, c.packed(), lockAlpha);
            return;
        }
        Dab d;
        d.radius = std::max(1.0f, strokeW_ * 0.5f);
        d.hardness = 1.0f;
        d.opacity = 1.0f;
        strokeDab(img, ox, oy, mask, mw, mh, dirty, x0, y0, x1, y1, std::max(0.6f, d.radius * 0.3f), d, c, false,
                  lockAlpha);
    }

    static void setShapeV(double v, void* ud) { static_cast<ShapeTool*>(ud)->shape_ = (int)v; }
    static void setFillV(double v, void* ud) { static_cast<ShapeTool*>(ud)->fill_ = v != 0.0; }
    static void setStrokeWV(double v, void* ud) { static_cast<ShapeTool*>(ud)->strokeW_ = (float)v; }
    static void setFgColor(const float* c, void* ud)
    {
        static_cast<Editor*>(ud)->palette.setFg(c);
    }

    int shape_ = 0;
    bool fill_ = false;
    float strokeW_ = 4.0f;
    bool dragging_ = false;
    Vec2f start_{}, cur_{};

    static void constrainLine(Vec2f& a, Vec2f& b)
    {
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float ang = std::atan2(dy, dx);
        const float step = 3.14159265f / 4.0f;
        const float snapped = std::round(ang / step) * step;
        const float len = std::sqrt(dx * dx + dy * dy);
        b.x = a.x + std::cos(snapped) * len;
        b.y = a.y + std::sin(snapped) * len;
    }

    static Rect normalized(Vec2f a, Vec2f b, bool square)
    {
        float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
        float x1 = std::max(a.x, b.x), y1 = std::max(a.y, b.y);
        if (square) {
            const float s = std::max(x1 - x0, y1 - y0);
            if (b.x < a.x) x0 = x1 - s;
            else x1 = x0 + s;
            if (b.y < a.y) y0 = y1 - s;
            else y1 = y0 + s;
        }
        Rect r;
        r.x = (int)std::floor(x0);
        r.y = (int)std::floor(y0);
        r.w = std::max(0, (int)std::ceil(x1) - r.x);
        r.h = std::max(0, (int)std::ceil(y1) - r.y);
        return r;
    }

    void commit(Editor& e, bool square)
    {
        Layer* l = e.doc.activeLayer();
        if (!l) return;
        const Rect r = normalized(start_, cur_, square);
        const uint8_t* mask = e.sel.maskPtr();
        const int mw = e.doc.width, mh = e.doc.height;
        const Color c = e.palette.fgColor();

        if (shape_ == 3) {
            Rect dirty;
            Vec2f a{start_.x, start_.y};
            Vec2f b{cur_.x, cur_.y};
            if (square) {
                const float dx = b.x - a.x, dy = b.y - a.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                const float snapped = std::round(std::atan2(dy, dx) / (3.14159265f / 4.0f)) * (3.14159265f / 4.0f);
                b.x = a.x + std::cos(snapped) * len;
                b.y = a.y + std::sin(snapped) * len;
            }
            const Rect need = dabBounds(a.x, a.y, std::max(strokeW_ * 0.5f, 1.0f))
                                  .unite(dabBounds(b.x, b.y, std::max(strokeW_ * 0.5f, 1.0f)));
            e.doc.ensureLayerRect(e.doc.active, need);
            l = e.doc.activeLayer();
            edge(l->img, l->x, l->y, mask, mw, mh, dirty, a.x, a.y, b.x, b.y, c, l->lockAlpha);
            e.doc.markDirty(dirty, e.doc.active);
            return;
        }

        if (r.empty()) return;

        const bool lockAlpha = l->lockAlpha;
        if (fill_) {
            const uint32_t src = c.packed();
            e.doc.ensureLayerRect(e.doc.active, r);
            l = e.doc.activeLayer();
            uint32_t* p = l->img.pixels();
            const int iw = l->img.width(), ih = l->img.height();
            const int lx0 = r.x - l->x, ly0 = r.y - l->y;
            if (shape_ == 0) {
                for (int y = ly0; y < ly0 + r.h; ++y) {
                    if (y < 0 || y >= ih) continue;
                    for (int x = lx0; x < lx0 + r.w; ++x) {
                        if (x < 0 || x >= iw) continue;
                        const int cx = x + l->x, cy = y + l->y;
                        if (mask && !mask[(size_t)cy * mw + cx]) continue;
                        p[(size_t)y * iw + x] = blend::paintPixel(p[(size_t)y * iw + x], src, 1.0f, lockAlpha);
                    }
                }
            } else if (shape_ == 1) {
                const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
                const float rx = std::max(0.5f, r.w * 0.5f), ry = std::max(0.5f, r.h * 0.5f);
                for (int y = ly0; y < ly0 + r.h; ++y) {
                    if (y < 0 || y >= ih) continue;
                    for (int x = lx0; x < lx0 + r.w; ++x) {
                        if (x < 0 || x >= iw) continue;
                        const int cxi = x + l->x, cyi = y + l->y;
                        const float nx = ((float)cxi + 0.5f - cx) / rx;
                        const float ny = ((float)cyi + 0.5f - cy) / ry;
                        if (nx * nx + ny * ny > 1.0f) continue;
                        if (mask && !mask[(size_t)cyi * mw + cxi]) continue;
                        p[(size_t)y * iw + x] = blend::paintPixel(p[(size_t)y * iw + x], src, 1.0f, lockAlpha);
                    }
                }
            } else {
                const float tx = r.x + r.w * 0.5f;
                const float ax = tx, ay = (float)r.y;
                const float bx = (float)r.x, by = (float)r.bottom();
                const float cx3 = (float)r.right(), cy3 = (float)r.bottom();
                auto inTri = [&](float px, float py) {
                    const float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
                    const float d2 = (px - cx3) * (by - cy3) - (bx - cx3) * (py - cy3);
                    const float d3 = (px - ax) * (cy3 - ay) - (cx3 - ax) * (py - ay);
                    const bool hasNeg = d1 < 0 || d2 < 0 || d3 < 0;
                    const bool hasPos = d1 > 0 || d2 > 0 || d3 > 0;
                    return !(hasNeg && hasPos);
                };
                for (int y = ly0; y < ly0 + r.h; ++y) {
                    if (y < 0 || y >= ih) continue;
                    for (int x = lx0; x < lx0 + r.w; ++x) {
                        if (x < 0 || x >= iw) continue;
                        const int cxi = x + l->x, cyi = y + l->y;
                        if (!inTri((float)cxi + 0.5f, (float)cyi + 0.5f)) continue;
                        if (mask && !mask[(size_t)cyi * mw + cxi]) continue;
                        p[(size_t)y * iw + x] = blend::paintPixel(p[(size_t)y * iw + x], src, 1.0f, lockAlpha);
                    }
                }
            }
            e.doc.markDirty(r, e.doc.active);
            return;
        }

        Rect dirty;
        const float margin = std::max(1.0f, strokeW_ * 0.5f);
        e.doc.ensureLayerRect(e.doc.active, Rect::fromLTRB(r.x - (int)margin - 2, r.y - (int)margin - 2,
                                                           r.right() + (int)margin + 2, r.bottom() + (int)margin + 2));
        l = e.doc.activeLayer();

        if (shape_ == 0) {
            const float x0 = (float)r.x, y0 = (float)r.y, x1 = (float)r.right(), y1 = (float)r.bottom();
            edge(l->img, l->x, l->y, mask, mw, mh, dirty, x0, y0, x1, y0, c, l->lockAlpha);
            edge(l->img, l->x, l->y, mask, mw, mh, dirty, x1, y0, x1, y1, c, l->lockAlpha);
            edge(l->img, l->x, l->y, mask, mw, mh, dirty, x1, y1, x0, y1, c, l->lockAlpha);
            edge(l->img, l->x, l->y, mask, mw, mh, dirty, x0, y1, x0, y0, c, l->lockAlpha);
        } else if (shape_ == 1) {
            const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
            const float rx = std::max(0.5f, r.w * 0.5f), ry = std::max(0.5f, r.h * 0.5f);
            const float circ = 3.14159265f * (3.0f * (rx + ry) - std::sqrt((3.0f * rx + ry) * (rx + 3.0f * ry)));
            const float step = strokeW_ < 1.5f ? 0.7f : std::max(0.6f, strokeW_ * 0.5f * 0.3f);
            const int steps = std::clamp((int)(circ / step), 24, 8192);
            float px = cx + rx, py = cy;
            for (int i = 1; i <= steps; ++i) {
                const float t = (float)i / (float)steps * 6.2831853f;
                const float nx = cx + std::cos(t) * rx;
                const float ny = cy + std::sin(t) * ry;
                edge(l->img, l->x, l->y, mask, mw, mh, dirty, px, py, nx, ny, c, l->lockAlpha);
                px = nx;
                py = ny;
            }
        } else {
            const float x0 = (float)r.x, y1 = (float)r.bottom();
            const float x1 = (float)r.right();
            const float y0 = (float)r.y;
            const float cx = r.x + r.w * 0.5f;
            edge(l->img, l->x, l->y, mask, mw, mh, dirty, cx, y0, x0, y1, c, l->lockAlpha);
            edge(l->img, l->x, l->y, mask, mw, mh, dirty, x0, y1, x1, y1, c, l->lockAlpha);
            edge(l->img, l->x, l->y, mask, mw, mh, dirty, x1, y1, cx, y0, c, l->lockAlpha);
        }
        e.doc.markDirty(dirty, e.doc.active);
    }
};

}

std::unique_ptr<Tool> makeShapeTool() { return std::make_unique<ShapeTool>(); }

}
