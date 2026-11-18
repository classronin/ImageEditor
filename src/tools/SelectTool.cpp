#include "tools/Tools.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ie {

const char* const kSelModeItems[4] = {"新选区", "添加到选区", "从选区减去", "与选区交叉"};
const SegIcon kSelModeIcons[4] = {SegIcon::New, SegIcon::Add, SegIcon::Sub, SegIcon::Inter};

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

class SelectTool final : public Tool {
public:
    ToolId id() const override { return ToolId::Select; }
    const char* name() const override { return "选区"; }

    CursorShape cursor(const InputFrame&) const override { return CursorShape::Arrow; }
    bool busy() const override { return marquee_ || movingSel_; }

    static constexpr const char* kShapeItems[] = {"矩形", "椭圆"};

    void onEnter(Editor&) override { marquee_ = false; movingSel_ = false; }
    void onLeave(Editor&) override { marquee_ = false; movingSel_ = false; }

    void onPress(Editor& e, const InputFrame& in, int btn) override
    {
        if (btn != 0) return;
        if (!e.canvasHas(in.mouse.x, in.mouse.y)) return;

        selBefore_ = e.sel;
        const Vec2i ip = e.canvasPixel(in.mouse.x, in.mouse.y);
        if (e.sel.active && e.sel.contains(ip.x, ip.y)) {
            movingSel_ = true;
            marquee_ = false;
            selStart_ = e.canvasImage(in.mouse.x, in.mouse.y);
            selAppliedX_ = 0;
            selAppliedY_ = 0;
            moved_ = false;
            return;
        }

        marquee_ = true;
        start_ = e.canvasImage(in.mouse.x, in.mouse.y);
        cur_ = start_;
        liveW_ = 0;
        liveH_ = 0;
        mode_ = in.shift ? ((selMode_ == 0 && !e.sel.active) ? 0 : 1) : (in.alt ? 2 : selMode_);
        if (mode_ == 0) applyReplace(e, rectFromPoints(start_, cur_, in.shift));
    }

    void onDrag(Editor& e, const InputFrame& in, int btn) override
    {
        if (btn != 0) return;
        if (movingSel_) {
            const Vec2f p = e.canvasImage(in.mouse.x, in.mouse.y);
            const int dx = (int)std::lround(p.x - selStart_.x);
            const int dy = (int)std::lround(p.y - selStart_.y);
            const int mx = dx - selAppliedX_, my = dy - selAppliedY_;
            if (mx != 0 || my != 0) {
                e.sel.translate(mx, my);
                selAppliedX_ = dx;
                selAppliedY_ = dy;
                moved_ = true;
                e.doc.markAllDirty();
            }
            return;
        }
        if (!marquee_) return;
        cur_ = e.canvasImage(in.mouse.x, in.mouse.y);
        Rect lr = rectFromPoints(start_, cur_, in.shift && mode_ == 0).clampTo(e.doc.width, e.doc.height);
        liveW_ = std::max(0, lr.w);
        liveH_ = std::max(0, lr.h);
        if (mode_ == 0) applyReplace(e, rectFromPoints(start_, cur_, in.shift));
    }

    void onRelease(Editor& e, const InputFrame& in, int btn) override
    {
        if (movingSel_) {
            movingSel_ = false;
            if (moved_) e.recordSelectionChange(selBefore_, "移动选区");
            return;
        }
        if (!marquee_ || btn != 0) return;
        marquee_ = false;
        cur_ = e.canvasImage(in.mouse.x, in.mouse.y);

        const bool square = in.shift && mode_ == 0;
        Rect r = rectFromPoints(start_, cur_, square).clampTo(e.doc.width, e.doc.height);
        if (r.w < 1 || r.h < 1) {
            if (mode_ == 0) {
                e.sel.clear();
                e.recordSelectionChange(selBefore_, "取消选择");
            }
            return;
        }
        const bool ellipse = shape_ == 1;
        if (mode_ == 1) {
            if (ellipse) e.sel.addEllipse(r);
            else e.sel.addRect(r);
        } else if (mode_ == 2) {
            if (ellipse) e.sel.subtractEllipse(r);
            else e.sel.subtractRect(r);
        } else if (mode_ == 3) {
            if (ellipse) e.sel.intersectEllipse(r);
            else e.sel.intersectRect(r);
        } else {
        }
        e.recordSelectionChange(selBefore_, "选区");
    }

    void onKey(Editor& e, const InputFrame& in) override
    {
        if (in.key == KeyEsc) {
            marquee_ = false;
            if (e.sel.active) {
                Selection before = e.sel;
                e.sel.clear();
                e.recordSelectionChange(before, "取消选择");
            }
        }
        if (in.key == KeyEnter) marquee_ = false;
        int dx = 0, dy = 0;
        switch (in.key) {
            case KeyLeft: dx = -1; break;
            case KeyRight: dx = 1; break;
            case KeyUp: dy = -1; break;
            case KeyDown: dy = 1; break;
            default: break;
        }
        if ((dx != 0 || dy != 0) && e.sel.active && !marquee_) {
            const int step = in.shift ? 10 : 1;
            Selection before = e.sel;
            e.sel.translate(dx * step, dy * step);
            e.doc.markAllDirty();
            e.recordSelectionChange(before, "移动选区");
        }
    }

    void props(Props& p, Editor& e) override
    {
        ed_ = &e;
        p.header("选区");
        p.seg(&selMode_, kSelModeItems, 4, &SelectTool::setModeV, this, kSelModeIcons);
        p.combo("形状", &shape_, kShapeItems, 2, &SelectTool::setShapeV, this, 100);
    }

    void overlay(Overlay* dl, const Editor& e, const CanvasViewState& v, const InputFrame& in) override
    {
        (void)in;
        if (!marquee_) return;
        const Rect r = rectFromPoints(start_, cur_, in.shift && mode_ == 0);
        const Vec2f a = v.toScreen((float)r.x, (float)r.y, e.doc.width, e.doc.height);
        const Vec2f b = v.toScreen((float)r.right(), (float)r.bottom(), e.doc.width, e.doc.height);

        const float dash = 5.0f, gap = 4.0f;
        auto dashLine = [&](Vec2f p0, Vec2f p1) {
            const float len = std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y));
            if (len < 0.5f) return;
            const float ux = (p1.x - p0.x) / len, uy = (p1.y - p0.y) / len;
            float t = 0;
            bool on = true;
            while (t < len) {
                const float seg = std::min(on ? dash : gap, len - t);
                if (on)
                    dl->line(p0.x + ux * t, p0.y + uy * t, p0.x + ux * (t + seg), p0.y + uy * (t + seg),
                             IE_COL32(255, 255, 255, 220), 1.0f);
                t += seg;
                on = !on;
            }
        };
        if (shape_ == 0) {
            dashLine(Vec2f(a.x, a.y), Vec2f(b.x, a.y));
            dashLine(Vec2f(b.x, a.y), Vec2f(b.x, b.y));
            dashLine(Vec2f(b.x, b.y), Vec2f(a.x, b.y));
            dashLine(Vec2f(a.x, b.y), Vec2f(a.x, a.y));
        }
        if (shape_ == 1) {
            const float ccx = (a.x + b.x) * 0.5f, ccy = (a.y + b.y) * 0.5f;
            const float rx = (b.x - a.x) * 0.5f, ry = (b.y - a.y) * 0.5f;
            const int kSeg = 72;
            Vec2f prev{ccx + rx, ccy};
            for (int i = 1; i <= kSeg; ++i) {
                const float th = 6.2831853f * (float)i / (float)kSeg;
                Vec2f next{ccx + rx * std::cos(th), ccy + ry * std::sin(th)};
                dashLine(prev, next);
                prev = next;
            }
        }

    }

    bool liveSize(int& w, int& h) override
    {
        if (!marquee_) return false;
        w = liveW_;
        h = liveH_;
        return true;
    }

private:
    void applyReplace(Editor& e, const Rect& r0)
    {
        const Rect r = r0.clampTo(e.doc.width, e.doc.height);
        if (shape_ == 1) e.sel.setEllipse(r);
        else e.sel.setRect(r);
    }

    static void setShapeV(double v, void* ud) { static_cast<SelectTool*>(ud)->shape_ = (int)v; }
    static void setModeV(double v, void* ud) { static_cast<SelectTool*>(ud)->selMode_ = (int)v; }

    Editor* ed_ = nullptr;
    bool marquee_ = false;
    bool movingSel_ = false;
    bool moved_ = false;
    Selection selBefore_;
    int mode_ = 0;
    int selMode_ = 0;
    int shape_ = 0;
    int liveW_ = 0, liveH_ = 0;
    Vec2f start_{}, cur_{};
    Vec2f selStart_{};
    int selAppliedX_ = 0, selAppliedY_ = 0;
};

}

std::unique_ptr<Tool> makeSelectTool() { return std::make_unique<SelectTool>(); }

}
