#pragma once

#include "core/Types.h"
#include "nodes/Overlay.h"

#include <cstdint>
#include <vector>

class SkCanvas;

namespace ie {

enum class OvKind : uint8_t { Line, RectStroke, RectFill, Circle, PathStroke, PathFill, Text };

struct OverlayOp {
    OvKind kind = OvKind::Line;
    uint8_t closed = 0;
    uint8_t antiAlias = 1;
    Color32 color = 0;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float width = 0.0f, rounding = 0.0f;
    uint32_t ptBegin = 0, ptEnd = 0;
    uint32_t textBegin = 0, textEnd = 0;
    Rect bounds{};
};

struct OverlayRecording {
    std::vector<OverlayOp> ops;
    std::vector<Vec2f> pts;
    std::vector<char> text;

    void reset()
    {
        ops.clear();
        pts.clear();
        text.clear();
    }
};

bool overlayOpSame(const OverlayRecording& a, size_t i, const OverlayRecording& b, size_t j);

class OverlayRecorder : public Overlay {
public:
    explicit OverlayRecorder(OverlayRecording* out) : r_(out) {}

    void line(float x0, float y0, float x1, float y1, Color32 col, float width, bool aa) override;
    void rect(float x0, float y0, float x1, float y1, Color32 col, float width, float rounding) override;
    void rectFilled(float x0, float y0, float x1, float y1, Color32 col, float rounding) override;
    void circle(float cx, float cy, float radius, Color32 col, float width) override;
    void triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color32 col,
                  float width) override;
    void triangleFilled(float x0, float y0, float x1, float y1, float x2, float y2, Color32 col) override;
    void polyline(const Vec2f* pts, int count, Color32 col, float width, bool closed) override;
    void polygonFilled(const Vec2f* pts, int count, Color32 col) override;
    void text(float x, float y, Color32 col, const char* str) override;
    float textWidth(const char* str) override;
    float textHeight() override;

private:
    OverlayRecording* r_;
};

void replayOverlay(SkCanvas& canvas, const OverlayRecording& rec);

}
