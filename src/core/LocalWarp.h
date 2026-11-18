#pragma once

#include "core/Image.h"
#include "core/Types.h"

#include <vector>

namespace ie {

struct LocalWarpParams {
    Vec2f center{0.0f, 0.0f};
    Vec2f radius{1.0f, 1.0f};
    float rotDeg = 0.0f;
    Vec2f amp{0.0f, 0.0f};
    float poisson = 0.35f;
    int harmonic = 2;
    float falloff = 1.0f;
};

bool localWarpHasRegion(const LocalWarpParams& p);
bool localWarpHasEffect(const LocalWarpParams& p);
Vec2f localWarpField(const LocalWarpParams& p, const Vec2f& at);
Image localWarpApply(const Image& layer, int layerX, int layerY, const LocalWarpParams& p, int& outX, int& outY);
Image localWarpApplyAll(const Image& layer, int layerX, int layerY, const std::vector<LocalWarpParams>& ps,
                       int& outX, int& outY);
Rect localWarpRegionBounds(const LocalWarpParams& p);
Rect localWarpPaintBounds(const LocalWarpParams& p);

struct LocalWarpField {
    int w = 0, h = 0;
    float base_ampX = 0.0f, base_ampY = 0.0f, base_poisson = 0.0f, base_falloff = 0.0f;
    int base_harmonic = 0;
    float ox = 0.0f, oy = 0.0f, step = 2.0f;
    float rx = 1.0f, ry = 1.0f;
    std::vector<float> tab;

    bool matches(const LocalWarpParams& p) const;
    void build(const LocalWarpParams& p);
    Vec2f sample(float lx, float ly) const;
};

void localWarpWarpDocRectFields(const Image& src, int srcX, int srcY, const std::vector<LocalWarpParams>& ps,
                               const std::vector<LocalWarpField>& fs, uint32_t* dst, int dstW, int dstH,
                               int dstX, int dstY, Rect docRect);
void localWarpBlitDocRect(const Image& src, int srcX, int srcY, uint32_t* dst, int dstW, int dstH,
                          int dstX, int dstY, Rect docRect);
void localWarpWarpDocRect(const Image& src, int srcX, int srcY, const std::vector<LocalWarpParams>& ps,
                          uint32_t* dst, int dstW, int dstH, int dstX, int dstY, Rect docRect);

}
