#include "core/ShadowLift.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ie {
namespace shadow {

namespace {

inline int clampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Box {
    int w = 0, h = 0;
    std::vector<double> sat;
    void build(const std::vector<float>& src, int width, int height)
    {
        w = width;
        h = height;
        sat.assign((size_t)(w + 1) * (size_t)(h + 1), 0.0);
        for (int y = 0; y < h; ++y) {
            double acc = 0.0;
            for (int x = 0; x < w; ++x) {
                acc += (double)src[(size_t)y * (size_t)w + (size_t)x];
                sat[(size_t)(y + 1) * (size_t)(w + 1) + (size_t)(x + 1)] =
                    acc + sat[(size_t)y * (size_t)(w + 1) + (size_t)(x + 1)];
            }
        }
    }
    double mean(int x, int y, int r) const
    {
        const int x0 = clampI(x - r, 0, w - 1), x1 = clampI(x + r, 0, w - 1);
        const int y0 = clampI(y - r, 0, h - 1), y1 = clampI(y + r, 0, h - 1);
        const double sum = sat[(size_t)(y1 + 1) * (size_t)(w + 1) + (size_t)(x1 + 1)] -
                           sat[(size_t)y0 * (size_t)(w + 1) + (size_t)(x1 + 1)] -
                           sat[(size_t)(y1 + 1) * (size_t)(w + 1) + (size_t)x0] +
                           sat[(size_t)y0 * (size_t)(w + 1) + (size_t)x0];
        const int cnt = (x1 - x0 + 1) * (y1 - y0 + 1);
        return cnt > 0 ? sum / (double)cnt : 0.0;
    }
};

inline void lumaSat(uint32_t v, float& y, float& sat)
{
    const float b = (float)(v & 0xffu), g = (float)((v >> 8) & 0xffu), r = (float)((v >> 16) & 0xffu);
    y = 0.299f * r + 0.587f * g + 0.114f * b;
    const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
    sat = mx > 1.0f ? (mx - mn) / mx : 0.0f;
}

}

void removeShadow(Image& img, Rect roi, const Params& p)
{
    roi = roi.clampTo(img.width(), img.height());
    if (roi.empty() || img.empty() || !img.hasPixels()) return;
    const float s = clamp01(p.strength);
    if (s <= 0.0f || p.radius <= 0.5f) return;
    if (roi.w < 3 || roi.h < 3) return;

    const int W = img.width();
    uint32_t* px = img.pixels();
    constexpr int kStep = 2;
    const int sw = (roi.w + kStep - 1) / kStep;
    const int sh = (roi.h + kStep - 1) / kStep;
    const size_t n = (size_t)sw * (size_t)sh;

    std::vector<float> Y(n, 0.0f), S(n, 0.0f);
    for (int j = 0; j < sh; ++j) {
        const int sy = roi.y + std::min(roi.h - 1, j * kStep);
        const uint32_t* row = px + (size_t)sy * (size_t)W;
        for (int i = 0; i < sw; ++i) {
            const int sx = roi.x + std::min(roi.w - 1, i * kStep);
            lumaSat(row[sx], Y[(size_t)j * (size_t)sw + (size_t)i], S[(size_t)j * (size_t)sw + (size_t)i]);
        }
    }

    double hist[256] = {0};
    for (float v : Y) hist[clampI((int)std::lround(v), 0, 255)] += 1.0;
    const double total = (double)Y.size();
    float ref = 1.0f, p5 = 255.0f;
    {
        double acc = 0.0;
        for (int i = 0; i < 256; ++i) {
            acc += hist[i];
            if (acc >= total * 0.95) { ref = std::max(1.0f, (float)i); break; }
        }
        acc = 0.0;
        for (int i = 0; i < 256; ++i) {
            acc += hist[i];
            if (acc >= total * 0.05) { p5 = (float)i; break; }
        }
    }
    if (ref - p5 < 6.0f) return;

    const int rb1 = std::max(1, (int)std::lround((double)p.radius * 0.25));
    const int rb2 = std::max(1, (int)std::lround((double)p.radius * 0.5));
    const int rb3 = std::max(1, (int)std::lround((double)p.radius));
    const int rs[3] = {rb1, rb2, rb3};
    std::vector<float> blurs[3];
    {
        Box box;
        box.build(Y, sw, sh);
        for (int k = 0; k < 3; ++k) {
            blurs[k].assign(n, 0.0f);
            for (int j = 0; j < sh; ++j)
                for (int i = 0; i < sw; ++i)
                    blurs[k][(size_t)j * (size_t)sw + (size_t)i] = (float)box.mean(i, j, rs[k]);
        }
    }

    const float kThr = clampf(p.sensitivity, 0.2f, 0.9f);
    std::vector<float> mask(n, 0.0f);
    for (size_t i = 0; i < n; ++i)
        mask[i] = (Y[i] < ref * kThr && S[i] < 0.5f) ? 1.0f : 0.0f;
    {
        const int rSoft = p.softness > 0.5f ? std::max(1, (int)std::lround((double)p.softness * 0.5))
                                            : std::max(1, rb2);
        Box box;
        box.build(mask, sw, sh);
        std::vector<float> soft(n, 0.0f);
        for (int j = 0; j < sh; ++j)
            for (int i = 0; i < sw; ++i)
                soft[(size_t)j * (size_t)sw + (size_t)i] = (float)box.mean(i, j, rSoft);
        mask.swap(soft);
    }

    const float gCap = clampf(p.maxGain, 1.5f, 6.0f);
    std::vector<float> gain(n, 1.0f);
    for (size_t i = 0; i < n; ++i) {
        const float wgt = std::pow(clamp01(mask[i]), 1.5f);
        if (wgt <= 0.01f) continue;
        double acc = 1.0;
        for (int k = 0; k < 3; ++k) acc *= (double)ref / std::max(1.0, (double)blurs[k][i]);
        float g = (float)std::cbrt(acc);
        g = clampf(g, 1.0f, gCap);
        gain[i] = 1.0f + s * wgt * (g - 1.0f);
    }

    for (int y = 0; y < roi.h; ++y) {
        const float fy = (float)y / (float)kStep;
        const int y0 = clampI((int)fy, 0, sh - 1), y1 = clampI(y0 + 1, 0, sh - 1);
        const float ty = fy - (float)y0;
        uint32_t* row = px + (size_t)(roi.y + y) * (size_t)W + (size_t)roi.x;
        for (int x = 0; x < roi.w; ++x) {
            const float fx = (float)x / (float)kStep;
            const int x0 = clampI((int)fx, 0, sw - 1), x1 = clampI(x0 + 1, 0, sw - 1);
            const float tx = fx - (float)x0;
            const auto at = [&](int i, int j) { return gain[(size_t)j * (size_t)sw + (size_t)i]; };
            const float k = (1.0f - ty) * ((1.0f - tx) * at(x0, y0) + tx * at(x1, y0)) +
                            ty * ((1.0f - tx) * at(x0, y1) + tx * at(x1, y1));
            if (k <= 1.0001f) continue;

            const uint32_t v = row[x];
            const uint32_t a = v & 0xFF000000u;
            const auto ch = [&](int shift) {
                float c = (float)((v >> shift) & 0xffu) * k;
                return (uint32_t)(c > 255.0f ? 255.0f : c);
            };
            row[x] = a | ch(0) | (ch(8) << 8) | (ch(16) << 16);
        }
    }
}

}
}
