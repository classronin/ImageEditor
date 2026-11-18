#include "tools/Tools.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ie {

namespace {

constexpr float kPi = 3.14159265358979f;

static void antsPolyline(Overlay* dl, const std::vector<Vec2f>& pts, const CanvasViewState& v, int W, int H,
                         float phase, Color32 col)
{
    const float dash = 4.0f, gap = 4.0f;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec2f a = v.toScreen(pts[i].x, pts[i].y, W, H);
        const Vec2f b = v.toScreen(pts[i + 1].x, pts[i + 1].y, W, H);
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.3f) continue;
        const float ux = dx / len, uy = dy / len;
        float t = -std::fmod(phase, dash + gap);
        bool white = true;
        while (t < len) {
            const float t0 = std::max(0.0f, t);
            const float t1 = std::min(len, t + (white ? dash : gap));
            if (t1 > t0 && white)
                dl->line(a.x + ux * t0, a.y + uy * t0, a.x + ux * t1, a.y + uy * t1, col, 1.0f);
            t += (white ? dash : gap);
            white = !white;
        }
    }
}

class LassoTool final : public Tool {
public:
    ToolId id() const override { return ToolId::Lasso; }
    const char* name() const override { return "多边形套索"; }

    CursorShape cursor(const InputFrame&) const override { return CursorShape::Arrow; }
    bool busy() const override { return drawing_; }

    void onEnter(Editor&) override { reset(); }
    void onLeave(Editor&) override { reset(); }

    void onPress(Editor& e, const InputFrame& in, int btn) override
    {
        if (btn != 0 || !e.canvasHas(in.mouse.x, in.mouse.y)) return;
        const Vec2f p = e.canvasImage(in.mouse.x, in.mouse.y);

        if (drawing_) {
            if (in.doubleClick || nearStart(p)) {
                finish(e);
                return;
            }
            addAnchor(e, p, in.shift);
            return;
        }

        drawing_ = true;
        pts_.clear();
        pts_.push_back(p);
        if (in.shift || in.alt) selMode_ = in.shift ? (in.alt ? 3 : 1) : 2;

    }

    void onDrag(Editor&, const InputFrame&, int) override {}

    void onRelease(Editor&, const InputFrame&, int) override {}

    bool consumesKey(int scancode) const override
    {
        return drawing_ && (scancode == KeyDelete || scancode == KeyBackspace);
    }

    void onKey(Editor& e, const InputFrame& in) override
    {
        if (!drawing_) return;
        if (in.key == KeyEnter) {
            finish(e);
            return;
        }
        if (in.key == KeyEsc) {
            cancel(e);
            return;
        }
        if (in.key == KeyDelete || in.key == KeyBackspace) {
            if (pts_.size() > 1) {
                pts_.pop_back();
            } else {
                cancel(e);
            }
        }
    }

    void props(Props& p, Editor& e) override
    {
        ed_ = &e;
        p.header(name());
        p.seg(&selMode_, kSelModeItems, 4, &LassoTool::setModeV, this, kSelModeIcons);
    }

    void overlay(Overlay* dl, const Editor& e, const CanvasViewState& v, const InputFrame& in) override
    {
        if (!drawing_ || pts_.empty()) return;
        const int W = e.doc.width, H = e.doc.height;
        const float phase = 0.0f;
        const Vec2f cur = e.canvasImage(in.mouse.x, in.mouse.y);

        antsPolyline(dl, pts_, v, W, H, phase, IE_COL32(255, 255, 255, 230));
        std::vector<Vec2f> tail = {pts_.back(), cur};
        antsPolyline(dl, tail, v, W, H, phase, IE_COL32(255, 255, 255, 230));
        if (!nearStart(cur)) {
            std::vector<Vec2f> close = {cur, pts_.front()};
            antsPolyline(dl, close, v, W, H, phase, IE_COL32(255, 255, 255, 90));
        }
    }

private:
    Editor* ed_ = nullptr;
    bool drawing_ = false;
    int selMode_ = 0;
    static void setModeV(double v, void* ud) { static_cast<LassoTool*>(ud)->selMode_ = (int)v; }
    std::vector<Vec2f> pts_;

    void reset()
    {
        drawing_ = false;
        pts_.clear();
    }

    bool nearStart(const Vec2f& p) const
    {
        return !pts_.empty() && std::hypot(p.x - pts_.front().x, p.y - pts_.front().y) <= 4.0f;
    }

    void addAnchor(Editor& e, Vec2f p, bool shift45)
    {
        if (shift45 && !pts_.empty()) {
            const Vec2f& a = pts_.back();
            const float ang = std::atan2(p.y - a.y, p.x - a.x);
            const float snapped = std::round(ang / (kPi / 4.0f)) * (kPi / 4.0f);
            const float len = std::hypot(p.x - a.x, p.y - a.y);
            p.x = a.x + std::cos(snapped) * len;
            p.y = a.y + std::sin(snapped) * len;
        }
        pts_.push_back(p);
    }

    void cancel(Editor& e)
    {
        (void)e;
        reset();
    }

    void finish(Editor& e)
    {
        drawing_ = false;
        if (pts_.size() >= 3) {
            Selection before = e.sel;
            if (selMode_ == 1)
                e.sel.addPolygon(pts_);
            else if (selMode_ == 2)
                e.sel.subtractPolygon(pts_);
            else if (selMode_ == 3)
                e.sel.intersectPolygon(pts_);
            else
                e.sel.selectPolygon(pts_);
            e.recordSelectionChange(before, "多边形套索");
        }
        pts_.clear();
    }
};

}

std::unique_ptr<Tool> makeLassoTool() { return std::make_unique<LassoTool>(); }

}
