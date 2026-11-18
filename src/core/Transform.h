#pragma once

#include "core/Image.h"
#include "core/Types.h"

#include <vector>

#include "include/core/SkMatrix.h"

namespace ie {

struct XForm {
    float cx = 0.0f, cy = 0.0f;
    float sx = 1.0f, sy = 1.0f;
    float rot = 0.0f;
};

void xformQuad(const Rect& srcCanvasRect, const XForm& xf, Vec2f out[4]);

SkMatrix xformMatrix(const Rect& srcCanvasRect, const XForm& xf);
SkMatrix xformMatrix(const SkRect& srcCanvasRect, const XForm& xf);
SkMatrix xformMatrixAbout(float srcCx, float srcCy, const XForm& xf);

Image xformRasterize(const Image& src, Rect srcRect, const XForm& xf, int oldX, int oldY, int& outOx, int& outOy,
                     bool fastPreview = false);
Image quadRasterize(const Image& src, Rect srcRect, int oldX, int oldY, const Vec2f quad[4], int& outOx, int& outOy,
                    bool fastPreview = false);


float meshC1BumpWeight(const Vec2f& param, float gu, float gv);
Vec2f meshC1Surface(const std::vector<Vec2f>& pts, int n, const std::vector<Vec2f>& extraParam,
                    const std::vector<Vec2f>& extraDisp, float gu, float gv);
bool meshC1Inverse(const std::vector<Vec2f>& pts, int n, const Vec2f& p, float& gu, float& gv);
Image meshC1Rasterize(const Image& src, Rect srcRect, int oldX, int oldY, const std::vector<Vec2f>& pts, int n,
                      const std::vector<Vec2f>& extraParam, const std::vector<Vec2f>& extraDisp, int& outOx,
                      int& outOy, bool highQuality, int sub = 3);

}
