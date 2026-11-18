#pragma once

#include "core/Image.h"
#include "core/Types.h"

namespace ie {
namespace primaries {

struct Params {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    bool keepLuma = false;
};

inline float luma(int r, int g, int b) { return 0.299f * (float)r + 0.587f * (float)g + 0.114f * (float)b; }

struct Tables {
    bool keepLuma = false;
    unsigned char r[256], g[256], b[256];
    int dR[256], dG[256], dB[256];
    unsigned char clamp[768];
};

void buildTables(const Params& p, Tables& t);

inline uint32_t applyOne(const Tables& t, uint32_t px)
{
    const uint32_t a = px >> 24;
    const int r0 = (int)(px & 255u), g0 = (int)((px >> 8) & 255u), b0 = (int)((px >> 16) & 255u);
    unsigned char nr = t.r[r0], ng = t.g[g0], nb = t.b[b0];
    if (t.keepLuma) {
        const int d = t.dR[r0] + t.dG[g0] + t.dB[b0] + 255;
        nr = t.clamp[(int)nr + d];
        ng = t.clamp[(int)ng + d];
        nb = t.clamp[(int)nb + d];
    }
    return (a << 24) | ((uint32_t)nb << 16) | ((uint32_t)ng << 8) | (uint32_t)nr;
}

void apply(Image& img, Rect roi, const Params& p);

}
}
