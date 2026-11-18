#pragma once

#include "core/Types.h"

#include "include/core/SkMatrix.h"

#include <algorithm>
#include <cmath>

namespace ie {

struct CanvasViewState {
    float zoom = 1.0f;
    float scaleY = 1.0f;
    float rotationDeg = 0.0f;
    float skewX = 0.0f;
    float panX = 0, panY = 0;
    float vpX = 0, vpY = 0, vpW = 1, vpH = 1;

    float centerX() const { return vpX + vpW * 0.5f; }
    float centerY() const { return vpY + vpH * 0.5f; }

    bool axisAligned() const { return rotationDeg == 0.0f && skewX == 0.0f && scaleY == 1.0f; }

    SkMatrix matrix(int imgW, int imgH) const
    {
        const float cx = centerX() + panX;
        const float cy = centerY() + panY;
        const float tx = axisAligned() ? std::round(cx - imgW * zoom * 0.5f) : cx - imgW * zoom * 0.5f;
        const float ty = axisAligned() ? std::round(cy - imgH * zoom * 0.5f) : cy - imgH * zoom * 0.5f;

        SkMatrix s = SkMatrix::Scale(zoom, zoom * scaleY);
        SkMatrix local = s;
        if (skewX != 0.0f) {
            SkMatrix k;
            k.setSkew(skewX, 0.0f);
            local.setConcat(k, local);
        }
        if (rotationDeg != 0.0f) {
            SkMatrix r;
            r.setRotate(rotationDeg);
            local.setConcat(r, local);
        }
        SkMatrix t;
        t.setTranslate(tx, ty);
        SkMatrix m;
        m.setConcat(t, local);
        return m;
    }

    float originX(int imgW) const
    {
        const float cx = centerX() + panX;
        return axisAligned() ? std::round(cx - imgW * zoom * 0.5f) : cx - imgW * zoom * 0.5f;
    }
    float originY(int imgH) const
    {
        const float cy = centerY() + panY;
        return axisAligned() ? std::round(cy - imgH * zoom * 0.5f) : cy - imgH * zoom * 0.5f;
    }

    Vec2f toScreen(float ix, float iy, int imgW, int imgH) const
    {
        SkPoint p = matrix(imgW, imgH).mapXY(ix, iy);
        return Vec2f{p.x(), p.y()};
    }
    Vec2f toImage(float sx, float sy, int imgW, int imgH) const
    {
        SkMatrix inv;
        if (!matrix(imgW, imgH).invert(&inv)) {
            const float z = zoom > 1e-6f ? zoom : 1e-6f;
            return Vec2f{(sx - originX(imgW)) / z, (sy - originY(imgH)) / z};
        }
        SkPoint p = inv.mapXY(sx, sy);
        return Vec2f{p.x(), p.y()};
    }

    Vec2i pixelAt(float sx, float sy, int imgW, int imgH) const
    {
        const Vec2f p = toImage(sx, sy, imgW, imgH);
        return Vec2i{(int)std::floor(p.x), (int)std::floor(p.y)};
    }

    Rect rectToScreen(Rect r, int imgW, int imgH) const
    {
        if (r.empty()) return Rect{};
        const Vec2f a = toScreen((float)r.x, (float)r.y, imgW, imgH);
        const Vec2f b = toScreen((float)r.right(), (float)r.y, imgW, imgH);
        const Vec2f c = toScreen((float)r.right(), (float)r.bottom(), imgW, imgH);
        const Vec2f d = toScreen((float)r.x, (float)r.bottom(), imgW, imgH);
        const float x0 = std::floor(std::min(std::min(a.x, b.x), std::min(c.x, d.x)));
        const float y0 = std::floor(std::min(std::min(a.y, b.y), std::min(c.y, d.y)));
        const float x1 = std::ceil(std::max(std::max(a.x, b.x), std::max(c.x, d.x)));
        const float y1 = std::ceil(std::max(std::max(a.y, b.y), std::max(c.y, d.y)));
        return Rect{(int)x0 - 1, (int)y0 - 1, (int)(x1 - x0) + 2, (int)(y1 - y0) + 2};
    }
    static bool imageHas(Vec2i px, int imgW, int imgH)
    {
        return px.x >= 0 && px.y >= 0 && px.x < imgW && px.y < imgH;
    }

    void fit(int imgW, int imgH)
    {
        if (imgW <= 0 || imgH <= 0) return;
        const float sx = (vpW * 0.94f) / (float)imgW;
        const float sy = (vpH * 0.94f) / (float)imgH;
        zoom = std::clamp(std::min(sx, sy), 0.01f, 64.0f);
        scaleY = 1.0f;
        rotationDeg = 0.0f;
        skewX = 0.0f;
        panX = panY = 0;
    }

    void setZoomAt(float nz, float sx, float sy, int imgW, int imgH)
    {
        nz = std::clamp(nz, 0.01f, 64.0f);
        const Vec2f p = toImage(sx, sy, imgW, imgH);
        zoom = nz;
        const Vec2f q = toScreen(p.x, p.y, imgW, imgH);
        panX += sx - q.x;
        panY += sy - q.y;
    }

    void panBy(float dx, float dy)
    {
        panX += dx;
        panY += dy;
    }

    struct PanStep {
        bool panning = false;
        bool begin = false;
        bool move = false;
        bool end = false;
    };
    static PanStep panStep(bool panning, bool pressed, bool released, int button, bool inside)
    {
        PanStep r;
        if (!inside) return r;
        if (!panning) {
            if (pressed && button == 2) { r.panning = true; r.begin = true; }
            return r;
        }
        if (released && button == 2) { r.end = true; return r; }
        if (pressed) { r.panning = true; return r; }
        r.panning = true; r.move = true; return r;
    }

    bool containsScreen(float sx, float sy) const
    {
        return sx >= vpX && sx < vpX + vpW && sy >= vpY && sy < vpY + vpH;
    }

    Rect visibleRect(int imgW, int imgH) const
    {
        SkMatrix inv;
        const bool invertible = matrix(imgW, imgH).invert(&inv);
        const float qx[4] = {vpX, vpX + vpW, vpX + vpW, vpX};
        const float qy[4] = {vpY, vpY, vpY + vpH, vpY + vpH};
        float x0 = 0.0f, y0 = 0.0f, x1 = (float)imgW, y1 = (float)imgH;
        if (invertible) {
            x0 = y0 = 1e30f;
            x1 = y1 = -1e30f;
            for (int i = 0; i < 4; ++i) {
                SkPoint p = inv.mapXY(qx[i], qy[i]);
                x0 = std::min(x0, p.x());
                y0 = std::min(y0, p.y());
                x1 = std::max(x1, p.x());
                y1 = std::max(y1, p.y());
            }
        }
        Rect r;
        r.x = std::max(0, (int)std::floor(x0));
        r.y = std::max(0, (int)std::floor(y0));
        r.w = std::min(imgW, (int)std::ceil(x1)) - r.x;
        r.h = std::min(imgH, (int)std::ceil(y1)) - r.y;
        return r;
    }
};

}
