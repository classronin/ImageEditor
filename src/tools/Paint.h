#pragma once

#include "core/Blend.h"
#include "core/Image.h"

#include <algorithm>
#include <cmath>

namespace ie {

struct Dab {
    float radius = 8.0f;
    float hardness = 0.8f;
    float opacity = 1.0f;
};

inline Rect dabBounds(float cx, float cy, float r)
{
    return Rect::fromLTRB((int)std::floor(cx - r - 1.0f), (int)std::floor(cy - r - 1.0f),
                          (int)std::ceil(cx + r + 1.0f), (int)std::ceil(cy + r + 1.0f));
}

inline void stampDab(Image& img, int ox, int oy, const uint8_t* mask, int mw, int mh, Rect& dirty, float cx, float cy,
                     const Dab& dab, Color color, bool erase, bool lockAlpha = false)
{
    if (img.empty()) return;

    const float r = std::max(0.5f, dab.radius);
    int x0 = std::max(ox, (int)std::floor(cx - r - 1.0f));
    int y0 = std::max(oy, (int)std::floor(cy - r - 1.0f));
    int x1 = std::min(ox + img.width(), (int)std::ceil(cx + r + 1.0f));
    int y1 = std::min(oy + img.height(), (int)std::ceil(cy + r + 1.0f));
    if (mask) {
        x0 = std::max(x0, 0);
        y0 = std::max(y0, 0);
        x1 = std::min(x1, mw);
        y1 = std::min(y1, mh);
    }
    if (x0 >= x1 || y0 >= y1) return;

    const float hard = std::clamp(dab.hardness, 0.0f, 1.0f);
    const float inner = r * hard;
    const float band = std::max(1e-4f, r - inner);
    const uint32_t src = color.packed();
    const float opacity = std::clamp(dab.opacity, 0.0f, 1.0f);

    uint32_t* p = img.pixels();
    const int w = img.width();

    for (int y = y0; y < y1; ++y) {
        uint32_t* row = p + (size_t)(y - oy) * (size_t)w + (size_t)(x0 - ox);
        const float dy = (float)y + 0.5f - cy;
        for (int x = x0; x < x1; ++x) {
            const float dx = (float)x + 0.5f - cx;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist >= r) continue;

            float cov;
            if (dist <= inner) {
                cov = 1.0f;
            } else {
                const float t = (r - dist) / band;
                cov = t * t * (3.0f - 2.0f * t);
            }
            float a = cov * opacity;
            if (mask) a *= (float)mask[(size_t)y * (size_t)mw + (size_t)x] / 255.0f;
            if (a <= 0.002f) continue;

            if (erase) {
                if (lockAlpha) continue;
                row[x - x0] = blend::erasePixel(row[x - x0], a);
            } else {
                row[x - x0] = blend::paintPixel(row[x - x0], src, a, lockAlpha);
            }
        }
    }

    dirty = dirty.unite(Rect::fromLTRB(x0, y0, x1, y1));
}

inline void putPixel(Image& img, int ox, int oy, const uint8_t* mask, int mw, int mh, Rect& dirty, int x, int y,
                     uint32_t src, bool lockAlpha)
{
    if (img.empty()) return;
    const int ix = x - ox, iy = y - oy;
    if (ix < 0 || iy < 0 || ix >= img.width() || iy >= img.height()) return;
    float a = 1.0f;
    if (mask) {
        if (x < 0 || y < 0 || x >= mw || y >= mh) return;
        a = (float)mask[(size_t)y * (size_t)mw + (size_t)x] / 255.0f;
        if (a <= 0.002f) return;
    }
    uint32_t* p = img.pixels();
    const size_t i = (size_t)iy * (size_t)img.width() + (size_t)ix;
    p[i] = blend::paintPixel(p[i], src, a, lockAlpha);
    dirty = dirty.unite(Rect::fromLTRB(x, y, x + 1, y + 1));
}

inline void hairline(Image& img, int ox, int oy, const uint8_t* mask, int mw, int mh, Rect& dirty, float fx0, float fy0,
                     float fx1, float fy1, uint32_t src, bool lockAlpha)
{
    int x0 = (int)std::floor(fx0), y0 = (int)std::floor(fy0);
    int x1 = (int)std::floor(fx1), y1 = (int)std::floor(fy1);
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        putPixel(img, ox, oy, mask, mw, mh, dirty, x0, y0, src, lockAlpha);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

inline void strokeDab(Image& img, int ox, int oy, const uint8_t* mask, int mw, int mh, Rect& dirty, float x0, float y0,
                      float x1, float y1, float spacing, const Dab& dab, Color color, bool erase, bool lockAlpha = false)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) {
        stampDab(img, ox, oy, mask, mw, mh, dirty, x1, y1, dab, color, erase, lockAlpha);
        return;
    }
    const float step = std::max(0.4f, spacing);
    const int n = std::max(1, (int)(len / step));
    for (int i = 1; i <= n; ++i) {
        const float t = (float)i / (float)n;
        stampDab(img, ox, oy, mask, mw, mh, dirty, x0 + dx * t, y0 + dy * t, dab, color, erase, lockAlpha);
    }
}

}
