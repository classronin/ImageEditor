#include "tools/Tools.h"

#include <algorithm>

namespace ie {

namespace {

const char* const kFillItems[] = {"前景色", "背景色", "图案"};

class BucketTool final : public Tool {
public:
    ToolId id() const override { return ToolId::Bucket; }
    const char* name() const override { return "油漆桶"; }

    CursorShape cursor(const InputFrame&) const override { return CursorShape::Arrow; }

    void onPress(Editor& e, const InputFrame& in, int btn) override
    {
        if (btn != 0) return;
        if (!e.canvasHas(in.mouse.x, in.mouse.y)) return;
        const Vec2i px = e.canvasPixel(in.mouse.x, in.mouse.y);
        const Editor::BucketFill content = (Editor::BucketFill)std::clamp(fill_, 0, 2);
        e.bucketFill(px.x, px.y, tolerance_, content);
    }

    void props(Props& p, Editor&) override
    {
        p.header("油漆桶");
        p.combo("填充", &fill_, kFillItems, 3, &BucketTool::setFillV, this);
        p.sliderI("容差", &tolerance_, 0, 255, &BucketTool::setTolV, this);
    }

private:
    static void setFillV(double v, void* ud) { static_cast<BucketTool*>(ud)->fill_ = (int)v; }
    static void setTolV(double v, void* ud) { static_cast<BucketTool*>(ud)->tolerance_ = (int)v; }

    int fill_ = 0;
    int tolerance_ = 30;
};

}

std::unique_ptr<Tool> makeBucketTool() { return std::make_unique<BucketTool>(); }

}
