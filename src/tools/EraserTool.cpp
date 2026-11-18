#include "tools/Paint.h"
#include "tools/Tools.h"

#include <algorithm>
#include <cmath>

namespace ie {

namespace {

const char* const kEraseToItems[] = {"透明", "背景色"};

class EraserTool final : public Tool {
public:
    ToolId id() const override { return ToolId::Eraser; }
    const char* name() const override { return "橡皮"; }

    CursorShape cursor(const InputFrame&) const override { return CursorShape::None; }
    bool busy() const override { return drawing_; }

    void onEnter(Editor&) override { drawing_ = false; }
    void onLeave(Editor&) override { drawing_ = false; }

    void onPress(Editor& e, const InputFrame& in, int btn) override
    {
        if (btn != 0 || !e.canvasHas(in.mouse.x, in.mouse.y)) return;
        if (!e.editableLayer()) return;
        const Vec2f p = e.canvasImage(in.mouse.x, in.mouse.y);

        e.hist.record(e.doc, e.sel, name());
        e.doc.ensureLayerRect(e.doc.active, dabBounds(p.x, p.y, dab().radius));
        Layer* l = e.doc.activeLayer();

        drawing_ = true;
        pending_ = 0.0f;
        lastX_ = p.x;
        lastY_ = p.y;

        Rect dirty;
        stamp(e, l, p.x, p.y, dirty);
        e.doc.markDirty(dirty, e.doc.active);
        e.afterEdit(name());
    }

    void onDrag(Editor& e, const InputFrame& in, int btn) override
    {
        if (!drawing_ || btn != 0) return;
        Layer* l = e.doc.activeLayer();
        if (!l) return;

        const Vec2f p = e.canvasImage(in.mouse.x, in.mouse.y);
        const float dx = p.x - lastX_;
        const float dy = p.y - lastY_;
        const float seg = std::sqrt(dx * dx + dy * dy);
        if (seg < 1e-4f) return;

        const float spacing = std::max(1.0f, dab().radius * 0.22f);
        Rect dirty;
        float travel = pending_ + seg;
        int guard = 0;
        while (travel >= spacing && guard++ < 4096) {
            const float overshoot = travel - spacing;
            const float t = 1.0f - overshoot / seg;
            const float px = lastX_ + dx * t, py = lastY_ + dy * t;
            e.doc.ensureLayerRect(e.doc.active, dabBounds(px, py, dab().radius));
            l = e.doc.activeLayer();
            stamp(e, l, px, py, dirty);
            travel = overshoot;
        }
        pending_ = travel;

        lastX_ = p.x;
        lastY_ = p.y;
        e.doc.markDirty(dirty, e.doc.active);
    }

    void onRelease(Editor&, const InputFrame&, int) override { drawing_ = false; }

    void props(Props& p, Editor& e) override
    {
        p.header("橡皮");
        p.combo("擦除到", &eraseTo_, kEraseToItems, 2, &EraserTool::setEraseToV, this);
        p.sliderF("大小", &size_, 1.0f, 400.0f, &EraserTool::setSizeV, this, "%.0f px");
        p.sliderF("硬度", &hardness_, 0.0f, 1.0f, &EraserTool::setHardnessV, this, "%.2f");
        p.sliderF("不透明度", &opacity_, 0.0f, 1.0f, &EraserTool::setOpacityV, this, "%.2f");
    }

    void overlay(Overlay* dl, const Editor& e, const CanvasViewState& v, const InputFrame& in) override
    {
        if (!e.canvasHas(in.mouse.x, in.mouse.y)) return;
        const Vec2f ip = e.canvasImage(in.mouse.x, in.mouse.y);
        const Vec2f c = v.toScreen(ip.x, ip.y, e.doc.width, e.doc.height);
        const float r = std::max(1.5f, size_ * 0.5f * v.zoom);
        dl->circle(c.x, c.y, r + 0.5f, IE_COL32(0, 0, 0, 140), 1.5f);
        dl->circle(c.x, c.y, r, IE_COL32(255, 255, 255, 220), 1.0f);
    }

private:
    static void setEraseToV(double v, void* ud) { static_cast<EraserTool*>(ud)->eraseTo_ = (int)v; }
    static void setSizeV(double v, void* ud) { static_cast<EraserTool*>(ud)->size_ = (float)v; }
    static void setHardnessV(double v, void* ud) { static_cast<EraserTool*>(ud)->hardness_ = (float)v; }
    static void setOpacityV(double v, void* ud) { static_cast<EraserTool*>(ud)->opacity_ = (float)v; }
    static void setBgColor(const float* c, void* ud)
    {
        static_cast<Editor*>(ud)->palette.setBg(c);
    }

    void stamp(Editor& e, Layer* l, float x, float y, Rect& dirty)
    {
        const bool erase = eraseTo_ == 0;
        const Color col = erase ? Color() : e.palette.bgColor();
        stampDab(l->img, l->x, l->y, e.sel.maskPtr(), e.doc.width, e.doc.height, dirty, x, y, dab(), col, erase,
                 l->lockAlpha);
    }

    bool drawing_ = false;
    float lastX_ = 0, lastY_ = 0;
    float pending_ = 0.0f;
    int eraseTo_ = 0;
    float size_ = 26.0f;
    float hardness_ = 0.85f;
    float opacity_ = 1.0f;

    Dab dab() const
    {
        Dab d;
        d.radius = std::max(0.5f, size_ * 0.5f);
        d.hardness = hardness_;
        d.opacity = opacity_;
        return d;
    }
};

}

std::unique_ptr<Tool> makeEraserTool() { return std::make_unique<EraserTool>(); }

}
