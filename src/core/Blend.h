#pragma once

#include "core/Image.h"
#include "core/Types.h"
#include "include/core/SkBlendMode.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ie {
namespace blend {

inline SkBlendMode skBlend(BlendMode m)
{
    switch (m) {
    case BlendMode::Multiply: return SkBlendMode::kMultiply;
    case BlendMode::Screen: return SkBlendMode::kScreen;
    case BlendMode::Overlay: return SkBlendMode::kOverlay;
    case BlendMode::Darken: return SkBlendMode::kDarken;
    case BlendMode::Lighten: return SkBlendMode::kLighten;
    case BlendMode::Add: return SkBlendMode::kPlus;
    case BlendMode::ColorDodge: return SkBlendMode::kColorDodge;
    case BlendMode::ColorBurn: return SkBlendMode::kColorBurn;
    case BlendMode::HardLight: return SkBlendMode::kHardLight;
    case BlendMode::SoftLight: return SkBlendMode::kSoftLight;
    case BlendMode::Difference: return SkBlendMode::kDifference;
    case BlendMode::Exclusion: return SkBlendMode::kExclusion;
    case BlendMode::Modulate: return SkBlendMode::kModulate;
    case BlendMode::Hue: return SkBlendMode::kHue;
    case BlendMode::Saturation: return SkBlendMode::kSaturation;
    case BlendMode::Color: return SkBlendMode::kColor;
    case BlendMode::Luminosity: return SkBlendMode::kLuminosity;
    default: return SkBlendMode::kSrcOver;
    }
}

inline uint32_t overPixel(uint32_t d, uint32_t s, float opacity)
{
    const float sa = (float)((s >> 24) & 0xff) / 255.0f * opacity;
    if (sa <= 0.0f) return d;
    const float da = (float)((d >> 24) & 0xff) / 255.0f;
    const float inv = 1.0f - sa;
    const float oa = sa + da * inv;
    if (oa <= 0.0f) return d;
    const float sr = (float)(s & 0xff), sg = (float)((s >> 8) & 0xff), sb = (float)((s >> 16) & 0xff);
    const float dr = (float)(d & 0xff), dg = (float)((d >> 8) & 0xff), db = (float)((d >> 16) & 0xff);
    auto q = [](float v) {
        if (v <= 0.0f) return 0u;
        if (v >= 255.0f) return 255u;
        return (uint32_t)(v + 0.5f);
    };
    const uint32_t r = q((sr * sa + dr * da * inv) / oa);
    const uint32_t g = q((sg * sa + dg * da * inv) / oa);
    const uint32_t b = q((sb * sa + db * da * inv) / oa);
    const uint32_t a = q(oa * 255.0f);
    return r | (g << 8) | (b << 16) | (a << 24);
}

inline uint32_t erasePixel(uint32_t d, float amount)
{
    if (amount <= 0.0f) return d;
    if (amount >= 1.0f) return d & 0x00ffffffu;
    const uint32_t a = (d >> 24) & 0xff;
    const uint32_t na = (uint32_t)((float)a * (1.0f - amount) + 0.5f);
    return (d & 0x00ffffffu) | (na << 24);
}

inline uint32_t paintPixel(uint32_t d, uint32_t s, float amount, bool lockAlpha)
{
    if (!lockAlpha) return overPixel(d, s, amount);
    const uint32_t da = d >> 24;
    if (da == 0) return d;
    const uint32_t r = overPixel(d, s, amount);
    return (r & 0x00ffffffu) | (da << 24);
}

inline float blendLum(float r, float g, float b) { return 0.30f * r + 0.59f * g + 0.11f * b; }

inline void blendSetSat(float& r, float& g, float& b, float s)
{
    const float mn = std::min(r, std::min(g, b));
    const float mx = std::max(r, std::max(g, b));
    const float sat = mx - mn;
    const float k = (sat <= 0.0f) ? 0.0f : s / sat;
    r = (r - mn) * k;
    g = (g - mn) * k;
    b = (b - mn) * k;
}

inline void blendClipColor(float& r, float& g, float& b, float a)
{
    const float mn = std::min(r, std::min(g, b));
    const float mx = std::max(r, std::max(g, b));
    const float l = blendLum(r, g, b);
    if (mn < 0.0f && l != mn) {
        const float k = l / (l - mn);
        r = l + (r - l) * k;
        g = l + (g - l) * k;
        b = l + (b - l) * k;
    }
    if (mx > a && l != mx) {
        const float k = (a - l) / (mx - l);
        r = l + (r - l) * k;
        g = l + (g - l) * k;
        b = l + (b - l) * k;
    }
    r = std::max(r, 0.0f);
    g = std::max(g, 0.0f);
    b = std::max(b, 0.0f);
}

inline void blendSetLum(float& r, float& g, float& b, float l)
{
    const float d = l - blendLum(r, g, b);
    r += d;
    g += d;
    b += d;
}

inline float blendSoftLight(float cb, float cs)
{
    if (cs <= 0.5f) return cb - (1.0f - 2.0f * cs) * cb * (1.0f - cb);
    const float d = (cb <= 0.25f) ? (((16.0f * cb - 12.0f) * cb + 4.0f) * cb) : std::sqrt(cb);
    return cb + (2.0f * cs - 1.0f) * (d - cb);
}

inline float blendChannel(float cb, float cs, BlendMode mode)
{
    switch (mode) {
    case BlendMode::Multiply:
        return cb * cs;
    case BlendMode::Screen:
        return cb + cs - cb * cs;
    case BlendMode::Overlay:
        return (cb <= 0.5f) ? (2.0f * cb * cs) : (1.0f - 2.0f * (1.0f - cb) * (1.0f - cs));
    case BlendMode::Darken:
        return std::min(cb, cs);
    case BlendMode::Lighten:
        return std::max(cb, cs);
    case BlendMode::ColorDodge:
        if (cb <= 0.0f) return 0.0f;
        if (cs >= 1.0f) return 1.0f;
        return std::min(1.0f, cb / (1.0f - cs));
    case BlendMode::ColorBurn:
        if (cb >= 1.0f) return 1.0f;
        if (cs <= 0.0f) return 0.0f;
        return 1.0f - std::min(1.0f, (1.0f - cb) / cs);
    case BlendMode::HardLight:
        return (cs <= 0.5f) ? (2.0f * cb * cs) : (1.0f - 2.0f * (1.0f - cb) * (1.0f - cs));
    case BlendMode::SoftLight:
        return blendSoftLight(cb, cs);
    case BlendMode::Difference:
        return std::abs(cb - cs);
    case BlendMode::Exclusion:
        return cb + cs - 2.0f * cb * cs;
    default:
        return cs;
    }
}

inline void blendColor(const float cb[3], const float cs[3], BlendMode mode, float out[3])
{
    out[0] = cb[0];
    out[1] = cb[1];
    out[2] = cb[2];
    if (mode == BlendMode::Hue || mode == BlendMode::Saturation || mode == BlendMode::Color ||
        mode == BlendMode::Luminosity) {
        const float satS = std::max(cs[0], std::max(cs[1], cs[2])) - std::min(cs[0], std::min(cs[1], cs[2]));
        const float satB = std::max(cb[0], std::max(cb[1], cb[2])) - std::min(cb[0], std::min(cb[1], cb[2]));
        if (mode == BlendMode::Hue) {
            out[0] = cs[0];
            out[1] = cs[1];
            out[2] = cs[2];
            blendSetSat(out[0], out[1], out[2], satB);
            blendSetLum(out[0], out[1], out[2], blendLum(cb[0], cb[1], cb[2]));
            blendClipColor(out[0], out[1], out[2], 1.0f);
        } else if (mode == BlendMode::Saturation) {
            out[0] = cb[0];
            out[1] = cb[1];
            out[2] = cb[2];
            blendSetSat(out[0], out[1], out[2], satS);
            blendSetLum(out[0], out[1], out[2], blendLum(cb[0], cb[1], cb[2]));
            blendClipColor(out[0], out[1], out[2], 1.0f);
        } else if (mode == BlendMode::Color) {
            out[0] = cs[0];
            out[1] = cs[1];
            out[2] = cs[2];
            blendSetLum(out[0], out[1], out[2], blendLum(cb[0], cb[1], cb[2]));
            blendClipColor(out[0], out[1], out[2], 1.0f);
        } else {
            out[0] = cb[0];
            out[1] = cb[1];
            out[2] = cb[2];
            blendSetLum(out[0], out[1], out[2], blendLum(cs[0], cs[1], cs[2]));
            blendClipColor(out[0], out[1], out[2], 1.0f);
        }
        for (int i = 0; i < 3; ++i) out[i] = std::clamp(out[i], 0.0f, 1.0f);
        return;
    }
    for (int i = 0; i < 3; ++i) out[i] = std::clamp(blendChannel(cb[i], cs[i], mode), 0.0f, 1.0f);
}

inline uint32_t compositePixel(uint32_t d, uint32_t s, float opacity, BlendMode mode)
{
    if (mode == BlendMode::Normal) return overPixel(d, s, opacity);

    const float o = std::clamp(opacity, 0.0f, 1.0f);
    const float sa = (float)((s >> 24) & 0xff) / 255.0f * o;
    if (sa <= 0.0f) return d;
    const float da = (float)((d >> 24) & 0xff) / 255.0f;
    if (mode == BlendMode::Modulate) {
        const float ao = sa * da;
        if (ao <= 0.0f) return d & 0x00ffffffu;
        auto qm = [](float v) { return (uint32_t)std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f); };
        const float cr = (float)(s & 0xff) / 255.0f * (float)(d & 0xff) / 255.0f;
        const float cg = (float)((s >> 8) & 0xff) / 255.0f * (float)((d >> 8) & 0xff) / 255.0f;
        const float cbl = (float)((s >> 16) & 0xff) / 255.0f * (float)((d >> 16) & 0xff) / 255.0f;
        return qm(cr) | (qm(cg) << 8) | (qm(cbl) << 16) | (qm(ao) << 24);
    }

    const float ao = (mode == BlendMode::Add) ? std::min(1.0f, sa + da) : (sa + da * (1.0f - sa));
    if (ao <= 0.0f) return d;

    const float cs[3] = {(float)(s & 0xff) / 255.0f, (float)((s >> 8) & 0xff) / 255.0f,
                         (float)((s >> 16) & 0xff) / 255.0f};
    const float cb[3] = {(float)(d & 0xff) / 255.0f, (float)((d >> 8) & 0xff) / 255.0f,
                         (float)((d >> 16) & 0xff) / 255.0f};
    float b[3];
    blendColor(cb, cs, mode, b);

    auto q = [](float v) { return (uint32_t)std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f); };
    uint32_t out = q(ao) << 24;
    for (int i = 0; i < 3; ++i) {
        const float premul = sa * (1.0f - da) * cs[i] + sa * da * b[i] + (1.0f - sa) * da * cb[i];
        out |= q(premul / ao) << (8 * i);
    }
    return out;
}

void over(uint32_t* dst, const uint32_t* src, size_t count, float opacity);

bool allOpaque(const uint32_t* px, size_t count);

const char* activeSimdTarget();

void overMasked(uint32_t* dst, const uint32_t* src, const uint8_t* mask, size_t count, float opacity);

void brightnessContrast(Image& img, Rect roi, float brightness, float contrast);
void invert(Image& img, Rect roi);
void desaturate(Image& img, Rect roi);
void threshold(Image& img, Rect roi, int level);
void hueShift(Image& img, Rect roi, int degrees);

void blur(Image& img, Rect roi, int radius);

void fillMasked(Image& img, int ox, int oy, const uint8_t* mask, int mw, int mh, Color c);

void strokeMask(Image& img, int ox, int oy, const uint8_t* mask, int mw, int mh, int strokeWidth, Color c);

}
}
