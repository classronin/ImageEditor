#pragma once

#include "core/Types.h"

#include <cstdint>

namespace ie {

using Color32 = uint32_t;

inline Color32 rgba(int r, int g, int b, int a = 255)
{
    return ((Color32)(a & 255) << 24) | ((Color32)(r & 255) << 16) | ((Color32)(g & 255) << 8) |
           (Color32)(b & 255);
}

#define IE_COL32(r, g, b, a) ::ie::rgba(r, g, b, a)

class Overlay {
public:
    virtual ~Overlay() = default;

    virtual void line(float x0, float y0, float x1, float y1, Color32 col, float width = 1.0f,
                      bool antiAlias = true) = 0;
    virtual void rect(float x0, float y0, float x1, float y1, Color32 col, float width = 1.0f, float rounding = 0.0f) = 0;
    virtual void rectFilled(float x0, float y0, float x1, float y1, Color32 col, float rounding = 0.0f) = 0;
    virtual void circle(float cx, float cy, float radius, Color32 col, float width = 1.0f) = 0;
    virtual void triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color32 col,
                          float width = 1.0f) = 0;
    virtual void triangleFilled(float x0, float y0, float x1, float y1, float x2, float y2, Color32 col) = 0;
    virtual void polyline(const Vec2f* pts, int count, Color32 col, float width, bool closed) = 0;
    virtual void polygonFilled(const Vec2f* pts, int count, Color32 col) = 0;
    virtual void text(float x, float y, Color32 col, const char* str) = 0;
    virtual float textWidth(const char* str) = 0;
    virtual float textHeight() = 0;
};

}
