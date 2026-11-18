#include "tools/Tools.h"

#include <algorithm>
#include <cmath>

namespace ie {

namespace {

void stampMask(std::vector<uint8_t>& m, int w, int h, float cx, float cy, float r, float hardness,
               float opacity, bool erase)
{
    if (m.empty() || w <= 0 || h <= 0) return;
    const float rr = std::max(0.5f, r);
    const int x0 = std::max(0, (int)std::floor(cx - rr));
    const int y0 = std::max(0, (int)std::floor(cy - rr));
    const int x1 = std::min(w - 1, (int)std::ceil(cx + rr));
    const int y1 = std::min(h - 1, (int)std::ceil(cy + rr));
    if (x0 > x1 || y0 > y1) return;

    const float hard = std::clamp(hardness, 0.0f, 1.0f);
    const float soft = 1.0f - hard;
    const float inv = soft > 0.0001f ? 1.0f / soft : 0.0f;
    const float op = std::clamp(opacity, 0.0f, 1.0f);

    for (int y = y0; y <= y1; ++y) {
        uint8_t* row = m.data() + (size_t)y * (size_t)w;
        const float dy = ((float)y + 0.5f) - cy;
        for (int x = x0; x <= x1; ++x) {
            const float dx = ((float)x + 0.5f) - cx;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > rr) continue;

            float cover = 1.0f;
            if (inv > 0.0f) {
                const float t = d / rr;                       // 0=中心 1=边缘
                cover = std::clamp((1.0f - t) * inv, 0.0f, 1.0f); // 硬度内实心，外侧线性衰减
            }
            cover *= op;
            if (cover <= 0.0f) continue;

            uint8_t& v = row[x];
            if (erase) {
                const float cur = (float)v;
                v = (uint8_t)std::clamp(cur + (255.0f - cur) * cover, 0.0f, 255.0f);
            } else {
                const float cur = (float)v;
                v = (uint8_t)std::clamp(cur * (1.0f - cover), 0.0f, 255.0f);
            }
        }
    }
}

class MaskBrushTool final : public Tool {
public:
    ToolId id() const override { return ToolId::Mask; }
    const char* name() const override { return "抠图"; }

    CursorShape cursor(const InputFrame&) const override { return CursorShape::None; }
    bool busy() const override { return drawing_; }

    void onEnter(Editor&) override { drawing_ = false; }
    void onLeave(Editor& e) override
    {
        drawing_ = false;
        if (!e.doc.hasUserMask(e.doc.active)) return;
        e.hist.record(e.doc, e.sel, "清除蒙版");
        e.doc.dropUserMask(e.doc.active);
        e.afterEdit("清除蒙版");
    }

    void onKey(Editor& e, const InputFrame& in) override
    {
        if (in.key == KeyEnter) apply(e);
    }

    void onPress(Editor& e, const InputFrame& in, int btn) override
    {
        if (btn != 0 || !e.canvasHas(in.mouse.x, in.mouse.y)) return;
        Layer* l = e.doc.activeLayer();
        if (!l || !e.editableLayer() || l->img.empty()) return;

        e.hist.record(e.doc, e.sel, name());
        if (!e.doc.ensureUserMask(e.doc.active)) return;

        drawing_ = true;
        pending_ = 0.0f;
        const Vec2f p = e.canvasImage(in.mouse.x, in.mouse.y);
        lastX_ = p.x;
        lastY_ = p.y;
        paintAt(e, p.x, p.y);
    }

    void onDrag(Editor& e, const InputFrame& in, int btn) override
    {
        if (!drawing_ || btn != 0) return;
        Layer* l = e.doc.activeLayer();
        if (!l || l->uMask.empty()) return;

        const Vec2f p = e.canvasImage(in.mouse.x, in.mouse.y);
        const float dx = p.x - lastX_;
        const float dy = p.y - lastY_;
        const float seg = std::sqrt(dx * dx + dy * dy);
        if (seg < 1e-4f) return;

        const float spacing = std::max(1.0f, radius() * 0.22f);
        float travel = pending_ + seg;
        int guard = 0;
        while (travel >= spacing && guard++ < 4096) {
            const float overshoot = travel - spacing;
            const float t = 1.0f - overshoot / seg;
            paintAt(e, lastX_ + dx * t, lastY_ + dy * t);
            travel = overshoot;
        }
        pending_ = travel;
        lastX_ = p.x;
        lastY_ = p.y;
    }

    void onRelease(Editor&, const InputFrame&, int) override { drawing_ = false; }

    void props(Props& p, Editor&) override
    {
        p.header("抠图");
        p.sliderF("大小", &size_, 1.0f, 400.0f, &MaskBrushTool::setSizeV, this, "%.0f px");
        p.sliderF("硬度", &hardness_, 0.0f, 1.0f, &MaskBrushTool::setHardnessV, this, "%.2f");
        p.sliderF("不透明度", &opacity_, 0.0f, 1.0f, &MaskBrushTool::setOpacityV, this, "%.2f");
        p.sliderI("收边", &band_, 0, 40, &MaskBrushTool::setBandV, this);
        p.check("橡皮", &erase_, &MaskBrushTool::setEraseV, this);
    }

    void overlay(Overlay* dl, const Editor& e, const CanvasViewState& v, const InputFrame& in) override
    {
        if (!e.canvasHas(in.mouse.x, in.mouse.y)) return;
        const Vec2f ip = e.canvasImage(in.mouse.x, in.mouse.y);
        const Vec2f c = v.toScreen(ip.x, ip.y, e.doc.width, e.doc.height);
        const float r = std::max(1.5f, size_ * 0.5f * v.zoom);
        dl->circle(c.x, c.y, r + 0.5f, IE_COL32(0, 0, 0, 140), 1.5f);
        dl->circle(c.x, c.y, r, IE_COL32(255, 255, 255, 220), 1.0f);
        dl->circle(c.x, c.y, 1.5f, IE_COL32(255, 255, 255, 200), 1.0f);
        if (erase_)
            dl->circle(c.x, c.y, r - 3.0f > 1.0f ? r - 3.0f : r, IE_COL32(0, 230, 255, 180), 1.0f);
    }

private:
    static void setSizeV(double v, void* ud) { static_cast<MaskBrushTool*>(ud)->size_ = (float)v; }
    static void setHardnessV(double v, void* ud) { static_cast<MaskBrushTool*>(ud)->hardness_ = (float)v; }
    static void setOpacityV(double v, void* ud) { static_cast<MaskBrushTool*>(ud)->opacity_ = (float)v; }
    static void setEraseV(double v, void* ud) { static_cast<MaskBrushTool*>(ud)->erase_ = v != 0.0; }
    static void setBandV(double v, void* ud) { static_cast<MaskBrushTool*>(ud)->band_ = (int)v; }

    void apply(Editor& e)
    {
        if (!e.doc.hasUserMask(e.doc.active)) return;
        e.hist.record(e.doc, e.sel, "抠图");
        e.doc.applyUserMask(e.doc.active, band_);
        e.afterEdit("抠图");
    }

    float radius() const { return std::max(0.5f, size_ * 0.5f); }

    void paintAt(Editor& e, float x, float y)
    {
        Layer* l = e.doc.activeLayer();
        if (!l || l->uMask.empty()) return;
        stampMask(l->uMask, l->uMaskW, l->uMaskH, x - (float)l->x, y - (float)l->y, radius(),
                  hardness_, opacity_, erase_);
        ++l->uMaskRev;
        const int r = (int)(radius() + 2.0f);
        Rect dirty = Rect::fromLTRB((int)x - r, (int)y - r, (int)x + r + 1, (int)y + r + 1);
        e.doc.markDirty(dirty, e.doc.active);
    }

    bool drawing_ = false;
    bool erase_ = false;
    int band_ = 14;
    float lastX_ = 0, lastY_ = 0;
    float pending_ = 0.0f;
    float size_ = 60.0f;
    float hardness_ = 0.7f;
    float opacity_ = 1.0f;
};

}

std::unique_ptr<Tool> makeMaskTool() { return std::make_unique<MaskBrushTool>(); }

}
