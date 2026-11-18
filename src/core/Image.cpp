#include "core/Image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ie {

void Image::fillRect(Rect r, uint32_t packed)
{
    if (empty()) return;
    r = r.clampTo(w_, h_);
    if (r.empty()) return;

    uint32_t* p = pixels();
    for (int y = r.y; y < r.bottom(); ++y) {
        uint32_t* row = p + (size_t)y * (size_t)w_ + (size_t)r.x;
        for (int x = 0; x < r.w; ++x) row[x] = packed;
    }
}

Rect Image::contentBounds() const
{
    if (empty()) return Rect{};
    const uint32_t* p = pixels();
    int x0 = w_, x1 = -1, y0 = h_, y1 = -1;
    for (int y = 0; y < h_; ++y) {
        const uint32_t* row = p + (size_t)y * (size_t)w_;
        int a = -1;
        for (int x = 0; x < w_; ++x) {
            if ((row[x] >> 24) != 0) { a = x; break; }
        }
        if (a < 0) continue;
        int b = a;
        for (int x = w_ - 1; x > a; --x) {
            if ((row[x] >> 24) != 0) { b = x; break; }
        }
        if (y0 > y) y0 = y;
        y1 = y;
        if (a < x0) x0 = a;
        if (b > x1) x1 = b;
    }
    if (x1 < x0 || y1 < y0) return Rect{};
    return Rect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

void Image::blit(const Image& o, int dx, int dy)
{
    if (empty() || o.empty()) return;
    const Rect dst = Rect{dx, dy, o.width(), o.height()}.clampTo(w_, h_);
    if (dst.empty()) return;

    uint32_t* d = pixels();
    const uint32_t* s = o.pixels();
    for (int y = dst.y; y < dst.bottom(); ++y) {
        const int sy = y - dy;
        std::memcpy(d + (size_t)y * (size_t)w_ + (size_t)dst.x,
                    s + (size_t)sy * (size_t)o.width() + (size_t)(dst.x - dx),
                    (size_t)dst.w * 4);
    }
}

}

namespace ie {

Image cropRegion(const Image& src, Rect r)
{
    r = r.clampTo(src.width(), src.height());
    Image out(std::max(1, r.w), std::max(1, r.h));
    if (r.empty() || src.empty()) return out;

    uint8_t* d = out.data();
    const uint8_t* s = src.data();
    for (int y = 0; y < r.h; ++y) {
        std::memcpy(d + (size_t)y * r.w * 4, s + (size_t)(r.y + y) * src.width() * 4 + (size_t)r.x * 4, (size_t)r.w * 4);
    }
    return out;
}

Image scaleImage(const Image& src, int nw, int nh)
{
    nw = std::max(1, nw);
    nh = std::max(1, nh);
    Image out(nw, nh);
    const int sw = src.width(), sh = src.height();
    if (src.empty()) return out;

    const uint32_t* s = src.pixels();
    uint32_t* d = out.pixels();

    const float kx = (float)sw / (float)nw, ky = (float)sh / (float)nh;
    for (int y = 0; y < nh; ++y) {
        float fy = ((float)y + 0.5f) * ky - 0.5f;
        fy = std::max(0.0f, std::min((float)sh - 1.0f, fy));
        const int y0 = (int)fy, y1 = std::min(sh - 1, y0 + 1);
        const float ty = fy - (float)y0;

        for (int x = 0; x < nw; ++x) {
            float fx = ((float)x + 0.5f) * kx - 0.5f;
            fx = std::max(0.0f, std::min((float)sw - 1.0f, fx));
            const int x0 = (int)fx, x1 = std::min(sw - 1, x0 + 1);
            const float tx = fx - (float)x0;

            float a = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
            const float ws[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
            const int xs[4] = {x0, x1, x0, x1};
            const int ys[4] = {y0, y0, y1, y1};
            for (int k = 0; k < 4; ++k) {
                const uint32_t v = s[(size_t)ys[k] * (size_t)sw + (size_t)xs[k]];
                const float al = (float)(v >> 24) * ws[k];
                a += al;
                r += (float)(v & 0xff) * al;
                g += (float)((v >> 8) & 0xff) * al;
                b += (float)((v >> 16) & 0xff) * al;
            }
            uint32_t pa = (uint32_t)std::lround(std::min(255.0f, a));
            uint32_t pr = 0, pg = 0, pb = 0;
            if (a > 0.1f) {
                pr = (uint32_t)std::lround(std::min(255.0f, r / a));
                pg = (uint32_t)std::lround(std::min(255.0f, g / a));
                pb = (uint32_t)std::lround(std::min(255.0f, b / a));
            }
            d[(size_t)y * (size_t)nw + (size_t)x] = (pa << 24) | (pb << 16) | (pg << 8) | pr;
        }
    }
    return out;
}

void copyRegion(Image& dst, const Image& src, Rect r)
{
    r = r.clampTo(std::min(dst.width(), src.width()), std::min(dst.height(), src.height()));
    if (r.empty()) return;
    uint8_t* d = dst.data();
    const uint8_t* s = src.data();
    for (int y = r.y; y < r.bottom(); ++y) {
        std::memcpy(d + (size_t)y * dst.width() * 4 + (size_t)r.x * 4, s + (size_t)y * src.width() * 4 + (size_t)r.x * 4,
                    (size_t)r.w * 4);
    }
}

}

