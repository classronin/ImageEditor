#pragma once

#include "app/ViewState.h"
#include "core/Types.h"
#include "nodes/Overlay.h"

#include <cmath>

namespace ie {
namespace guides {

constexpr int kComposeDash = 4;

inline void dashLine(Overlay* ov, float x0, float y0, float x1, float y1, Color32 col)
{
    const float len = std::fabs(x1 - x0) >= std::fabs(y1 - y0) ? std::fabs(x1 - x0) : std::fabs(y1 - y0);
    const float ux = x1 >= x0 ? 1.0f : -1.0f;
    const float uy = y1 >= y0 ? 1.0f : -1.0f;
    const bool horiz = std::fabs(x1 - x0) >= std::fabs(y1 - y0);
    for (float t = 0.0f; t < len; t += (float)(2 * kComposeDash)) {
        const float e = t + (float)kComposeDash < len ? t + (float)kComposeDash : len;
        if (horiz) ov->line(x0 + ux * t, y0, x0 + ux * e, y0, col, 1.0f, false);
        else ov->line(x0, y0 + uy * t, x0, y0 + uy * e, col, 1.0f, false);
    }
}

inline void drawCompose(Overlay* ov, const CanvasViewState& v, int imgW, int imgH)
{
    if (!ov || imgW <= 0 || imgH <= 0) return;
    const Color32 col = IE_COL32(255, 255, 255, 200);
    const Vec2f a = v.toScreen(0.0f, 0.0f, imgW, imgH);
    const Vec2f b = v.toScreen((float)imgW, (float)imgH, imgW, imgH);
    for (int i = 1; i <= 2; ++i) {
        const float fx = (float)imgW * (float)i / 3.0f;
        const float x = v.toScreen(fx, 0.0f, imgW, imgH).x;
        dashLine(ov, x, a.y, x, b.y, col);
        const float fy = (float)imgH * (float)i / 3.0f;
        const float y = v.toScreen(0.0f, fy, imgW, imgH).y;
        dashLine(ov, a.x, y, b.x, y, col);
    }
}

}
}
