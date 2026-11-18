#include "core/Blend.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#define HWY_COMPILE_ALL_ATTAINABLE 1

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "core/Blend.cpp"
#include "hwy/foreach_target.h"
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace ie {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

HWY_ATTR void OverImpl(uint32_t* HWY_RESTRICT dst, const uint32_t* HWY_RESTRICT src, size_t count, float opacity)
{
    const hn::ScalableTag<uint32_t> du;
    const hn::ScalableTag<float> df;
    const size_t N = hn::Lanes(du);

    const auto zero = hn::Zero(df);
    const auto one = hn::Set(df, 1.0f);
    const auto c255 = hn::Set(df, 255.0f);
    const auto inv255 = hn::Set(df, 1.0f / 255.0f);
    const auto half = hn::Set(df, 0.5f);
    const auto ff = hn::Set(du, 0xFFu);
    const auto op = hn::Set(df, opacity);

    size_t i = 0;
    for (; i + N <= count; i += N) {
        const auto dv = hn::LoadU(du, dst + i);
        const auto sv = hn::LoadU(du, src + i);

        const auto dr = hn::ConvertTo(df, hn::And(dv, ff));
        const auto dg = hn::ConvertTo(df, hn::And(hn::ShiftRight<8>(dv), ff));
        const auto db = hn::ConvertTo(df, hn::And(hn::ShiftRight<16>(dv), ff));
        const auto da8 = hn::ConvertTo(df, hn::ShiftRight<24>(dv));

        const auto sr = hn::ConvertTo(df, hn::And(sv, ff));
        const auto sg = hn::ConvertTo(df, hn::And(hn::ShiftRight<8>(sv), ff));
        const auto sb = hn::ConvertTo(df, hn::And(hn::ShiftRight<16>(sv), ff));
        const auto sa8 = hn::ConvertTo(df, hn::ShiftRight<24>(sv));

        const auto da = hn::Mul(da8, inv255);
        const auto sa = hn::Mul(hn::Mul(sa8, inv255), op);
        const auto inv = hn::Sub(one, sa);
        const auto outA = hn::Add(sa, hn::Mul(da, inv));

        const auto srcW = sa;
        const auto dstW = hn::Mul(da, inv);
        const auto numR = hn::Add(hn::Mul(sr, srcW), hn::Mul(dr, dstW));
        const auto numG = hn::Add(hn::Mul(sg, srcW), hn::Mul(dg, dstW));
        const auto numB = hn::Add(hn::Mul(sb, srcW), hn::Mul(db, dstW));

        const auto rcp = hn::ApproximateReciprocal(outA);
        const auto invA = hn::Mul(rcp, hn::Sub(hn::Add(one, one), hn::Mul(outA, rcp)));
        const auto r = hn::Add(hn::Mul(numR, invA), half);
        const auto g = hn::Add(hn::Mul(numG, invA), half);
        const auto b = hn::Add(hn::Mul(numB, invA), half);
        const auto a = hn::Add(hn::Mul(outA, c255), half);

        const auto rCl = hn::Min(hn::Max(r, zero), c255);
        const auto gCl = hn::Min(hn::Max(g, zero), c255);
        const auto bCl = hn::Min(hn::Max(b, zero), c255);
        const auto aCl = hn::Min(hn::Max(a, zero), c255);

        const auto ru = hn::ConvertTo(du, rCl);
        const auto gu = hn::ShiftLeft<8>(hn::ConvertTo(du, gCl));
        const auto bu = hn::ShiftLeft<16>(hn::ConvertTo(du, bCl));
        const auto au = hn::ShiftLeft<24>(hn::ConvertTo(du, aCl));

        const auto res = hn::Or(hn::Or(ru, gu), hn::Or(bu, au));
        const auto keep = hn::Eq(hn::ConvertTo(du, aCl), hn::Zero(du));
        hn::StoreU(hn::IfThenElse(keep, dv, res), du, dst + i);
    }

    for (; i < count; ++i) {
        const uint32_t s = src[i];
        const float sa = (float)((s >> 24) & 0xff) / 255.0f * opacity;
        if (sa <= 0.0f) continue;
        const uint32_t d = dst[i];
        const float da = (float)((d >> 24) & 0xff) / 255.0f;
        const float inv = 1.0f - sa;
        const float oa = sa + da * inv;
        if (oa <= 0.0f) continue;
        auto ch = [&](int sh) {
            const float sc = (float)((s >> sh) & 0xff);
            const float dc = (float)((d >> sh) & 0xff);
            return (uint32_t)std::lround((sc * sa + dc * da * inv) / oa);
        };
        const uint32_t r = std::min<uint32_t>(255u, ch(0));
        const uint32_t g = std::min<uint32_t>(255u, ch(8));
        const uint32_t b = std::min<uint32_t>(255u, ch(16));
        const uint32_t a = (uint32_t)std::lround(oa * 255.0f);
        dst[i] = r | (g << 8) | (b << 16) | (std::min<uint32_t>(255u, a) << 24);
    }
}

HWY_ATTR int OverTargetId() { return HWY_TARGET; }

HWY_ATTR bool AllOpaqueImpl(const uint32_t* HWY_RESTRICT px, size_t count)
{
    const hn::ScalableTag<uint32_t> du;
    const size_t N = hn::Lanes(du);
    const auto amask = hn::Set(du, 0xFF000000u);
    auto acc = amask;

    size_t i = 0;
    for (; i + N <= count; i += N)
        acc = hn::Min(acc, hn::And(hn::LoadU(du, px + i), amask));

    uint32_t minA = 0xFF000000u;
    uint32_t lanes[256];
    hn::StoreU(acc, du, lanes);
    for (size_t k = 0; k < N && k < 256; ++k) minA = std::min(minA, lanes[k]);
    for (; i < count; ++i) minA = std::min<uint32_t>(minA, px[i] & 0xFF000000u);
    return minA == 0xFF000000u;
}

}
}
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace ie {
HWY_EXPORT(OverImpl);
HWY_EXPORT(OverTargetId);
HWY_EXPORT(AllOpaqueImpl);

namespace {

inline uint8_t channelOf(uint32_t v, int sh) { return (uint8_t)((v >> sh) & 0xff); }
inline uint32_t packCh(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    return (r & 0xff) | ((g & 0xff) << 8) | ((b & 0xff) << 16) | ((a & 0xff) << 24);
}

inline uint8_t clamp8(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (uint8_t)(v + 0.5f);
}

}

namespace blend {

const char* activeSimdTarget()
{
    static const std::string name = hwy::TargetName(HWY_DYNAMIC_DISPATCH(OverTargetId)());
    return name.c_str();
}

void over(uint32_t* dst, const uint32_t* src, size_t count, float opacity)
{
    if (!dst || !src || count == 0 || opacity <= 0.0f) return;
    HWY_DYNAMIC_DISPATCH(OverImpl)(dst, src, count, opacity);
}

bool allOpaque(const uint32_t* px, size_t count)
{
    if (!px || count == 0) return false;
    return HWY_DYNAMIC_DISPATCH(AllOpaqueImpl)(px, count);
}

void overMasked(uint32_t* dst, const uint32_t* src, const uint8_t* mask, size_t count, float opacity)
{
    if (!dst || !src || count == 0) return;
    if (!mask) {
        over(dst, src, count, opacity);
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!mask[i]) continue;
        dst[i] = overPixel(dst[i], src[i], opacity * (float)mask[i] / 255.0f);
    }
}

void brightnessContrast(Image& img, Rect roi, float brightness, float contrast)
{
    roi = roi.clampTo(img.width(), img.height());
    if (roi.empty() || img.empty()) return;

    uint8_t lut[256];
    const float b = brightness * 2.55f;
    const float c = (contrast + 100.0f) / 100.0f;
    for (int i = 0; i < 256; ++i) {
        float v = (float)i + b;
        v = (v - 128.0f) * c + 128.0f;
        lut[i] = clamp8(v);
    }

    uint32_t* p = img.pixels();
    for (int y = roi.y; y < roi.bottom(); ++y) {
        uint32_t* row = p + (size_t)y * (size_t)img.width();
        for (int x = roi.x; x < roi.right(); ++x) {
            const uint32_t v = row[x];
            row[x] = packCh(lut[v & 0xff], lut[(v >> 8) & 0xff], lut[(v >> 16) & 0xff], (v >> 24) & 0xff);
        }
    }
}

void invert(Image& img, Rect roi)
{
    roi = roi.clampTo(img.width(), img.height());
    if (roi.empty()) return;
    uint32_t* p = img.pixels();
    for (int y = roi.y; y < roi.bottom(); ++y) {
        uint32_t* row = p + (size_t)y * (size_t)img.width();
        for (int x = roi.x; x < roi.right(); ++x) {
            const uint32_t v = row[x];
            row[x] = packCh(255 - (v & 0xff), 255 - ((v >> 8) & 0xff), 255 - ((v >> 16) & 0xff), (v >> 24) & 0xff);
        }
    }
}

void desaturate(Image& img, Rect roi)
{
    roi = roi.clampTo(img.width(), img.height());
    if (roi.empty()) return;
    uint32_t* p = img.pixels();
    for (int y = roi.y; y < roi.bottom(); ++y) {
        uint32_t* row = p + (size_t)y * (size_t)img.width();
        for (int x = roi.x; x < roi.right(); ++x) {
            const uint32_t v = row[x];
            const int r = v & 0xff, g = (v >> 8) & 0xff, b = (v >> 16) & 0xff;
            const int y8 = (r * 299 + g * 587 + b * 114) / 1000;
            row[x] = packCh((uint32_t)y8, (uint32_t)y8, (uint32_t)y8, (v >> 24) & 0xff);
        }
    }
}

void threshold(Image& img, Rect roi, int level)
{
    roi = roi.clampTo(img.width(), img.height());
    if (roi.empty()) return;
    const int lv = level <= 0 ? 0 : (level >= 255 ? 255 : level);
    uint32_t* p = img.pixels();
    for (int y = roi.y; y < roi.bottom(); ++y) {
        uint32_t* row = p + (size_t)y * (size_t)img.width();
        for (int x = roi.x; x < roi.right(); ++x) {
            const uint32_t v = row[x];
            const int r = v & 0xff, g = (v >> 8) & 0xff, b = (v >> 16) & 0xff;
            const int y8 = (r * 299 + g * 587 + b * 114) / 1000;
            const uint32_t o = y8 >= lv ? 255u : 0u;
            row[x] = packCh(o, o, o, (v >> 24) & 0xff);
        }
    }
}

void hueShift(Image& img, Rect roi, int degrees)
{
    roi = roi.clampTo(img.width(), img.height());
    if (roi.empty() || degrees == 0) return;
    int deg = degrees % 360;
    if (deg < 0) deg += 360;
    const float add = (float)deg;
    uint32_t* p = img.pixels();
    for (int y = roi.y; y < roi.bottom(); ++y) {
        uint32_t* row = p + (size_t)y * (size_t)img.width();
        for (int x = roi.x; x < roi.right(); ++x) {
            const uint32_t v = row[x];
            const float r = (float)(v & 0xff) / 255.0f;
            const float g = (float)((v >> 8) & 0xff) / 255.0f;
            const float b = (float)((v >> 16) & 0xff) / 255.0f;
            const float mx = std::max(r, std::max(g, b));
            const float mn = std::min(r, std::min(g, b));
            const float d = mx - mn;
            float h = 0.0f;
            if (d > 0.0f) {
                if (mx == r) h = 60.0f * std::fmod((g - b) / d, 6.0f);
                else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
                else h = 60.0f * ((r - g) / d + 4.0f);
            }
            if (h < 0.0f) h += 360.0f;
            const float s = mx <= 0.0f ? 0.0f : d / mx;
            h = std::fmod(h + add, 360.0f);
            if (h < 0.0f) h += 360.0f;
            const float c = mx * s;
            const float xx = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
            const float m = mx - c;
            float rr = 0.0f, gg = 0.0f, bb = 0.0f;
            if (h < 60.0f) { rr = c; gg = xx; }
            else if (h < 120.0f) { rr = xx; gg = c; }
            else if (h < 180.0f) { gg = c; bb = xx; }
            else if (h < 240.0f) { gg = xx; bb = c; }
            else if (h < 300.0f) { rr = xx; bb = c; }
            else { rr = c; bb = xx; }
            row[x] = packCh(clamp8((rr + m) * 255.0f), clamp8((gg + m) * 255.0f), clamp8((bb + m) * 255.0f),
                            (v >> 24) & 0xff);
        }
    }
}

namespace {

inline uint32_t premul(uint32_t v)
{
    const uint32_t a = (v >> 24) & 0xff;
    const uint32_t r = ((v & 0xff) * a + 127) / 255;
    const uint32_t g = (((v >> 8) & 0xff) * a + 127) / 255;
    const uint32_t b = (((v >> 16) & 0xff) * a + 127) / 255;
    return r | (g << 8) | (b << 16) | (a << 24);
}

inline uint32_t unpremul(uint32_t v)
{
    const uint32_t a = (v >> 24) & 0xff;
    if (a == 0) return 0;
    const uint32_t r = std::min<uint32_t>(255, ((v & 0xff) * 255 + a / 2) / a);
    const uint32_t g = std::min<uint32_t>(255, (((v >> 8) & 0xff) * 255 + a / 2) / a);
    const uint32_t b = std::min<uint32_t>(255, (((v >> 16) & 0xff) * 255 + a / 2) / a);
    return r | (g << 8) | (b << 16) | (a << 24);
}

void boxPass(std::vector<uint32_t>& src, std::vector<uint32_t>& dst, int w, int h, int r, bool horiz)
{
    const int len = horiz ? w : h;
    const int lines = horiz ? h : w;
    const int denom = 2 * r + 1;

    std::vector<uint32_t> acc((size_t)len);
    for (int line = 0; line < lines; ++line) {
        auto at = [&](int i) -> uint32_t {
            if (i < 0) i = 0;
            if (i >= len) i = len - 1;
            return horiz ? src[(size_t)line * w + (size_t)i] : src[(size_t)i * w + (size_t)line];
        };
        uint32_t sr = 0, sg = 0, sb = 0, sa = 0;
        for (int i = -r; i <= r; ++i) {
            const uint32_t v = at(i);
            sr += v & 0xff;
            sg += (v >> 8) & 0xff;
            sb += (v >> 16) & 0xff;
            sa += (v >> 24) & 0xff;
        }
        for (int i = 0; i < len; ++i) {
            const uint32_t out = ((sr / denom) & 0xff) | (((sg / denom) & 0xff) << 8) | (((sb / denom) & 0xff) << 16) |
                                 (((sa / denom) & 0xff) << 24);
            if (horiz)
                dst[(size_t)line * w + (size_t)i] = out;
            else
                dst[(size_t)i * w + (size_t)line] = out;

            const uint32_t add = at(i + r + 1);
            const uint32_t sub = at(i - r);
            sr += (add & 0xff) - (sub & 0xff);
            sg += ((add >> 8) & 0xff) - ((sub >> 8) & 0xff);
            sb += ((add >> 16) & 0xff) - ((sub >> 16) & 0xff);
            sa += ((add >> 24) & 0xff) - ((sub >> 24) & 0xff);
        }
    }
}

}

void blur(Image& img, Rect roi, int radius)
{
    roi = roi.clampTo(img.width(), img.height());
    if (roi.empty() || radius <= 0) return;

    const int w = roi.w, h = roi.h;
    std::vector<uint32_t> a((size_t)w * h), b((size_t)w * h);
    const uint32_t* base = img.pixels();
    for (int y = 0; y < h; ++y) {
        const uint32_t* row = base + (size_t)(roi.y + y) * (size_t)img.width() + (size_t)roi.x;
        for (int x = 0; x < w; ++x) a[(size_t)y * w + x] = premul(row[x]);
    }

    const int passes = 3;
    const int radii[3] = {radius, radius, radius};
    for (int p = 0; p < passes; ++p) {
        boxPass(a, b, w, h, radii[p], true);
        boxPass(b, a, w, h, radii[p], false);
    }

    uint32_t* out = img.pixels();
    for (int y = 0; y < h; ++y) {
        uint32_t* row = out + (size_t)(roi.y + y) * (size_t)img.width() + (size_t)roi.x;
        for (int x = 0; x < w; ++x) row[x] = unpremul(a[(size_t)y * w + x]);
    }
}

void fillMasked(Image& img, int ox, int oy, const uint8_t* mask, int mw, int mh, Color c)
{
    if (img.empty()) return;
    const int w = img.width(), h = img.height();
    uint32_t* p = img.pixels();
    const uint32_t src = c.packed();
    if (!mask) {
        for (size_t i = 0, n = (size_t)w * h; i < n; ++i) p[i] = overPixel(p[i], src, 1.0f);
        return;
    }
    for (int y = 0; y < h; ++y) {
        const int cy = y + oy;
        if (cy < 0 || cy >= mh) continue;
        for (int x = 0; x < w; ++x) {
            const int cx = x + ox;
            if (cx < 0 || cx >= mw) continue;
            const uint8_t m = mask[(size_t)cy * mw + cx];
            if (!m) continue;
            p[(size_t)y * w + x] = overPixel(p[(size_t)y * w + x], src, (float)m / 255.0f);
        }
    }
}

void strokeMask(Image& img, int ox, int oy, const uint8_t* mask, int mw, int mh, int strokeWidth, Color c)
{
    if (!mask || mw <= 0 || mh <= 0 || strokeWidth <= 0) return;
    const int w = mw, h = mh;

    const float INF = 1e9f;
    const float D1 = 1.0f, D2 = 1.41421356f;
    std::vector<float> din((size_t)w * h), dout((size_t)w * h);

    for (size_t i = 0, n = (size_t)w * h; i < n; ++i) {
        const bool in = mask[i] != 0;
        din[i] = in ? INF : 0.0f;
        dout[i] = in ? 0.0f : INF;
    }

    auto pass = [&](std::vector<float>& d) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t i = (size_t)y * w + x;
                float v = d[i];
                if (x > 0) v = std::min(v, d[i - 1] + D1);
                if (y > 0) {
                    v = std::min(v, d[i - w] + D1);
                    if (x > 0) v = std::min(v, d[i - w - 1] + D2);
                    if (x + 1 < w) v = std::min(v, d[i - w + 1] + D2);
                }
                d[i] = v;
            }
        }
        for (int y = h - 1; y >= 0; --y) {
            for (int x = w - 1; x >= 0; --x) {
                const size_t i = (size_t)y * w + x;
                float v = d[i];
                if (x + 1 < w) v = std::min(v, d[i + 1] + D1);
                if (y + 1 < h) {
                    v = std::min(v, d[i + w] + D1);
                    if (x + 1 < w) v = std::min(v, d[i + w + 1] + D2);
                    if (x > 0) v = std::min(v, d[i + w - 1] + D2);
                }
                d[i] = v;
            }
        }
    };
    pass(din);
    pass(dout);

    const float half = strokeWidth * 0.5f;
    uint32_t* p = img.pixels();
    const int iw = img.width(), ih = img.height();
    const uint32_t src = c.packed();
    for (int y = 0; y < h; ++y) {
        const int ly = y - oy;
        if (ly < 0 || ly >= ih) continue;
        for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)y * w + x;
            const bool in = mask[i] != 0;
            const float dist = in ? din[i] : dout[i];
            if (dist > half) continue;
            const int lx = x - ox;
            if (lx < 0 || lx >= iw) continue;
            const float cov = std::min(1.0f, half - dist + 0.5f);
            const size_t di = (size_t)ly * (size_t)iw + (size_t)lx;
            p[di] = overPixel(p[di], src, cov);
        }
    }
}

}
}
#endif
