#include "tools/Tools.h"

#include <cstdio>

namespace ie {

namespace {

class WandTool final : public Tool {
public:
    ToolId id() const override { return ToolId::Wand; }
    const char* name() const override { return "魔棒"; }

    CursorShape cursor(const InputFrame&) const override { return CursorShape::Arrow; }

    void onPress(Editor& e, const InputFrame& in, int btn) override
    {
        if (btn != 0) return;
        if (!e.canvasHas(in.mouse.x, in.mouse.y)) return;
        const Vec2i px = e.canvasPixel(in.mouse.x, in.mouse.y);
        int modeIdx = selMode_;
        if (in.shift || in.alt) modeIdx = in.shift ? (in.alt ? 3 : 1) : 2;
        const Editor::WandMode mode = (Editor::WandMode)modeIdx;
        Selection before = e.sel;
        e.wandSelect(px.x, px.y, tolerance_, mode);
        e.recordSelectionChange(before, "魔棒");
    }

    void props(Props& p, Editor& e) override
    {
        ed_ = &e;
        p.header("魔棒");
        p.seg(&selMode_, kSelModeItems, 4, &WandTool::setModeV, this, kSelModeIcons);
        p.sliderI("容差", &tolerance_, 0, 255, &WandTool::setTolV, this);
    }

private:
    static void setTolV(double v, void* ud) { static_cast<WandTool*>(ud)->tolerance_ = (int)v; }
    static void setModeV(double v, void* ud) { static_cast<WandTool*>(ud)->selMode_ = (int)v; }

    Editor* ed_ = nullptr;
    int tolerance_ = 32;
    int selMode_ = 0;
};

}

std::unique_ptr<Tool> makeWandTool() { return std::make_unique<WandTool>(); }

}
