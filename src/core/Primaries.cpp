#include "core/Primaries.h"

#include <cmath>

namespace ie {
namespace primaries {

namespace {
inline int clamp8(float v)
{
    const int i = (int)std::lround(v);
    return i < 0 ? 0 : (i > 255 ? 255 : i);
}
inline unsigned char clamp8u(float v) { return (unsigned char)clamp8(v); }
}

void buildTables(const Params& p, Tables& t)
{
    t.keepLuma = p.keepLuma;
    for (int v = 0; v < 256; ++v) {
        t.r[v] = clamp8u((float)v * p.r);
        t.g[v] = clamp8u((float)v * p.g);
        t.b[v] = clamp8u((float)v * p.b);
    }
    if (!p.keepLuma) return;
    for (int v = 0; v < 256; ++v) {
        t.dR[v] = (int)std::lround(0.299f * ((float)v - (float)(int)t.r[v]));
        t.dG[v] = (int)std::lround(0.587f * ((float)v - (float)(int)t.g[v]));
        t.dB[v] = (int)std::lround(0.114f * ((float)v - (float)(int)t.b[v]));
    }
    for (int i = 0; i < 768; ++i) {
        const int v = i - 255;
        t.clamp[i] = (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
}

void apply(Image& img, Rect roi, const Params& p)
{
    roi = roi.clampTo(img.width(), img.height());
    if (roi.empty() || img.empty() || !img.hasPixels()) return;

    if (!p.keepLuma && p.r == 1.0f && p.g == 1.0f && p.b == 1.0f) return;

    Tables t;
    buildTables(p, t);

    uint32_t* px = img.pixels();
    const int iw = img.width();
    for (int y = roi.y; y < roi.bottom(); ++y) {
        uint32_t* row = px + (size_t)y * (size_t)iw;
        for (int x = roi.x; x < roi.right(); ++x) row[x] = applyOne(t, row[x]);
    }
}

}
}
