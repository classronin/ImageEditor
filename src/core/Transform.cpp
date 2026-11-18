#include "core/Transform.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkTileMode.h"
#include "include/core/SkVertices.h"

namespace ie {

namespace {

inline uint32_t bilinearSample(const uint32_t* sp, int sw, int sh, float u, float v, float x0, float x1, float y0,
                               float y1)
{
    if (u < x0 || u > x1 || v < y0 || v > y1) return 0;
    const float fx = u - 0.5f, fy = v - 0.5f;
    const int ix = (fx >= 0.0f) ? (int)fx : (int)fx - 1;
    const int iy = (fy >= 0.0f) ? (int)fy : (int)fy - 1;
    const float tx = fx - (float)ix, ty = fy - (float)iy;
    const float w00 = (1.0f - tx) * (1.0f - ty);
    const float w10 = tx * (1.0f - ty);
    const float w01 = (1.0f - tx) * ty;
    const float w11 = tx * ty;

    float r = 0, g = 0, b = 0, a = 0;
    if (iy >= 0 && iy < sh) {
        const uint32_t* rowp = sp + (size_t)iy * (size_t)sw;
        if (w00 > 0.0f && ix >= 0 && ix < sw) {
            const uint32_t vv = rowp[ix];
            const float aa = (float)(vv >> 24) * (1.0f / 255.0f) * w00;
            r += (float)(vv & 0xff) * aa;
            g += (float)((vv >> 8) & 0xff) * aa;
            b += (float)((vv >> 16) & 0xff) * aa;
            a += aa;
        }
        if (w10 > 0.0f && ix + 1 >= 0 && ix + 1 < sw) {
            const uint32_t vv = rowp[ix + 1];
            const float aa = (float)(vv >> 24) * (1.0f / 255.0f) * w10;
            r += (float)(vv & 0xff) * aa;
            g += (float)((vv >> 8) & 0xff) * aa;
            b += (float)((vv >> 16) & 0xff) * aa;
            a += aa;
        }
    }
    if (iy + 1 >= 0 && iy + 1 < sh) {
        const uint32_t* rowp = sp + (size_t)(iy + 1) * (size_t)sw;
        if (w01 > 0.0f && ix >= 0 && ix < sw) {
            const uint32_t vv = rowp[ix];
            const float aa = (float)(vv >> 24) * (1.0f / 255.0f) * w01;
            r += (float)(vv & 0xff) * aa;
            g += (float)((vv >> 8) & 0xff) * aa;
            b += (float)((vv >> 16) & 0xff) * aa;
            a += aa;
        }
        if (w11 > 0.0f && ix + 1 >= 0 && ix + 1 < sw) {
            const uint32_t vv = rowp[ix + 1];
            const float aa = (float)(vv >> 24) * (1.0f / 255.0f) * w11;
            r += (float)(vv & 0xff) * aa;
            g += (float)((vv >> 8) & 0xff) * aa;
            b += (float)((vv >> 16) & 0xff) * aa;
            a += aa;
        }
    }
    if (a <= 0.0f) return 0;
    const float inv = 1.0f / a;
    r *= inv;
    g *= inv;
    b *= inv;
    const uint32_t rr = (uint32_t)((r < 0.0f ? 0.0f : (r > 255.0f ? 255.0f : r)) + 0.5f);
    const uint32_t gg = (uint32_t)((g < 0.0f ? 0.0f : (g > 255.0f ? 255.0f : g)) + 0.5f);
    const uint32_t bb = (uint32_t)((b < 0.0f ? 0.0f : (b > 255.0f ? 255.0f : b)) + 0.5f);
    const float ac = a > 1.0f ? 1.0f : a;
    const uint32_t aa = (uint32_t)(ac * 255.0f + 0.5f);
    return rr | (gg << 8) | (bb << 16) | (aa << 24);
}

inline void clipSpan(float a0, float d, float lo, float hi, int& xa, int& xb)
{
    if (xa > xb) return;
    if (std::abs(d) < 1e-6f) {
        if (a0 < lo || a0 > hi) xb = -1;
        return;
    }
    float t0 = (lo - a0) / d, t1 = (hi - a0) / d;
    if (t0 > t1) std::swap(t0, t1);
    const int a = (int)std::floor(t0);
    const int b = (int)std::ceil(t1);
    if (a > xa) xa = a;
    if (b < xb) xb = b;
}

Image rasterizeAxial(const Image& src, const Rect& srcRect, const Rect& srcCanvasRect, const XForm& xf, int oldX,
                     int oldY, int outOx, int outOy, int ow, int oh)
{
    Image out(ow, oh);
    const int sw = src.width(), sh = src.height();
    if (sw <= 0 || sh <= 0) return out;
    const uint32_t* sp = src.pixels();
    uint32_t* dp = out.pixels();

    const float invSx = 1.0f / std::max(1e-4f, xf.sx);
    const float invSy = 1.0f / std::max(1e-4f, xf.sy);
    const float srcCx = (float)srcCanvasRect.x + (float)srcCanvasRect.w * 0.5f;
    const float srcCy = (float)srcCanvasRect.y + (float)srcCanvasRect.h * 0.5f;
    const float sx0 = (float)srcRect.x, sx1 = (float)srcRect.right();
    const float sy0 = (float)srcRect.y, sy1 = (float)srcRect.bottom();

    const float u0 = ((float)outOx + 0.5f - xf.cx) * invSx + srcCx - (float)oldX;
    const float v0 = ((float)outOy + 0.5f - xf.cy) * invSy + srcCy - (float)oldY;
    const float du = invSx, dv = invSy;

    int xa = 0, xb = ow - 1;
    clipSpan(u0, du, sx0 - 1.0f, sx1 + 1.0f, xa, xb);
    int ya = 0, yb = oh - 1;
    clipSpan(v0, dv, sy0 - 1.0f, sy1 + 1.0f, ya, yb);
    if (xb < xa || yb < ya || xa > xb) return out;

    const float vFirst = v0 + dv * (float)ya, vLast = v0 + dv * (float)yb;
    int iyMin = (int)std::floor(std::min(vFirst, vLast) - 0.5f) - 1;
    int iyMax = (int)std::floor(std::max(vFirst, vLast) - 0.5f) + 2;
    iyMin = std::max(0, iyMin);
    iyMax = std::min(sh, iyMax);
    if (iyMax <= iyMin) return out;

    const int bufW = ow, bufH = iyMax - iyMin;
    std::vector<float> buf((size_t)bufW * (size_t)bufH * 4, 0.0f);

    for (int i = 0; i < bufH; ++i) {
        const int sy = iyMin + i;
        const uint32_t* srow = sp + (size_t)sy * (size_t)sw;
        float* drow = buf.data() + (size_t)i * (size_t)bufW * 4;
        for (int x = xa; x <= xb; ++x) {
            const float u = u0 + du * (float)x;
            if (u < sx0 || u > sx1) continue;
            const float fx = u - 0.5f;
            const int ix = (fx >= 0.0f) ? (int)fx : (int)fx - 1;
            const float tx = fx - (float)ix;
            float pr = 0, pg = 0, pb = 0, pa = 0;
            if (ix >= 0 && ix < sw) {
                const uint32_t vv = srow[ix];
                const float aa = (float)(vv >> 24) * (1.0f / 255.0f) * (1.0f - tx);
                pr += (float)(vv & 0xff) * aa;
                pg += (float)((vv >> 8) & 0xff) * aa;
                pb += (float)((vv >> 16) & 0xff) * aa;
                pa += aa;
            }
            if (ix + 1 >= 0 && ix + 1 < sw) {
                const uint32_t vv = srow[ix + 1];
                const float aa = (float)(vv >> 24) * (1.0f / 255.0f) * tx;
                pr += (float)(vv & 0xff) * aa;
                pg += (float)((vv >> 8) & 0xff) * aa;
                pb += (float)((vv >> 16) & 0xff) * aa;
                pa += aa;
            }
            float* o = drow + (size_t)x * 4;
            o[0] = pr;
            o[1] = pg;
            o[2] = pb;
            o[3] = pa;
        }
    }

    for (int y = ya; y <= yb; ++y) {
        const float v = v0 + dv * (float)y;
        if (v < sy0 || v > sy1) continue;
        const float fy = v - 0.5f;
        const int iy = (fy >= 0.0f) ? (int)fy : (int)fy - 1;
        const float ty = fy - (float)iy;
        const float* r0 = (iy >= iyMin && iy < iyMax) ? buf.data() + (size_t)(iy - iyMin) * (size_t)bufW * 4 : nullptr;
        const float* r1 =
            (iy + 1 >= iyMin && iy + 1 < iyMax) ? buf.data() + (size_t)(iy + 1 - iyMin) * (size_t)bufW * 4 : nullptr;
        uint32_t* drow = dp + (size_t)y * (size_t)ow;
        for (int x = xa; x <= xb; ++x) {
            float pr = 0, pg = 0, pb = 0, pa = 0;
            if (r0) {
                const float* p = r0 + (size_t)x * 4;
                const float w = 1.0f - ty;
                pr += p[0] * w;
                pg += p[1] * w;
                pb += p[2] * w;
                pa += p[3] * w;
            }
            if (r1) {
                const float* p = r1 + (size_t)x * 4;
                pr += p[0] * ty;
                pg += p[1] * ty;
                pb += p[2] * ty;
                pa += p[3] * ty;
            }
            if (pa <= 0.0f) continue;
            const float inv = 1.0f / pa;
            const uint32_t rr = (uint32_t)((pr * inv < 0.0f ? 0.0f : (pr * inv > 255.0f ? 255.0f : pr * inv)) + 0.5f);
            const uint32_t gg = (uint32_t)((pg * inv < 0.0f ? 0.0f : (pg * inv > 255.0f ? 255.0f : pg * inv)) + 0.5f);
            const uint32_t bb = (uint32_t)((pb * inv < 0.0f ? 0.0f : (pb * inv > 255.0f ? 255.0f : pb * inv)) + 0.5f);
            const float ac = pa > 1.0f ? 1.0f : pa;
            const uint32_t aa = (uint32_t)(ac * 255.0f + 0.5f);
            drow[x] = rr | (gg << 8) | (bb << 16) | (aa << 24);
        }
    }

    return out;
}

}

namespace {

bool solveHomography(const Vec2f s[4], const Vec2f d[4], double h[9])
{
    double a[8][9];
    for (int i = 0; i < 4; ++i) {
        const double x = s[i].x, y = s[i].y, u = d[i].x, v = d[i].y;
        a[2 * i][0] = x; a[2 * i][1] = y; a[2 * i][2] = 1.0;
        a[2 * i][3] = 0.0; a[2 * i][4] = 0.0; a[2 * i][5] = 0.0;
        a[2 * i][6] = -x * u; a[2 * i][7] = -y * u; a[2 * i][8] = u;
        a[2 * i + 1][0] = 0.0; a[2 * i + 1][1] = 0.0; a[2 * i + 1][2] = 0.0;
        a[2 * i + 1][3] = x; a[2 * i + 1][4] = y; a[2 * i + 1][5] = 1.0;
        a[2 * i + 1][6] = -x * v; a[2 * i + 1][7] = -y * v; a[2 * i + 1][8] = v;
    }
    for (int c = 0; c < 8; ++c) {
        int piv = c;
        for (int r = c + 1; r < 8; ++r)
            if (std::abs(a[r][c]) > std::abs(a[piv][c])) piv = r;
        if (std::abs(a[piv][c]) < 1e-9) return false;
        if (piv != c)
            for (int k = c; k < 9; ++k) std::swap(a[c][k], a[piv][k]);
        const double inv = 1.0 / a[c][c];
        for (int k = c; k < 9; ++k) a[c][k] *= inv;
        for (int r = 0; r < 8; ++r) {
            if (r == c) continue;
            const double f = a[r][c];
            if (f == 0.0) continue;
            for (int k = c; k < 9; ++k) a[r][k] -= f * a[c][k];
        }
    }
    for (int i = 0; i < 8; ++i) h[i] = a[i][8];
    h[8] = 1.0;
    return true;
}

bool invert3x3(const double m[9], double out[9])
{
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                       m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-12) return false;
    const double inv = 1.0 / det;
    out[0] = (m[4] * m[8] - m[5] * m[7]) * inv;
    out[1] = (m[2] * m[7] - m[1] * m[8]) * inv;
    out[2] = (m[1] * m[5] - m[2] * m[4]) * inv;
    out[3] = (m[5] * m[6] - m[3] * m[8]) * inv;
    out[4] = (m[0] * m[8] - m[2] * m[6]) * inv;
    out[5] = (m[2] * m[3] - m[0] * m[5]) * inv;
    out[6] = (m[3] * m[7] - m[4] * m[6]) * inv;
    out[7] = (m[1] * m[6] - m[0] * m[7]) * inv;
    out[8] = (m[0] * m[4] - m[1] * m[3]) * inv;
    return true;
}

}

Image quadRasterize(const Image& src, Rect srcRect, int oldX, int oldY, const Vec2f quad[4], int& outOx, int& outOy,
                    bool fastPreview)
{
    outOx = outOy = 0;
    if (src.empty() || srcRect.empty()) return Image();

    const Rect rc{oldX + srcRect.x, oldY + srcRect.y, srcRect.w, srcRect.h};
    Vec2f s[4] = {Vec2f{(float)rc.x, (float)rc.y}, Vec2f{(float)rc.right(), (float)rc.y},
                  Vec2f{(float)rc.right(), (float)rc.bottom()}, Vec2f{(float)rc.x, (float)rc.bottom()}};
    double h[9], hi[9];
    if (!solveHomography(s, quad, h)) return Image();
    if (!invert3x3(h, hi)) return Image();

    float minX = quad[0].x, minY = quad[0].y, maxX = quad[0].x, maxY = quad[0].y;
    for (int i = 1; i < 4; ++i) {
        minX = std::min(minX, quad[i].x);
        minY = std::min(minY, quad[i].y);
        maxX = std::max(maxX, quad[i].x);
        maxY = std::max(maxY, quad[i].y);
    }
    outOx = (int)std::floor(minX) - 1;
    outOy = (int)std::floor(minY) - 1;
    const int ow = (int)std::ceil(maxX) - outOx + 1;
    const int oh = (int)std::ceil(maxY) - outOy + 1;
    if (ow <= 0 || oh <= 0 || ow > 20000 || oh > 20000) return Image();

    Image out(ow, oh);
    const int sw = src.width(), sh = src.height();
    if (sw <= 0 || sh <= 0) return out;
    const uint32_t* sp = src.pixels();
    uint32_t* dp = out.pixels();
    const float sx0 = (float)srcRect.x, sx1 = (float)srcRect.right();
    const float sy0 = (float)srcRect.y, sy1 = (float)srcRect.bottom();

    if (fastPreview) {
        const float f[9] = {(float)hi[0], (float)hi[1], (float)hi[2], (float)hi[3], (float)hi[4],
                            (float)hi[5], (float)hi[6], (float)hi[7], (float)hi[8]};
        const float fx = (float)oldX, fy = (float)oldY;
        const int ux0 = std::max(0, (int)sx0), ux1 = std::min(sw, (int)sx1);
        const int uy0 = std::max(0, (int)sy0), uy1 = std::min(sh, (int)sy1);
        for (int y = 0; y < oh; ++y) {
            uint32_t* drow = dp + (size_t)y * (size_t)ow;
            const float cy = (float)(outOy + y) + 0.5f;
            for (int x = 0; x < ow; ++x) {
                const float cx = (float)(outOx + x) + 0.5f;
                const float w = f[6] * cx + f[7] * cy + f[8];
                if (w < 1e-6f && w > -1e-6f) {
                    drow[x] = 0;
                    continue;
                }
                const float u = (f[0] * cx + f[1] * cy + f[2]) / w - fx;
                const float v = (f[3] * cx + f[4] * cy + f[5]) / w - fy;
                const int iu = (int)u, iv = (int)v;
                if (iu < ux0 || iu >= ux1 || iv < uy0 || iv >= uy1 || u < sx0 || v < sy0) {
                    drow[x] = 0;
                    continue;
                }
                drow[x] = sp[(size_t)iv * (size_t)sw + (size_t)iu];
            }
        }
        return out;
    }

    for (int y = 0; y < oh; ++y) {
        uint32_t* drow = dp + (size_t)y * (size_t)ow;
        const double cy = (double)(outOy + y) + 0.5;
        for (int x = 0; x < ow; ++x) {
            const double cx = (double)(outOx + x) + 0.5;
            const double w = hi[6] * cx + hi[7] * cy + hi[8];
            if (std::abs(w) < 1e-12) continue;
            const double u = (hi[0] * cx + hi[1] * cy + hi[2]) / w - (double)oldX;
            const double v = (hi[3] * cx + hi[4] * cy + hi[5]) / w - (double)oldY;
            const uint32_t px = bilinearSample(sp, sw, sh, (float)u, (float)v, sx0, sx1, sy0, sy1);
            if ((px >> 24) == 0) continue;
            drow[x] = px;
        }
    }
    return out;
}

namespace {

uint32_t unpremulOne(uint32_t v)
{
    const uint32_t a = v >> 24;
    if (a == 0u || a == 255u) return v;
    const uint32_t r = v & 255u, g = (v >> 8) & 255u, b = (v >> 16) & 255u;
    const uint32_t rr = std::min(255u, (r * 255u + a / 2u) / a);
    const uint32_t gg = std::min(255u, (g * 255u + a / 2u) / a);
    const uint32_t bb = std::min(255u, (b * 255u + a / 2u) / a);
    return rr | (gg << 8) | (bb << 16) | (a << 24);
}

void unpremulAll(Image& img)
{
    uint32_t* p = img.pixels();
    const std::size_t n = (std::size_t)img.width() * (std::size_t)img.height();
    for (std::size_t i = 0; i < n; ++i) p[i] = unpremulOne(p[i]);
}

}

Image rasterizeNearest(const Image& src, Rect srcRect, const Rect& srcCanvasRect, const XForm& xf, int oldX,
                       int oldY, int outOx, int outOy, int ow, int oh)
{
    Image out(ow, oh);
    const int sw = src.width(), sh = src.height();
    if (sw <= 0 || sh <= 0) return out;
    const uint32_t* sp = src.pixels();
    uint32_t* dp = out.pixels();

    const float invSx = 1.0f / std::max(1e-4f, xf.sx);
    const float invSy = 1.0f / std::max(1e-4f, xf.sy);
    const float c = std::cos(-xf.rot), s = std::sin(-xf.rot);
    const float srcCx = (float)srcCanvasRect.x + (float)srcCanvasRect.w * 0.5f;
    const float srcCy = (float)srcCanvasRect.y + (float)srcCanvasRect.h * 0.5f;
    const float sx0 = (float)srcRect.x, sx1 = (float)srcRect.right();
    const float sy0 = (float)srcRect.y, sy1 = (float)srcRect.bottom();

    const float du = c * invSx, dv = s * invSy;
    const float tx0 = (float)outOx + 0.5f - xf.cx;
    const int64_t duf = (int64_t)std::llround((double)du * 65536.0);
    const int64_t dvf = (int64_t)std::llround((double)dv * 65536.0);

    for (int y = 0; y < oh; ++y) {
        const float ty = (float)(outOy + y) + 0.5f - xf.cy;
        const float rx0 = c * tx0 - s * ty;
        const float ry0 = s * tx0 + c * ty;
        const float u0 = rx0 * invSx + srcCx - (float)oldX;
        const float v0 = ry0 * invSy + srcCy - (float)oldY;

        int xa = 0, xb = ow - 1;
        clipSpan(u0, du, sx0 - 1.0f, sx1 + 1.0f, xa, xb);
        clipSpan(v0, dv, sy0 - 1.0f, sy1 + 1.0f, xa, xb);
        if (xa > xb) continue;

        const int64_t u0f = (int64_t)std::llround((double)u0 * 65536.0);
        const int64_t v0f = (int64_t)std::llround((double)v0 * 65536.0);
        uint32_t* row = dp + (size_t)y * (size_t)ow;
        for (int x = xa; x <= xb; ++x) {
            const int ix = (int)((u0f + duf * (int64_t)x) >> 16);
            const int iy = (int)((v0f + dvf * (int64_t)x) >> 16);
            if (ix < 0 || iy < 0 || ix >= sw || iy >= sh) continue;
            row[x] = sp[(size_t)iy * (size_t)sw + (size_t)ix];
        }
    }
    return out;
}

void xformQuad(const Rect& srcCanvasRect, const XForm& xf, Vec2f out[4])
{
    const float c = std::cos(xf.rot), s = std::sin(xf.rot);
    const float hw = (float)srcCanvasRect.w * 0.5f * xf.sx;
    const float hh = (float)srcCanvasRect.h * 0.5f * xf.sy;
    const float dx[4] = {-hw, hw, hw, -hw};
    const float dy[4] = {-hh, -hh, hh, hh};
    for (int i = 0; i < 4; ++i) {
        out[i] = Vec2f{xf.cx + dx[i] * c - dy[i] * s, xf.cy + dx[i] * s + dy[i] * c};
    }
}

Image xformRasterize(const Image& src, Rect srcRect, const XForm& xf, int oldX, int oldY, int& outOx, int& outOy,
                     bool fastPreview)
{
    outOx = outOy = 0;
    if (src.empty() || srcRect.empty()) return Image();

    const Rect srcCanvasRect{oldX + srcRect.x, oldY + srcRect.y, srcRect.w, srcRect.h};
    Vec2f q[4];
    xformQuad(srcCanvasRect, xf, q);
    float minX = q[0].x, minY = q[0].y, maxX = q[0].x, maxY = q[0].y;
    for (int i = 1; i < 4; ++i) {
        minX = std::min(minX, q[i].x);
        minY = std::min(minY, q[i].y);
        maxX = std::max(maxX, q[i].x);
        maxY = std::max(maxY, q[i].y);
    }
    outOx = (int)std::floor(minX) - 1;
    outOy = (int)std::floor(minY) - 1;
    const int ow = (int)std::ceil(maxX) - outOx + 1;
    const int oh = (int)std::ceil(maxY) - outOy + 1;
    if (ow <= 0 || oh <= 0) return Image();

    if (fastPreview) return rasterizeNearest(src, srcRect, srcCanvasRect, xf, oldX, oldY, outOx, outOy, ow, oh);

    if (xf.rot == 0.0f) return rasterizeAxial(src, srcRect, srcCanvasRect, xf, oldX, oldY, outOx, outOy, ow, oh);

    Image out(ow, oh);
    const int sw = src.width(), sh = src.height();
    const uint32_t* sp = src.pixels();
    uint32_t* dp = out.pixels();

    const float invSx = 1.0f / std::max(1e-4f, xf.sx);
    const float invSy = 1.0f / std::max(1e-4f, xf.sy);
    const float c = std::cos(-xf.rot), s = std::sin(-xf.rot);
    const float srcCx = (float)srcCanvasRect.x + (float)srcCanvasRect.w * 0.5f;
    const float srcCy = (float)srcCanvasRect.y + (float)srcCanvasRect.h * 0.5f;
    const float sx0 = (float)srcRect.x, sx1 = (float)srcRect.right();
    const float sy0 = (float)srcRect.y, sy1 = (float)srcRect.bottom();
    const float oldXf = (float)oldX, oldYf = (float)oldY;

    const float du = c * invSx;
    const float dv = s * invSy;
    const float tx0 = (float)outOx + 0.5f - xf.cx;

    for (int y = 0; y < oh; ++y) {
        const float ty = (float)(outOy + y) + 0.5f - xf.cy;
        const float rx0 = c * tx0 - s * ty;
        const float ry0 = s * tx0 + c * ty;
        const float u0 = rx0 * invSx + srcCx - oldXf;
        const float v0 = ry0 * invSy + srcCy - oldYf;

        int xa = 0, xb = ow - 1;
        clipSpan(u0, du, sx0 - 1.0f, sx1 + 1.0f, xa, xb);
        clipSpan(v0, dv, sy0 - 1.0f, sy1 + 1.0f, xa, xb);

        uint32_t* row = dp + (size_t)y * (size_t)ow;
        if (xa > xb) continue;
        for (int x = xa; x <= xb; ++x)
            row[x] = bilinearSample(sp, sw, sh, u0 + du * (float)x, v0 + dv * (float)x, sx0, sx1, sy0, sy1);
    }
    return out;
}


SkMatrix xformMatrix(const Rect& srcCanvasRect, const XForm& xf)
{
    return xformMatrixAbout((float)srcCanvasRect.x + (float)srcCanvasRect.w * 0.5f,
                            (float)srcCanvasRect.y + (float)srcCanvasRect.h * 0.5f, xf);
}

SkMatrix xformMatrix(const SkRect& srcCanvasRect, const XForm& xf)
{
    return xformMatrixAbout(srcCanvasRect.centerX(), srcCanvasRect.centerY(), xf);
}

SkMatrix xformMatrixAbout(float srcCx, float srcCy, const XForm& xf)
{
    const float c0x = srcCx;
    const float c0y = srcCy;
    SkMatrix pre;
    pre.setTranslate(-c0x, -c0y);
    SkMatrix scl;
    scl.setScale(xf.sx, xf.sy);
    SkMatrix rot;
    rot.setRotate(xf.rot * 180.0f / 3.1415927f);
    SkMatrix post;
    post.setTranslate(xf.cx, xf.cy);
    SkMatrix m1;
    m1.setConcat(rot, scl);
    SkMatrix m2;
    m2.setConcat(m1, pre);
    SkMatrix m;
    m.setConcat(post, m2);
    return m;
}

static void c1Weights(float t, float w[4])
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    w[0] = 0.5f * (-t3 + 2.0f * t2 - t);
    w[1] = 0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f);
    w[2] = 0.5f * (-3.0f * t3 + 4.0f * t2 + t);
    w[3] = 0.5f * (t3 - t2);
}

static Vec2f c1Grid(const std::vector<Vec2f>& p, int n, int i, int j)
{
    const auto rowAt = [&](int col, int row) -> Vec2f {
        if (col < 0) {
            const Vec2f a = p[(size_t)row * (size_t)(n + 1)];
            const Vec2f b = p[(size_t)row * (size_t)(n + 1) + 1];
            return Vec2f{2.0f * a.x - b.x, 2.0f * a.y - b.y};
        }
        if (col > n) {
            const Vec2f a = p[(size_t)row * (size_t)(n + 1) + (size_t)n];
            const Vec2f b = p[(size_t)row * (size_t)(n + 1) + (size_t)(n - 1)];
            return Vec2f{2.0f * a.x - b.x, 2.0f * a.y - b.y};
        }
        return p[(size_t)row * (size_t)(n + 1) + (size_t)col];
    };
    if (j < 0) {
        const Vec2f a = rowAt(i, 0);
        const Vec2f b = rowAt(i, 1);
        return Vec2f{2.0f * a.x - b.x, 2.0f * a.y - b.y};
    }
    if (j > n) {
        const Vec2f a = rowAt(i, n);
        const Vec2f b = rowAt(i, n - 1);
        return Vec2f{2.0f * a.x - b.x, 2.0f * a.y - b.y};
    }
    return rowAt(i, j);
}

float meshC1BumpWeight(const Vec2f& param, float gu, float gv)
{
    int ci = (int)std::floor(param.x);
    int cj = (int)std::floor(param.y);
    if (ci < 0) ci = 0;
    if (cj < 0) cj = 0;
    if (gu < (float)ci || gu > (float)(ci + 1) || gv < (float)cj || gv > (float)(cj + 1)) return 0.0f;
    const float tu = gu - (float)ci;
    const float tv = gv - (float)cj;
    const float a = 4.0f * tu * (1.0f - tu);
    const float b = 4.0f * tv * (1.0f - tv);
    const float a3 = a * a * a;
    const float b3 = b * b * b;
    return a3 * b3;
}

Vec2f meshC1Surface(const std::vector<Vec2f>& pts, int n, const std::vector<Vec2f>& extraParam,
                    const std::vector<Vec2f>& extraDisp, float gu, float gv)
{
    if (n < 1 || (int)pts.size() < (n + 1) * (n + 1)) return Vec2f{};
    if (gu < 0.0f) gu = 0.0f;
    if (gv < 0.0f) gv = 0.0f;
    if (gu > (float)n) gu = (float)n;
    if (gv > (float)n) gv = (float)n;
    int i = (int)std::floor(gu);
    int j = (int)std::floor(gv);
    if (i > n - 1) i = n - 1;
    if (j > n - 1) j = n - 1;
    if (i < 0) i = 0;
    if (j < 0) j = 0;
    float wu[4], wv[4];
    c1Weights(gu - (float)i, wu);
    c1Weights(gv - (float)j, wv);
    float x = 0.0f, y = 0.0f;
    for (int a = 0; a < 4; ++a) {
        for (int b = 0; b < 4; ++b) {
            const Vec2f q = c1Grid(pts, n, i - 1 + a, j - 1 + b);
            const float w = wu[a] * wv[b];
            x += q.x * w;
            y += q.y * w;
        }
    }
    for (size_t k = 0; k < extraParam.size() && k < extraDisp.size(); ++k) {
        const float bw = meshC1BumpWeight(extraParam[k], gu, gv);
        if (bw == 0.0f) continue;
        x += extraDisp[k].x * bw;
        y += extraDisp[k].y * bw;
    }
    return Vec2f{x, y};
}

bool meshC1Inverse(const std::vector<Vec2f>& pts, int n, const Vec2f& p, float& gu, float& gv)
{
    gu = gv = 0.0f;
    if (n < 1 || (int)pts.size() < (n + 1) * (n + 1)) return false;
    float bestRes = 1e30f;
    float bu = 0.0f, bv = 0.0f;
    bool found = false;
    for (int cj = 0; cj < n; ++cj) {
        for (int ci = 0; ci < n; ++ci) {
            float u = (float)ci + 0.5f;
            float v = (float)cj + 0.5f;
            for (int it = 0; it < 8; ++it) {
                const Vec2f q = meshC1Surface(pts, n, {}, {}, u, v);
                const float fx = q.x - p.x, fy = q.y - p.y;
                const float lo = 1e-4f;
                float up = u + 0.005f, um = u - 0.005f;
                if (up > (float)(ci + 1) - lo) up = (float)(ci + 1) - lo;
                if (um < (float)ci + lo) um = (float)ci + lo;
                float vp = v + 0.005f, vm = v - 0.005f;
                if (vp > (float)(cj + 1) - lo) vp = (float)(cj + 1) - lo;
                if (vm < (float)cj + lo) vm = (float)cj + lo;
                const Vec2f a = meshC1Surface(pts, n, {}, {}, up, v);
                const Vec2f b = meshC1Surface(pts, n, {}, {}, um, v);
                const Vec2f e = meshC1Surface(pts, n, {}, {}, u, vp);
                const Vec2f f = meshC1Surface(pts, n, {}, {}, u, vm);
                const float dux = (a.x - b.x) / (up - um), duy = (a.y - b.y) / (up - um);
                const float dvx = (e.x - f.x) / (vp - vm), dvy = (e.y - f.y) / (vp - vm);
                const float det = dux * dvy - duy * dvx;
                if (det > -1e-9f && det < 1e-9f) break;
                const float su = (fx * dvy - fy * dvx) / det;
                const float sv = (dux * fy - duy * fx) / det;
                u -= su;
                v -= sv;
                if (u < (float)ci + lo) u = (float)ci + lo;
                if (u > (float)(ci + 1) - lo) u = (float)(ci + 1) - lo;
                if (v < (float)cj + lo) v = (float)cj + lo;
                if (v > (float)(cj + 1) - lo) v = (float)(cj + 1) - lo;
                if (std::fabs(su) < 1e-4f && std::fabs(sv) < 1e-4f) break;
            }
            const Vec2f q = meshC1Surface(pts, n, {}, {}, u, v);
            const float res = std::hypot(q.x - p.x, q.y - p.y);
            if (!found || res < bestRes) {
                found = true;
                bestRes = res;
                bu = u;
                bv = v;
            }
        }
    }
    if (!found) return false;
    gu = bu;
    gv = bv;
    return true;
}

Image meshC1Rasterize(const Image& src, Rect srcRect, int oldX, int oldY, const std::vector<Vec2f>& pts, int n,
                      const std::vector<Vec2f>& extraParam, const std::vector<Vec2f>& extraDisp, int& outOx,
                      int& outOy, bool highQuality, int sub)
{
    outOx = outOy = 0;
    if (src.empty() || srcRect.empty() || n < 2) return Image();
    if ((int)pts.size() < (n + 1) * (n + 1)) return Image();
    if (sub < 1) sub = 1;
    if (sub > 8) sub = 8;
    const int rs = n * sub;

    float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
    for (int j = 0; j <= rs; ++j) {
        for (int i = 0; i <= rs; ++i) {
            const Vec2f q = meshC1Surface(pts, n, extraParam, extraDisp, (float)i / (float)sub,
                                         (float)j / (float)sub);
            minX = std::min(minX, q.x);
            maxX = std::max(maxX, q.x);
            minY = std::min(minY, q.y);
            maxY = std::max(maxY, q.y);
        }
    }
    outOx = (int)std::floor(minX) - 1;
    outOy = (int)std::floor(minY) - 1;
    const int ow = (int)std::ceil(maxX) - outOx + 1;
    const int oh = (int)std::ceil(maxY) - outOy + 1;
    if (ow <= 0 || oh <= 0 || ow > 20000 || oh > 20000) return Image();

    std::vector<SkPoint> pos, uv;
    pos.reserve((size_t)rs * (size_t)rs * 6u);
    uv.reserve(pos.capacity());
    const auto sample = [&](int i, int j, SkPoint& sp, SkPoint& st) {
        const float u = (float)i / (float)sub;
        const float v = (float)j / (float)sub;
        const Vec2f q = meshC1Surface(pts, n, extraParam, extraDisp, u, v);
        sp = SkPoint::Make(q.x, q.y);
        st = SkPoint::Make((float)srcRect.x + (float)srcRect.w * u / (float)n,
                           (float)srcRect.y + (float)srcRect.h * v / (float)n);
    };
    for (int j = 0; j < rs; ++j) {
        for (int i = 0; i < rs; ++i) {
            SkPoint p00, p10, p11, p01, t00, t10, t11, t01;
            sample(i, j, p00, t00);
            sample(i + 1, j, p10, t10);
            sample(i + 1, j + 1, p11, t11);
            sample(i, j + 1, p01, t01);
            const SkPoint pa[6] = {p00, p10, p11, p00, p11, p01};
            const SkPoint ta[6] = {t00, t10, t11, t00, t11, t01};
            for (int k = 0; k < 6; ++k) {
                pos.push_back(pa[k]);
                uv.push_back(ta[k]);
            }
        }
    }
    if (pos.empty()) return Image();

    const int ss = highQuality ? 2 : 1;
    const int bw = ow * ss, bh = oh * ss;
    Image big(bw, bh);
    SkImageInfo info = SkImageInfo::Make(bw, bh, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    std::unique_ptr<SkCanvas> cv = SkCanvas::MakeRasterDirect(info, big.pixels(), (size_t)bw * 4);
    if (!cv) return Image();
    SkImageInfo texInfo =
        SkImageInfo::Make(src.width(), src.height(), kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    sk_sp<SkData> data = SkData::MakeWithoutCopy(src.data(), (size_t)src.width() * (size_t)src.height() * 4);
    if (!data) return Image();
    sk_sp<SkImage> tex = SkImages::RasterFromData(texInfo, data, (size_t)src.width() * 4);
    if (!tex) return Image();
    SkPaint paint;
    paint.setAntiAlias(false);
    paint.setShader(tex->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                    SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone)));
    std::vector<SkPoint> sp(pos.size());
    for (size_t i = 0; i < pos.size(); ++i)
        sp[i] = SkPoint::Make((pos[i].fX - (float)outOx) * (float)ss, (pos[i].fY - (float)outOy) * (float)ss);
    sk_sp<SkVertices> scaled =
        SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, (int)sp.size(), sp.data(), uv.data(), nullptr);
    if (!scaled) return Image();
    cv->drawVertices(scaled, SkBlendMode::kSrcOver, paint);
    cv.reset();

    Image out(ow, oh);
    const uint32_t* bp = big.pixels();
    uint32_t* op = out.pixels();
    if (ss == 1) {
        const std::size_t cnt = (std::size_t)ow * (std::size_t)oh;
        for (std::size_t i = 0; i < cnt; ++i) op[i] = bp[i];
    } else {
        for (int y = 0; y < oh; ++y) {
            for (int x = 0; x < ow; ++x) {
                const uint32_t* r0 = bp + (size_t)(y * 2) * (size_t)bw + (size_t)(x * 2);
                const uint32_t* r1 = r0 + (size_t)bw;
                const uint32_t v[4] = {r0[0], r0[1], r1[0], r1[1]};
                uint32_t sr = 0, sg = 0, sb = 0, sa = 0;
                for (int i = 0; i < 4; ++i) {
                    sr += v[i] & 255u;
                    sg += (v[i] >> 8) & 255u;
                    sb += (v[i] >> 16) & 255u;
                    sa += v[i] >> 24;
                }
                op[(size_t)y * (size_t)ow + (size_t)x] =
                    (sr / 4u) | ((sg / 4u) << 8) | ((sb / 4u) << 16) | ((sa / 4u) << 24);
            }
        }
    }
    unpremulAll(out);
    return out;
}


}
