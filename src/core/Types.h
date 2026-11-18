#pragma once

#include <cstdint>
#include <algorithm>

namespace ie {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

    static Rect fromLTRB(int l, int t, int r, int b)
    {
        Rect o;
        o.x = l;
        o.y = t;
        o.w = r - l;
        o.h = b - t;
        return o;
    }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const Rect& o) const { return !(*this == o); }
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }

    Rect intersect(const Rect& o) const
    {
        const int l = std::max(x, o.x);
        const int t = std::max(y, o.y);
        const int r = std::min(right(), o.right());
        const int b = std::min(bottom(), o.bottom());
        return Rect::fromLTRB(l, t, r, b);
    }

    Rect unite(const Rect& o) const
    {
        if (empty()) return o;
        if (o.empty()) return *this;
        return Rect::fromLTRB(std::min(x, o.x), std::min(y, o.y), std::max(right(), o.right()), std::max(bottom(), o.bottom()));
    }

    Rect clampTo(int W, int H) const { return intersect(Rect{0, 0, W, H}); }
};

struct Vec2f {
    float x = 0, y = 0;
    Vec2f() = default;
    Vec2f(float xx, float yy) : x(xx), y(yy) {}
};

struct Vec2i {
    int x = 0, y = 0;
};

enum class BlendMode {
    Normal = 0,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    Add,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Modulate,
    Hue,
    Saturation,
    Color,
    Luminosity
};

constexpr int kBlendModeCount = (int)BlendMode::Luminosity + 1;

inline const char* blendModeName(BlendMode m)
{
    switch (m) {
    case BlendMode::Normal: return "正常";
    case BlendMode::Multiply: return "正片叠底";
    case BlendMode::Screen: return "滤色";
    case BlendMode::Overlay: return "叠加";
    case BlendMode::Darken: return "变暗";
    case BlendMode::Lighten: return "变亮";
    case BlendMode::Add: return "线性减淡";
    case BlendMode::ColorDodge: return "颜色减淡";
    case BlendMode::ColorBurn: return "颜色加深";
    case BlendMode::HardLight: return "强光";
    case BlendMode::SoftLight: return "柔光";
    case BlendMode::Difference: return "差值";
    case BlendMode::Exclusion: return "排除";
    case BlendMode::Modulate: return "调制";
    case BlendMode::Hue: return "色相";
    case BlendMode::Saturation: return "饱和度";
    case BlendMode::Color: return "颜色";
    case BlendMode::Luminosity: return "明度";
    }
    return "正常";
}

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    Color() = default;
    Color(uint8_t rr, uint8_t gg, uint8_t bb, uint8_t aa = 255) : r(rr), g(gg), b(bb), a(aa) {}

    static Color rgb(uint32_t hex) { return Color((uint8_t)(hex >> 16), (uint8_t)(hex >> 8), (uint8_t)hex, 255); }

    uint32_t packed() const
    {
        return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
    }
    static Color fromPacked(uint32_t v)
    {
        return Color((uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff), (uint8_t)((v >> 16) & 0xff), (uint8_t)((v >> 24) & 0xff));
    }
};

}
