#include "tools/Tools.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ie {

namespace {

inline Rect rectFromPoints(const Vec2f& a, const Vec2f& b, bool square = false)
{
    Vec2f bb = b;
    if (square) {
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float s = std::max(std::abs(dx), std::abs(dy));
        bb.x = a.x + (dx < 0 ? -s : s);
        bb.y = a.y + (dy < 0 ? -s : s);
    }
    Rect r;
    r.x = (int)std::floor(std::min(a.x, bb.x));
    r.y = (int)std::floor(std::min(a.y, bb.y));
    r.w = (int)std::ceil(std::max(a.x, bb.x)) - r.x;
    r.h = (int)std::ceil(std::max(a.y, bb.y)) - r.y;
    return r;
}

inline float distToSeg(const Vec2f& p, const Vec2f& a, const Vec2f& b)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

class CropTool final : public Tool {
public:
    ToolId id() const override { return ToolId::Crop; }
    const char* name() const override { return "裁剪"; }

    CursorShape cursor(const InputFrame& in) const override
    {
        if (hasRect_) {
            switch (grabAt(in.mouse, ed_)) {
            case 1: case 3: return CursorShape::ResizeNWSE;
            case 2: case 4: return CursorShape::ResizeNESW;
            case 5: case 7: return CursorShape::ResizeNS;
            case 6: case 8: return CursorShape::ResizeEW;
            case 9: return CursorShape::Move;
            }
        }
        return CursorShape::Arrow;
    }
    bool busy() const override { return dragging_ || hasRect_ || grab_ != 0; }

    void onEnter(Editor& e) override
    {
        ed_ = &e;
        reset();
        if (!e.sel.active) return;
        const Rect b = e.sel.bounds();
        if (b.empty()) return;
        crop_ = b.clampTo(e.doc.width, e.doc.height);
        if (!crop_.empty()) hasRect_ = true;
    }
    void onLeave(Editor&) override { reset(); }

    void onPress(Editor& e, const InputFrame& in, int btn) override
    {
        ed_ = &e;
        if (btn != 0 || !e.canvasHas(in.mouse.x, in.mouse.y)) return;

        if (hasRect_) {
            const int g = grabAt(in.mouse, &e);
            if (g == -1) {
                reset();
                dragging_ = true;
                start_ = cur_ = e.canvasImage(in.mouse.x, in.mouse.y);
                return;
            }
            if (in.doubleClick && g == 9) {
                apply(e);
                return;
            }
            grab_ = g;
            grabPt_ = e.canvasImage(in.mouse.x, in.mouse.y);
            cropBase_ = crop_;
            return;
        }

        dragging_ = true;
        start_ = cur_ = e.canvasImage(in.mouse.x, in.mouse.y);
    }

    void onPress(Editor& e) { ed_ = &e; }

    void onDrag(Editor& e, const InputFrame& in, int btn) override
    {
        ed_ = &e;
        if (grab_ != 0) {
            if (btn != 0) return;
            crop_ = adjustCrop(e, e.canvasImage(in.mouse.x, in.mouse.y), in.shift);
            return;
        }
        if (!dragging_ || btn != 0) return;
        cur_ = e.canvasImage(in.mouse.x, in.mouse.y);
        const Rect r = rectFromPoints(start_, cur_, in.shift);
        hasRect_ = r.w > 1 && r.h > 1;
    }

    void onRelease(Editor& e, const InputFrame& in, int btn) override
    {
        if (grab_ != 0) {
            if (btn != 0) return;
            grab_ = 0;
            return;
        }
        if (!dragging_ || btn != 0) return;
        dragging_ = false;
        cur_ = e.canvasImage(in.mouse.x, in.mouse.y);
        crop_ = rectFromPoints(start_, cur_, in.shift).clampTo(e.doc.width, e.doc.height);
        hasRect_ = crop_.w > 1 && crop_.h > 1;
    }

    void onKey(Editor& e, const InputFrame& in) override
    {
        if (in.key == KeyEnter) apply(e);
        if (in.key == KeyEsc) {
            reset();
        }
    }

    void props(Props& p, Editor& e) override
    {
        ed_ = &e;
        p.header("裁剪");
    }


    void overlay(Overlay* dl, const Editor& e, const CanvasViewState& v, const InputFrame& in) override
    {
        if (!dragging_ && !hasRect_) return;
        const Rect r = dragging_ ? rectFromPoints(start_, cur_, in.shift).clampTo(e.doc.width, e.doc.height) : crop_;
        if (r.empty()) return;

        const Vec2f a = v.toScreen((float)r.x, (float)r.y, e.doc.width, e.doc.height);
        const Vec2f b = v.toScreen((float)r.right(), (float)r.bottom(), e.doc.width, e.doc.height);
        const Vec2f imgA = v.toScreen(0, 0, e.doc.width, e.doc.height);
        const Vec2f imgB = v.toScreen((float)e.doc.width, (float)e.doc.height, e.doc.width, e.doc.height);

        const Color32 dark = IE_COL32(0, 0, 0, 120);
        dl->rectFilled(imgA.x, imgA.y, imgB.x, a.y, dark);
        dl->rectFilled(imgA.x, b.y, imgB.x, imgB.y, dark);
        dl->rectFilled(imgA.x, a.y, a.x, b.y, dark);
        dl->rectFilled(b.x, a.y, imgB.x, b.y, dark);

        dl->rect(a.x, a.y, b.x, b.y, IE_COL32(255, 255, 255, 240), 1.5f, 0.0f);

        const Color32 thin = IE_COL32(255, 255, 255, 70);
        for (int i = 1; i <= 2; ++i) {
            const float fx = a.x + (b.x - a.x) * i / 3.0f;
            const float fy = a.y + (b.y - a.y) * i / 3.0f;
            dl->line(fx, a.y, fx, b.y, thin, 1.0f);
            dl->line(a.x, fy, b.x, fy, thin, 1.0f);
        }

        if (!dragging_ && hasRect_) {
            const Vec2f mid[4] = {Vec2f((a.x + b.x) * 0.5f, a.y), Vec2f(b.x, (a.y + b.y) * 0.5f),
                                  Vec2f((a.x + b.x) * 0.5f, b.y), Vec2f(a.x, (a.y + b.y) * 0.5f)};
            const Vec2f corner[4] = {Vec2f(a.x, a.y), Vec2f(b.x, a.y), Vec2f(b.x, b.y), Vec2f(a.x, b.y)};
            const Color32 hf = IE_COL32(255, 255, 255, 255);
            const Color32 hb = IE_COL32(38, 40, 48, 255);
            auto handle = [&](const Vec2f& p) {
                dl->rectFilled(p.x - 4, p.y - 4, p.x + 4, p.y + 4, hf, 1.0f);
                dl->rect(p.x - 4, p.y - 4, p.x + 4, p.y + 4, hb, 1.0f, 1.0f);
            };
            for (int i = 0; i < 4; ++i) handle(corner[i]);
            for (int i = 0; i < 4; ++i) handle(mid[i]);
        }
    }

    bool liveSize(int& w, int& h) override
    {
        if (!ed_) return false;
        Rect r = crop_;
        if (dragging_) {
            r = rectFromPoints(start_, cur_, false).clampTo(ed_->doc.width, ed_->doc.height);
            if (r.w < 0) r.w = 0;
            if (r.h < 0) r.h = 0;
        }
        if (r.empty() && !dragging_) return false;
        w = r.w;
        h = r.h;
        return true;
    }

private:
    Editor* ed_ = nullptr;
    bool dragging_ = false;
    bool hasRect_ = false;
    int grab_ = 0;
    Vec2f start_{}, cur_{}, grabPt_{};
    Rect crop_{}, cropBase_{};

    void reset()
    {
        dragging_ = false;
        hasRect_ = false;
        grab_ = 0;
        crop_ = Rect{};
    }

    int grabAt(const Vec2f& m, const Editor* e) const
    {
        if (!hasRect_ || !e) return -1;
        const int W = e->doc.width, H = e->doc.height;
        const Vec2f a = e->view.toScreen((float)crop_.x, (float)crop_.y, W, H);
        const Vec2f b = e->view.toScreen((float)crop_.right(), (float)crop_.bottom(), W, H);
        const Vec2f q[4] = {Vec2f(a.x, a.y), Vec2f(b.x, a.y), Vec2f(b.x, b.y), Vec2f(a.x, b.y)};
        const float kH = 8.0f;
        for (int i = 0; i < 4; ++i)
            if (std::hypot(m.x - q[i].x, m.y - q[i].y) <= kH) return i + 1;
        const Vec2f mid[4] = {Vec2f((a.x + b.x) * 0.5f, a.y), Vec2f(b.x, (a.y + b.y) * 0.5f),
                              Vec2f((a.x + b.x) * 0.5f, b.y), Vec2f(a.x, (a.y + b.y) * 0.5f)};
        for (int i = 0; i < 4; ++i)
            if (std::hypot(m.x - mid[i].x, m.y - mid[i].y) <= kH) return 5 + i;
        if (m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y) return 9;
        return -1;
    }

    Rect adjustCrop(const Editor& e, const Vec2f& p, bool keepRatio) const
    {
        const Rect base = cropBase_;
        const int W = e.doc.width, H = e.doc.height;
        const auto clampI = [](int v, int lo, int hi) { return std::clamp(v, std::min(lo, hi), std::max(lo, hi)); };
        Rect r = base;
        switch (grab_) {
        case 1: r.x = clampI((int)std::floor(p.x), 0, base.right() - 1); r.y = clampI((int)std::floor(p.y), 0, base.bottom() - 1); r.w = base.right() - r.x; r.h = base.bottom() - r.y; break;
        case 2: r.w = clampI((int)std::ceil(p.x), r.x + 1, W) - r.x; r.y = clampI((int)std::floor(p.y), 0, base.bottom() - 1); r.h = base.bottom() - r.y; break;
        case 3: r.w = clampI((int)std::ceil(p.x), r.x + 1, W) - r.x; r.h = clampI((int)std::ceil(p.y), r.y + 1, H) - r.y; break;
        case 4: r.x = clampI((int)std::floor(p.x), 0, base.right() - 1); r.w = base.right() - r.x; r.h = clampI((int)std::ceil(p.y), r.y + 1, H) - r.y; break;
        case 5: r.y = clampI((int)std::floor(p.y), 0, base.bottom() - 1); r.h = base.bottom() - r.y; break;
        case 6: r.w = clampI((int)std::ceil(p.x), r.x + 1, W) - r.x; break;
        case 7: r.h = clampI((int)std::ceil(p.y), r.y + 1, H) - r.y; break;
        case 8: r.x = clampI((int)std::floor(p.x), 0, base.right() - 1); r.w = base.right() - r.x; break;
        case 9:
            r.x = clampI(base.x + (int)std::lround(p.x - grabPt_.x), 0, W - base.w);
            r.y = clampI(base.y + (int)std::lround(p.y - grabPt_.y), 0, H - base.h);
            break;
        }
        r.w = std::max(1, r.w);
        r.h = std::max(1, r.h);

        if (keepRatio && grab_ >= 1 && grab_ <= 4 && base.w > 0 && base.h > 0) {
            const double bw = (double)base.w, bh = (double)base.h;
            const double sx = (double)r.w / bw, sy = (double)r.h / bh;
            double s = (std::abs(sx - 1.0) >= std::abs(sy - 1.0)) ? sx : sy;
            const double availW = (grab_ == 1 || grab_ == 4) ? (double)base.right() : (double)(W - base.x);
            const double availH = (grab_ == 1 || grab_ == 2) ? (double)base.bottom() : (double)(H - base.y);
            const double sMax = std::min(availW / bw, availH / bh);
            const double sMin = std::max(1.0 / bw, 1.0 / bh);
            s = std::clamp(s, std::min(sMin, sMax), sMax);
            const int nw = std::max(1, (int)std::lround(bw * s));
            const int nh = std::max(1, (int)std::lround(bh * s));
            r.x = (grab_ == 1 || grab_ == 4) ? base.right() - nw : base.x;
            r.y = (grab_ == 1 || grab_ == 2) ? base.bottom() - nh : base.y;
            r.w = nw;
            r.h = nh;
        }
        return r;
    }

    void apply(Editor& e)
    {
        if (!hasRect_ || crop_.empty()) return;
        e.hist.record(e.doc, e.sel, "裁剪");
        e.doc.resizeCanvas(crop_.w, crop_.h, -crop_.x, -crop_.y);
        e.sel.reset(e.doc.width, e.doc.height);
        e.view.fit(e.doc.width, e.doc.height);
        e.afterEdit("裁剪完成");
        reset();
    }
};

}

std::unique_ptr<Tool> makeCropTool() { return std::make_unique<CropTool>(); }

}
