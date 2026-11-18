#include "core/Blend.h"
#include "tools/Tools.h"

#include <algorithm>
#include <cmath>

namespace ie {

namespace {

const char* const kGradientTypeItems[] = {"线性", "径向"};

struct Rgba8 {
    uint8_t r, g, b, a;
};

inline Rgba8 fromColor(Color c) { return Rgba8{c.r, c.g, c.b, c.a}; }

inline uint32_t pack(const Rgba8& c)
{
    return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}

inline void fixZeroAlpha(Rgba8& c0, Rgba8& c1)
{
    if (c0.a == 0 && c1.a != 0) {
        c0.r = c1.r;
        c0.g = c1.g;
        c0.b = c1.b;
    } else if (c0.a != 0 && c1.a == 0) {
        c1.r = c0.r;
        c1.g = c0.g;
        c1.b = c0.b;
    }
}

inline Rgba8 lerp(const Rgba8& c0, const Rgba8& c1, double f)
{
    auto ch = [f](uint8_t a, uint8_t b) { return (uint8_t)std::clamp((int)(a + f * ((int)b - (int)a) + 1e-7), 0, 255); };
    return Rgba8{ch(c0.r, c1.r), ch(c0.g, c1.g), ch(c0.b, c1.b), ch(c0.a, c1.a)};
}

class GradientTool final : public Tool {
public:
    ToolId id() const override { return ToolId::Gradient; }
    const char* name() const override { return "渐变"; }

    CursorShape cursor(const InputFrame&) const override { return CursorShape::Cross; }
    bool busy() const override { return dragging_; }

    void onEnter(Editor&) override { dragging_ = false; }
    void onLeave(Editor&) override { dragging_ = false; }

    void onPress(Editor& e, const InputFrame& in, int btn) override
    {
        if (btn != 0 || !e.canvasHas(in.mouse.x, in.mouse.y)) return;
        if (!e.editableLayer()) return;
        dragging_ = true;
        start_ = e.canvasImage(in.mouse.x, in.mouse.y);
        cur_ = start_;
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
        commit(e);
    }

    void props(Props& p, Editor& e) override
    {
        p.header("渐变");
        p.combo("类型", &type_, kGradientTypeItems, 2, &GradientTool::setTypeV, this);
        p.separator();
        p.color("起点色", c0_, &GradientTool::setC0, this);
        p.color("终点色", c1_, &GradientTool::setC1, this);
        (void)e;
    }

    void overlay(Overlay* dl, const Editor& e, const CanvasViewState& v, const InputFrame& in) override
    {
        if (!dragging_) return;
        const Vec2f a = v.toScreen(start_.x, start_.y, e.doc.width, e.doc.height);
        const Vec2f b = v.toScreen(cur_.x, cur_.y, e.doc.width, e.doc.height);
        const Color32 col = IE_COL32(255, 255, 255, 230);
        const Color32 shadow = IE_COL32(0, 0, 0, 120);
        dl->line(a.x, a.y, b.x, b.y, shadow, 3.0f);
        dl->line(a.x, a.y, b.x, b.y, col, 1.5f);
        if (type_ == 1) {
            const float r = std::hypot(b.x - a.x, b.y - a.y);
            dl->circle(a.x, a.y, r + 0.5f, shadow, 3.0f);
            dl->circle(a.x, a.y, r, col, 1.5f);
        }
        dl->circle(a.x, a.y, 3.5f, shadow, 4.0f);
        dl->circle(a.x, a.y, 3.0f, col, 1.5f);
        dl->circle(b.x, b.y, 3.5f, shadow, 4.0f);
        dl->circle(b.x, b.y, 3.0f, col, 1.5f);
        (void)in;
    }

private:
    static void setTypeV(double v, void* ud) { static_cast<GradientTool*>(ud)->type_ = (int)v; }
    static void setC0(const float* c, void* ud)
    {
        float* dst = static_cast<GradientTool*>(ud)->c0_;
        for (int k = 0; k < 4; ++k) dst[k] = c[k];
    }
    static void setC1(const float* c, void* ud)
    {
        float* dst = static_cast<GradientTool*>(ud)->c1_;
        for (int k = 0; k < 4; ++k) dst[k] = c[k];
    }
    static Color toColor(const float* c)
    {
        auto q = [](float v) { return (uint8_t)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return Color(q(c[0]), q(c[1]), q(c[2]), q(c[3]));
    }

    void commit(Editor& e)
    {
        if (start_.x == cur_.x && start_.y == cur_.y) return;

        Layer* l = e.doc.activeLayer();
        if (!l) return;

        const Rect canvasRect{0, 0, e.doc.width, e.doc.height};
        const Rect target =
            (e.sel.active && !e.sel.bounds().empty()) ? e.sel.bounds().intersect(canvasRect) : canvasRect;
        if (target.empty()) return;

        e.hist.record(e.doc, e.sel, name());
        e.doc.ensureLayerRect(e.doc.active, target);
        l = e.doc.activeLayer();
        if (!l || l->img.empty()) return;

        Rgba8 c0 = fromColor(toColor(c0_));
        Rgba8 c1 = fromColor(toColor(c1_));
        fixZeroAlpha(c0, c1);

        const double ux = start_.x, uy = start_.y;
        const double vx = cur_.x, vy = cur_.y;

        const double wmag = std::sqrt((vx - ux) * (vx - ux) + (vy - uy) * (vy - uy));
        const bool radial = type_ == 1;
        const double radius = wmag;

        const uint8_t* mask = e.sel.maskPtr();
        const int mw = e.doc.width, mh = e.doc.height;
        const bool lockAlpha = l->lockAlpha;

        uint32_t* p = l->img.pixels();
        const int iw = l->img.width(), ih = l->img.height();
        const int cx0 = std::max(l->x, target.x), cx1 = std::min(l->x + iw, target.right());
        const int cy0 = std::max(l->y, target.y), cy1 = std::min(l->y + ih, target.bottom());
        for (int cy = cy0; cy < cy1; ++cy) {
            const int y = cy - l->y;
            for (int cx = cx0; cx < cx1; ++cx) {
                if (mask) {
                    if (cx < 0 || cy < 0 || cx >= mw || cy >= mh) continue;
                    if (!mask[(size_t)cy * mw + cx]) continue;
                }

                const uint32_t grad =
                    pack(gradientAt(c0, c1, radial, ux, uy, vx, vy, wmag, radius, (double)cx, (double)cy));
                const size_t i = (size_t)y * (size_t)iw + (size_t)(cx - l->x);
                p[i] = blend::paintPixel(p[i], grad, 1.0f, lockAlpha);
            }
        }

        e.doc.markDirty(target, e.doc.active);
        e.afterEdit(name());
    }

    static Rgba8 gradientAt(const Rgba8& c0, const Rgba8& c1, bool radial, double ux, double uy, double vx,
                            double vy, double wmag, double radius, double qx, double qy)
    {
        double f;
        if (radial) {
            const double dx = qx - ux;
            const double dy = qy - uy;
            f = std::sqrt(dx * dx + dy * dy) / radius;
        } else {
            const double dx = qx - ux, dy = qy - uy;
            f = (dx * (vx - ux) + dy * (vy - uy)) / (wmag * wmag);
        }

        if (f < 0.0) return c0;
        if (f > 1.0) return c1;
        return lerp(c0, c1, f);
    }

    int type_ = 0;
    bool dragging_ = false;
    Vec2f start_{}, cur_{};
    float c0_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float c1_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

}

std::unique_ptr<Tool> makeGradientTool() { return std::make_unique<GradientTool>(); }

}
