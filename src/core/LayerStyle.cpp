#include "core/LayerStyle.h"

#include "core/Blend.h"
#include "core/Primaries.h"
#include "core/ShadowLift.h"
#include "core/TextRender.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkImageFilters.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ie {

static long g_textStyleHits = 0;
long textStyleHits() { return g_textStyleHits; }
namespace {

inline uint32_t packColor(const float c[4], float alpha)
{
    auto q = [](float v) { return (uint32_t)std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f); };
    return q(c[0]) | (q(c[1]) << 8) | (q(c[2]) << 16) | (q(alpha) << 24);
}

inline uint32_t scale255(uint32_t k) { return (255u << 8) / std::max(1u, k); }

const uint16_t* recip255()
{
    static const std::vector<uint16_t> tab = [] {
        std::vector<uint16_t> v(256);
        for (int k = 0; k < 256; ++k) v[(size_t)k] = (uint16_t)scale255((uint32_t)k);
        return v;
    }();
    return tab.data();
}

inline uint32_t over8(uint32_t d, uint32_t s)
{
    const uint32_t sa = s >> 24;
    if (!sa) return d;
    if (sa == 255) return s;
    const uint32_t da = d >> 24;
    if (!da) return s;
    const uint32_t inv = 255u - sa;
    const uint32_t oa = sa + (da * inv + 127u) / 255u;
    const uint32_t mul = recip255()[oa];
    auto ch = [&](int sh) {
        const uint32_t sc = (s >> sh) & 0xffu;
        const uint32_t dc = (d >> sh) & 0xffu;
        const uint32_t pre = (sc * sa + (dc * da * inv + 127u) / 255u) * mul;
        uint32_t v = (pre + 128u) >> 8;
        return v > 255u ? 255u : v;
    };
    return ch(0) | (ch(8) << 8) | (ch(16) << 16) | (oa << 24);
}

inline void shadowShift(const LayerStyle& s, int& sdx, int& sdy)
{
    sdx = 0;
    sdy = 0;
    if (!s.shadowOn) return;
    const float rad = s.shadowAngle * 3.14159265f / 180.0f;
    sdx = (int)std::lround(std::cos(rad) * s.shadowDist);
    sdy = (int)std::lround(-std::sin(rad) * s.shadowDist);
}

inline uint32_t colorKey(const float c[4])
{
    return packColor(c, 1.0f);
}

StyleGridKey gridKeyOf(const Layer& l)
{
    const int pad = stylePad(l.style);
    StyleGridKey k;
    k.pixRev = l.pixRev;
    if (!l.textXf.isIdentity()) {
        unsigned h = 2166136261u;
        const float v[6] = {l.textXf.getScaleX(), l.textXf.getSkewY(), l.textXf.getSkewX(),
                            l.textXf.getScaleY(), l.textXf.getTranslateX(), l.textXf.getTranslateY()};
        for (int i = 0; i < 6; ++i) {
            unsigned bits = (unsigned)SkFloat2Bits(v[i]);
            h = (h ^ bits) * 16777619u;
        }
        k.xfHash = h ? h : 1u;
    }
    k.pad = pad;
    int gx = l.x - pad, gy = l.y - pad;
    int gw = l.img.width() + pad * 2, gh = l.img.height() + pad * 2;
    if (gw > 0 && gh > 0 && l.styleClip.w > 0 && l.styleClip.h > 0) {
        const Rect is{l.styleClip.x, l.styleClip.y, l.styleClip.w, l.styleClip.h};
        const Rect r = Rect{gx, gy, gw, gh}.intersect(is);
        if (r.empty()) {
            gw = gh = 0;
            gx = gy = 0;
        } else {
            gx = r.x;
            gy = r.y;
            gw = r.w;
            gh = r.h;
        }
    }
    k.ox = gx;
    k.oy = gy;
    k.w = gw;
    k.h = gh;
    return k;
}

int strokeRadiusOf(const LayerStyle& s)
{
    return std::clamp((int)std::lround(s.strokeWidth), 1, 64);
}

double shadowBlurOf(const LayerStyle& s)
{
    return std::max(0.6, (double)s.shadowSize * 0.5);
}

StyleStrokeKey strokeKeyOf(const Layer& l)
{
    const LayerStyle& s = l.style;
    StyleStrokeKey k;
    k.g = gridKeyOf(l);
    k.radius = strokeRadiusOf(s);
    k.pos = s.strokePos;
    return k;
}

StyleShadowKey shadowKeyOf(const Layer& l)
{
    const LayerStyle& s = l.style;
    StyleShadowKey k;
    k.g = gridKeyOf(l);
    shadowShift(s, k.dx, k.dy);
    k.blur = shadowBlurOf(s);
    return k;
}

StyleCompositeKey compositeKeyOf(const Layer& l)
{
    const LayerStyle& s = l.style;
    StyleCompositeKey k;
    k.g = gridKeyOf(l);
    k.strokeOn = s.strokeOn;
    k.strokeRadius = strokeRadiusOf(s);
    k.strokePos = s.strokePos;
    k.strokeRGB = colorKey(s.strokeColor) & 0x00ffffffu;
    k.strokeA255 = (uint32_t)std::lround(std::clamp(s.strokeOpacity / 100.0f, 0.0f, 1.0f) * 255.0f);
    k.shadowOn = s.shadowOn;
    shadowShift(s, k.shadowDx, k.shadowDy);
    k.shadowBlur = shadowBlurOf(s);
    k.shadowRGB = colorKey(s.shadowColor) & 0x00ffffffu;
    k.shadowA255 = (uint32_t)std::lround(std::clamp(s.shadowOpacity / 100.0f, 0.0f, 1.0f) * 255.0f);
    k.overlayOn = s.overlayOn;
    k.overlayRGB = colorKey(s.overlayColor) & 0x00ffffffu;
    k.overlayT = (double)std::clamp(s.overlayOpacity / 100.0f, 0.0f, 1.0f);
    return k;
}
void buildSrcMask(const Layer& l)
{
    const Image& src = l.img;
    const StyleGridKey g = gridKeyOf(l);
    const int W = g.w, H = g.h;
    l.maskSrc.assign(W > 0 && H > 0 ? (size_t)W * (size_t)H : 0, 0);
    l.maskW = W;
    l.maskH = H;
    l.maskPad = g.pad;
    l.maskOx = g.ox;
    l.maskOy = g.oy;
    if (W <= 0 || H <= 0) {
        l.maskSrcKey = g;
        return;
    }
    const int tx = l.x - g.ox, ty = l.y - g.oy;
    for (int y = 0; y < src.height(); ++y) {
        const int dy = ty + y;
        if (dy < 0 || dy >= H) continue;
        const uint32_t* sp = src.pixels() + (size_t)y * (size_t)src.width();
        uint8_t* dp = l.maskSrc.data() + (size_t)dy * (size_t)W;
        for (int x = 0; x < src.width(); ++x) {
            const int dx = tx + x;
            if (dx < 0 || dx >= W) continue;
            dp[dx] = (uint8_t)(sp[x] >> 24);
        }
    }
    l.maskSrcKey = g;
}

void chamferDistToZero(const uint8_t* src, int W, int H, std::vector<float>& out)
{
    const float kInf = 1e9f;
    const float kDiag = 1.41421356f;
    out.assign((size_t)W * (size_t)H, kInf);
    for (size_t i = 0, n = out.size(); i < n; ++i)
        if (src[i] == 0) out[i] = 0.0f;

    for (int y = 0; y < H; ++y) {
        const float* up = y > 0 ? out.data() + (size_t)(y - 1) * W : nullptr;
        float* row = out.data() + (size_t)y * W;
        for (int x = 0; x < W; ++x) {
            float d = row[x];
            if (d == 0.0f) continue;
            if (up) {
                d = std::min(d, up[x] + 1.0f);
                if (x > 0) d = std::min(d, up[x - 1] + kDiag);
                if (x + 1 < W) d = std::min(d, up[x + 1] + kDiag);
            }
            if (x > 0) d = std::min(d, row[x - 1] + 1.0f);
            row[x] = d;
        }
    }
    for (int y = H - 1; y >= 0; --y) {
        const float* down = y + 1 < H ? out.data() + (size_t)(y + 1) * W : nullptr;
        float* row = out.data() + (size_t)y * W;
        for (int x = W - 1; x >= 0; --x) {
            float d = row[x];
            if (d == 0.0f) continue;
            if (down) {
                d = std::min(d, down[x] + 1.0f);
                if (x > 0) d = std::min(d, down[x - 1] + kDiag);
                if (x + 1 < W) d = std::min(d, down[x + 1] + kDiag);
            }
            if (x + 1 < W) d = std::min(d, row[x + 1] + 1.0f);
            row[x] = d;
        }
    }
}

inline int reflect101(int i, int n)
{
    while (i < 0 || i >= n) i = i < 0 ? -i : 2 * n - 2 - i;
    return i;
}

inline int refIdx(int i, int n)
{
    if (n <= 1) return 0;
    const int period = 2 * n - 2;
    i %= period;
    if (i < 0) i += period;
    return (i < n) ? i : period - i;
}

void boxBlurH(uint8_t* img, int W, int H, int r)
{
    if (r < 1 || W <= 0) return;
    std::vector<uint8_t> tmp((size_t)W);
    const int win = 2 * r + 1;
    for (int y = 0; y < H; ++y) {
        uint8_t* row = img + (size_t)y * (size_t)W;
        int acc = 0;
        for (int t = -r; t <= r; ++t) acc += row[refIdx(t, W)];
        for (int x = 0; x < W; ++x) {
            tmp[(size_t)x] = (uint8_t)((acc + win / 2) / win);
            acc -= row[refIdx(x - r, W)];
            acc += row[refIdx(x + r + 1, W)];
        }
        std::memcpy(row, tmp.data(), (size_t)W);
    }
}

void boxBlurV(uint8_t* img, int W, int H, int r)
{
    if (r < 1 || H <= 0) return;
    std::vector<uint8_t> tmp((size_t)H);
    const int win = 2 * r + 1;
    for (int x = 0; x < W; ++x) {
        uint8_t* col = img + (size_t)x;
        int acc = 0;
        for (int t = -r; t <= r; ++t) acc += col[(size_t)refIdx(t, H) * (size_t)W];
        for (int y = 0; y < H; ++y) {
            tmp[(size_t)y] = (uint8_t)((acc + win / 2) / win);
            acc -= col[(size_t)refIdx(y - r, H) * (size_t)W];
            acc += col[(size_t)refIdx(y + r + 1, H) * (size_t)W];
        }
        for (int y = 0; y < H; ++y) col[(size_t)y * (size_t)W] = tmp[(size_t)y];
    }
}

void gaussBlurAlpha(uint8_t* img, int W, int H, double sigma)
{
    sigma = std::max(0.0, sigma);
    const int r = (int)std::lround(sigma);
    if (r < 1) return;
    for (int pass = 0; pass < 3; ++pass) {
        boxBlurH(img, W, H, r);
        boxBlurV(img, W, H, r);
    }
}

bool rasterPathAlpha(const SkPath& path, bool stroke, float width, int clipMode, int W, int H,
                     std::vector<uint8_t>& out)
{
    if (W <= 0 || H <= 0) return false;
    out.assign((size_t)W * (size_t)H, 0);
    SkImageInfo info = SkImageInfo::Make(W, H, kN32_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surf = SkSurfaces::Raster(info);
    if (!surf) return false;
    SkCanvas* c = surf->getCanvas();
    c->clear(SK_ColorTRANSPARENT);
    if (clipMode == 1) c->clipPath(path, true);
    else if (clipMode == 2) c->clipPath(path, SkClipOp::kDifference, true);
    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    paint.setAntiAlias(true);
    if (stroke) {
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(width);
        paint.setStrokeJoin(SkPaint::kRound_Join);
    }
    c->drawPath(path, paint);
    SkPixmap pm;
    if (!surf->peekPixels(&pm)) return false;
    for (int y = 0; y < H; ++y) {
        const uint32_t* row = pm.addr32(0, y);
        uint8_t* o = out.data() + (size_t)y * W;
        for (int x = 0; x < W; ++x) o[x] = (uint8_t)(row[x] >> 24);
    }
    return true;
}

bool buildTextStrokeMask(const Layer& l, float r)
{
    SkPath path;
    if (!textGlyphPath(l.text, path)) return false;
    if (!l.textXf.isIdentity()) {
        SkPath tp;
        path.transform(l.textXf, &tp);
        path = tp;
    }
    const int W = l.maskW, H = l.maskH;
    if (W <= 0 || H <= 0) return false;
    path.offset((float)(-l.maskOx), (float)(-l.maskOy));

    const LayerStyle& s = l.style;
    std::vector<uint8_t> band;
    const float width = (s.strokePos == StrokePos::Center) ? r : r * 2.0f;
    const int clipMode = (s.strokePos == StrokePos::Inside)    ? 1
                         : (s.strokePos == StrokePos::Outside) ? 2
                                                               : 0;
    if (!rasterPathAlpha(path, true, width, clipMode, W, H, band)) return false;

    l.maskStroke.assign((size_t)W * (size_t)H, 0);
    for (size_t i = 0, n = l.maskStroke.size(); i < n; ++i) l.maskStroke[i] = band[i];
    l.maskStrokeKey = strokeKeyOf(l);
    return true;
}

void buildStrokeMask(const Layer& l)
{
    const LayerStyle& s = l.style;
    const int W = l.maskW, H = l.maskH;
    const float r = (float)std::clamp((int)std::lround(s.strokeWidth), 1, 64);
    if (l.kind == LayerKind::Text && buildTextStrokeMask(l, r)) return;

    l.maskStroke.assign((size_t)W * (size_t)H, 0);
    std::vector<float> distOpaque, distClear, distFull;
    {
        std::vector<uint8_t> inv((size_t)W * (size_t)H, 0);
        for (size_t i = 0, n = inv.size(); i < n; ++i) inv[i] = (uint8_t)(255u - l.maskSrc[i]);
        chamferDistToZero(inv.data(), W, H, distOpaque);
    }
    if (s.strokePos != StrokePos::Outside) {
        const int pb = (int)r + 2;
        const int PW = W + pb * 2, PH = H + pb * 2;
        std::vector<uint8_t> padded((size_t)PW * (size_t)PH, 0);
        for (int y = 0; y < H; ++y)
            std::memcpy(padded.data() + (size_t)(pb + y) * PW + pb, l.maskSrc.data() + (size_t)y * W, (size_t)W);
        chamferDistToZero(padded.data(), PW, PH, distFull);
        distClear.assign((size_t)W * (size_t)H, 0.0f);
        for (int y = 0; y < H; ++y)
            std::memcpy(distClear.data() + (size_t)y * W, distFull.data() + (size_t)(pb + y) * PW + pb,
                        (size_t)W * sizeof(float));
    }
    uint8_t* ms = l.maskStroke.data();
    const float dilR = r;
    const float eroR = (s.strokePos == StrokePos::Center ? (float)std::max(1, (int)(r / 2)) : r);
    for (int y = 0; y < H; ++y) {
        const uint8_t* ra = l.maskSrc.data() + (size_t)y * (size_t)W;
        const float* doRow = distOpaque.data() + (size_t)y * W;
        const float* dcRow = distClear.empty() ? nullptr : distClear.data() + (size_t)y * W;
        uint8_t* mo = ms + (size_t)y * (size_t)W;
        for (int x = 0; x < W; ++x) {
            const bool inSrc = ra[x] != 0;
            const bool inDil = doRow[x] <= dilR;
            const bool inEro = dcRow ? (dcRow[x] > eroR) : false;
            switch (s.strokePos) {
            case StrokePos::Inside:
                mo[x] = (inSrc && !inEro) ? 255u : 0u;
                break;
            case StrokePos::Center: {
                const bool outer = doRow[x] <= eroR;
                mo[x] = (outer && !inEro) ? 255u : 0u;
                break;
            }
            default:
                mo[x] = (inDil && !inSrc) ? 255u : 0u;
                break;
            }
        }
    }
    l.maskStrokeKey = strokeKeyOf(l);
}

void buildShadowMask(const Layer& l)
{
    const LayerStyle& s = l.style;
    const int W = l.maskW, H = l.maskH;
    int sdx = 0, sdy = 0;
    shadowShift(s, sdx, sdy);
    std::vector<uint8_t> shifted((size_t)W * (size_t)H, 0);
    const int x0 = std::max(0, sdx), x1 = std::min(W, W + sdx);
    const int y0 = std::max(0, sdy), y1 = std::min(H, H + sdy);
    for (int y = y0; y < y1; ++y)
        std::memcpy(shifted.data() + (size_t)y * W + x0, l.maskSrc.data() + (size_t)(y - sdy) * W + (x0 - sdx),
                    (size_t)(x1 - x0));
    gaussBlurAlpha(shifted.data(), W, H, std::max(0.6, (double)s.shadowSize * 0.5));
    l.maskShadow = shifted;
    l.maskShadowKey = shadowKeyOf(l);
}

bool buildTextStyleImage(const Layer& l, const StyleGridKey& g, Image& out)
{
    const LayerStyle& s = l.style;
    const int W = g.w, H = g.h;
    if (W <= 0 || H <= 0) return false;
    SkPath path;
    if (!textGlyphPath(l.text, path)) return false;
    if (!l.textXf.isIdentity()) {
        SkPath tp;
        path.transform(l.textXf, &tp);
        path = tp;
    }
    path.offset((float)(-g.ox), (float)(-g.oy));

    SkImageInfo info = SkImageInfo::Make(W, H, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surf = SkSurfaces::Raster(info);
    if (!surf) return false;
    SkCanvas* c = surf->getCanvas();
    c->clear(SK_ColorTRANSPARENT);

    const float shA = std::clamp(s.shadowOpacity / 100.0f, 0.0f, 1.0f) * s.shadowColor[3];
    const float stA = std::clamp(s.strokeOpacity / 100.0f, 0.0f, 1.0f) * s.strokeColor[3];

    float fr = l.text.color.r / 255.0f, fg = l.text.color.g / 255.0f, fb = l.text.color.b / 255.0f;
    const float fa = l.text.color.a / 255.0f * std::clamp(l.text.opacity, 0.0f, 1.0f);
    if (s.overlayOn) {
        const float t = std::clamp(s.overlayOpacity / 100.0f, 0.0f, 1.0f);
        fr = fr * (1.0f - t) + s.overlayColor[0] * t;
        fg = fg * (1.0f - t) + s.overlayColor[1] * t;
        fb = fb * (1.0f - t) + s.overlayColor[2] * t;
    }

    if (s.shadowOn && shA > 0.0f) {
        int sdx = 0, sdy = 0;
        shadowShift(s, sdx, sdy);
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(SkColor4f{s.shadowColor[0], s.shadowColor[1], s.shadowColor[2], shA});
        const float sigma = std::max(0.0f, s.shadowSize * 0.5f);
        p.setImageFilter(SkImageFilters::Blur(sigma, sigma, SkTileMode::kDecal, nullptr));
        c->save();
        c->translate((float)sdx, (float)sdy);
        c->drawPath(path, p);
        c->restore();
    }

    SkPaint fill;
    fill.setAntiAlias(true);
    fill.setStyle(SkPaint::kFill_Style);
    fill.setColor(SkColor4f{fr, fg, fb, fa});

    SkPaint stroke;
    stroke.setAntiAlias(true);
    stroke.setStyle(SkPaint::kStroke_Style);
    stroke.setStrokeJoin(SkPaint::kRound_Join);
    stroke.setColor(SkColor4f{s.strokeColor[0], s.strokeColor[1], s.strokeColor[2], stA});
    const float r = std::clamp(s.strokeWidth, 0.0f, (float)kStyleStrokeWidthMax);
    stroke.setStrokeWidth(s.strokePos == StrokePos::Center ? r : r * 2.0f);

    if (s.strokeOn && r >= 0.5f) {
        if (s.strokePos == StrokePos::Outside) {
            c->drawPath(path, stroke);
            c->drawPath(path, fill);
        } else if (s.strokePos == StrokePos::Center) {
            c->drawPath(path, fill);
            c->drawPath(path, stroke);
        } else {
            c->drawPath(path, fill);
            c->save();
            c->clipPath(path, true);
            c->drawPath(path, stroke);
            c->restore();
        }
    } else {
        c->drawPath(path, fill);
    }

    SkPixmap pm;
    if (!surf->peekPixels(&pm)) return false;
    const int srcStride = (int)pm.rowBytes();
    if (srcStride != W * 4) return false;
    out.reset(W, H);
    uint32_t* op = out.pixels();
    for (int y = 0; y < H; ++y) {
        const uint32_t* row = pm.addr32(0, y);
        uint32_t* o = op + (size_t)y * (size_t)W;
        for (int x = 0; x < W; ++x) {
            const uint32_t px = row[x];
            const uint32_t a = px >> 24;
            if (!a) { o[x] = 0; continue; }
            if (a == 255) { o[x] = px; continue; }
            uint32_t rgb = 0;
            for (int sh = 0; sh <= 16; sh += 8) {
                const uint32_t v = (px >> sh) & 0xffu;
                uint32_t s = (v * 255u + a / 2u) / a;
                rgb |= (s > 255u ? 255u : s) << sh;
            }
            o[x] = rgb | (a << 24);
        }
    }
    return true;
}

}

LayerStyle clampStyleLimits(const LayerStyle& in)
{
    LayerStyle s = in;
    s.strokeWidth = std::clamp(s.strokeWidth, 0.0f, kStyleStrokeWidthMax);
    s.shadowSize = std::clamp(s.shadowSize, 0.0f, kStyleShadowBlurMax);
    s.shadowDist = std::clamp(s.shadowDist, -kStyleShadowDistMax, kStyleShadowDistMax);
    s.strokeOpacity = std::clamp(s.strokeOpacity, 0.0f, 100.0f);
    s.shadowOpacity = std::clamp(s.shadowOpacity, 0.0f, 100.0f);
    s.overlayOpacity = std::clamp(s.overlayOpacity, 0.0f, 100.0f);
    return s;
}

bool styleActive(const LayerStyle& s)
{
    return (s.strokeOn && s.strokeWidth >= 0.5f) || s.shadowOn || s.overlayOn;
}

int stylePad(const LayerStyle& s)
{
    if (!styleActive(s)) return 0;
    int pad = 0;
    if (s.strokeOn && s.strokePos != StrokePos::Inside) pad = std::max(pad, (int)std::ceil(s.strokeWidth) + 1);
    if (s.shadowOn) {
        int sdx = 0, sdy = 0;
        shadowShift(s, sdx, sdy);
        pad = std::max(pad, std::max(std::abs(sdx), std::abs(sdy)) + (int)std::ceil(s.shadowSize * 2.0f) + 1);
    }
    return pad;
}

const Image& styledPixelsBase(const Layer& l, int& ox, int& oy)
{
    ox = 0;
    oy = 0;
    if (l.img.empty() || !styleActive(l.style)) return l.img;

    const LayerStyle& s = l.style;
    const StyleCompositeKey key = compositeKeyOf(l);
    if (l.styleKey == key && !l.styleCache.empty()) {
        ox = l.styleCacheOx;
        oy = l.styleCacheOy;
        return l.styleCache;
    }

    const StyleGridKey grid = gridKeyOf(l);
    if (l.kind == LayerKind::Text) {
        Image texOut;
        if (buildTextStyleImage(l, grid, texOut)) {
            ox = grid.ox - l.x;
            oy = grid.oy - l.y;
            l.styleCache = std::move(texOut);
            l.styleKey = key;
            l.styleCacheOx = ox;
            l.styleCacheOy = oy;
            ++g_textStyleHits;
            return l.styleCache;
        }
    }
    if (l.maskSrcKey != grid || l.maskW == 0) buildSrcMask(l);

    if (s.strokeOn) {
        const StyleStrokeKey k = strokeKeyOf(l);
        if (l.maskStrokeKey != k) buildStrokeMask(l);
    } else if (!l.maskStroke.empty()) {
        l.maskStroke.clear();
        l.maskStrokeKey = StyleStrokeKey{};
    }
    if (s.shadowOn) {
        const StyleShadowKey k = shadowKeyOf(l);
        if (l.maskShadowKey != k) buildShadowMask(l);
    } else if (!l.maskShadow.empty()) {
        l.maskShadow.clear();
        l.maskShadowKey = StyleShadowKey{};
    }

    const int W = l.maskW, H = l.maskH;
    const int tx = l.x - l.maskOx, ty = l.y - l.maskOy;
    ox = l.maskOx - l.x;
    oy = l.maskOy - l.y;
    const Image& src = l.img;
    if (W <= 0 || H <= 0) {
        l.styleCache = Image();
        l.styleKey = key;
        l.styleCacheOx = ox;
        l.styleCacheOy = oy;
        return l.styleCache;
    }

    Image out;
    out.reset(W, H);
    uint32_t* op = out.pixels();
    const uint32_t* srcPx = src.pixels();
    const int srcW = src.width();
    const float shA = std::clamp(s.shadowOpacity / 100.0f, 0.0f, 1.0f);
    const float stA = std::clamp(s.strokeOpacity / 100.0f, 0.0f, 1.0f);
    const float ovT = std::clamp(s.overlayOpacity / 100.0f, 0.0f, 1.0f);
    const uint32_t ovPx = packColor(s.overlayColor, 1.0f) & 0x00ffffffu;
    const uint32_t shRGB = packColor(s.shadowColor, 1.0f) & 0x00ffffffu;
    const uint32_t stRGB = packColor(s.strokeColor, 1.0f) & 0x00ffffffu;
    const uint32_t shA255 = (uint32_t)std::lround(shA * 255.0f);
    const uint32_t stA255 = (uint32_t)std::lround(stA * 255.0f);
    const bool ovOn = s.overlayOn && ovT > 0.0f;
    const bool strokeUnder = s.strokeOn && s.strokePos == StrokePos::Outside;
    for (int y = 0; y < H; ++y) {
        uint32_t* ro = op + (size_t)y * (size_t)W;
        const uint8_t* ra = l.maskSrc.data() + (size_t)y * (size_t)W;
        const uint8_t* rsh = l.maskShadow.empty() ? nullptr : l.maskShadow.data() + (size_t)y * (size_t)W;
        const uint8_t* rst = l.maskStroke.empty() ? nullptr : l.maskStroke.data() + (size_t)y * (size_t)W;
        for (int x = 0; x < W; ++x) {
            if (rsh && rsh[x] && shA255)
                ro[x] = over8(ro[x], shRGB | ((((uint32_t)rsh[x] * shA255 + 127u) / 255u) << 24));
            if (strokeUnder && rst && rst[x] && stA255)
                ro[x] = over8(ro[x], stRGB | ((((uint32_t)rst[x] * stA255 + 127u) / 255u) << 24));
            if (ra[x]) {
                uint32_t px = (uint32_t)ra[x] << 24;
                uint32_t rgb = srcPx[(size_t)(y - ty) * (size_t)srcW + (size_t)(x - tx)] & 0x00ffffffu;
                if (ovOn) {
                    auto mix = [ovT](uint32_t a, uint32_t b) {
                        return (uint32_t)std::lround((double)a * (1.0 - ovT) + (double)b * ovT);
                    };
                    rgb = mix(rgb & 0xffu, ovPx & 0xffu) | (mix((rgb >> 8) & 0xffu, (ovPx >> 8) & 0xffu) << 8) |
                          (mix((rgb >> 16) & 0xffu, (ovPx >> 16) & 0xffu) << 16);
                }
                ro[x] = over8(ro[x], px | rgb);
            }
            if (!strokeUnder && rst && rst[x] && stA255)
                ro[x] = over8(ro[x], stRGB | ((((uint32_t)rst[x] * stA255 + 127u) / 255u) << 24));
        }
    }

    l.styleCache = std::move(out);
    l.styleKey = key;
    l.styleCacheOx = ox;
    l.styleCacheOy = oy;
    return l.styleCache;
}

namespace {
const LayerChild* activeLiftChild(const Layer& l)
{
    for (const auto& c : l.children)
        if (c.kind == ChildKind::Lift && c.active()) return &c;
    return nullptr;
}

const LayerChild* activeTriChild(const Layer& l)
{
    for (const auto& c : l.children)
        if (c.kind == ChildKind::Tri && c.active()) return &c;
    return nullptr;
}
}

const Image& styledPixels(const Layer& l, int& ox, int& oy)
{
    const Image& base = styledPixelsBase(l, ox, oy);
    const Image* last = &base;
    const LayerChild* lc = activeLiftChild(l);
    if (lc && !base.empty()) {
        const float kp[5] = {lc->liftStrength, lc->liftRadius, lc->liftSensitivity, lc->liftSoftness,
                             lc->liftMaxGain};
        const StyleCompositeKey sk = compositeKeyOf(l);
        if (l.liftCacheRev == l.pixRev && l.liftCacheStyleKey == sk && !l.liftCache.empty() &&
            l.liftCacheW == base.width() && l.liftCacheH == base.height() &&
            std::memcmp(l.liftCacheParams, kp, sizeof(kp)) == 0) {
            last = &l.liftCache;
        } else {
            l.liftCache = base;
            shadow::Params p;
            p.strength = kp[0];
            p.radius = kp[1];
            p.sensitivity = kp[2];
            p.softness = kp[3];
            p.maxGain = kp[4];
            shadow::removeShadow(l.liftCache, Rect{0, 0, base.width(), base.height()}, p);

            l.liftCacheRev = l.pixRev;
            l.liftCacheStyleKey = sk;
            std::memcpy(l.liftCacheParams, kp, sizeof(kp));
            l.liftCacheW = base.width();
            l.liftCacheH = base.height();
            last = &l.liftCache;
        }
    }

    const LayerChild* tc = activeTriChild(l);
    if (!tc || last->empty()) return *last;

    const float tp[3] = {tc->r, tc->g, tc->b};
    const StyleCompositeKey sk = compositeKeyOf(l);
    if (l.triCacheRev == l.pixRev && l.triCacheStyleKey == sk && !l.triCache.empty() &&
        l.triCacheW == last->width() && l.triCacheH == last->height() && l.triCacheKeep == tc->keepLuma &&
        std::memcmp(l.triCacheParams, tp, sizeof(tp)) == 0)
        return l.triCache;

    l.triCache = *last;
    primaries::Tables tabs;
    primaries::buildTables(primaries::Params{tp[0], tp[1], tp[2], tc->keepLuma}, tabs);
    uint32_t* px = l.triCache.pixels();
    const size_t n = (size_t)l.triCache.width() * (size_t)l.triCache.height();
    for (size_t i = 0; i < n; ++i) px[i] = primaries::applyOne(tabs, px[i]);

    l.triCacheRev = l.pixRev;
    l.triCacheStyleKey = sk;
    l.triCacheKeep = tc->keepLuma;
    std::memcpy(l.triCacheParams, tp, sizeof(tp));
    l.triCacheW = last->width();
    l.triCacheH = last->height();
    return l.triCache;
}

}
