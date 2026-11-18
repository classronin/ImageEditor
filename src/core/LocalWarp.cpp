#include "core/LocalWarp.h"

#include <algorithm>
#include <cmath>

namespace ie {

namespace {

constexpr float kPiF = 3.14159265358979f;

struct Frame {
    float cosA = 1.0f;
    float sinA = 0.0f;
    Vec2f c{};
    Vec2f r{1.0f, 1.0f};
    float ampX = 0.0f;
    float ampY = 0.0f;
    float invRx = 1.0f;
    float invRy = 1.0f;
    float poisson = 0.0f;
    int harmonic = 0;
    float falloff = 1.0f;

    Vec2f toLocal(const Vec2f& p) const
    {
        const float dx = p.x - c.x;
        const float dy = p.y - c.y;
        return Vec2f{dx * cosA + dy * sinA, -dx * sinA + dy * cosA};
    }

    Vec2f toGlobal(const Vec2f& d) const
    {
        return Vec2f{d.x * cosA - d.y * sinA, d.x * sinA + d.y * cosA};
    }

    float normRadius(const Vec2f& d) const
    {
        const float nx = d.x / (r.x > 1.0f ? r.x : 1.0f);
        const float ny = d.y / (r.y > 1.0f ? r.y : 1.0f);
        return std::sqrt(nx * nx + ny * ny);
    }

    Vec2f offset(const Vec2f& d) const
    {
        const float nx = d.x * invRx, ny = d.y * invRy;
        const float r2 = nx * nx + ny * ny;
        if (r2 >= 1.0f) return Vec2f{0.0f, 0.0f};
        const float base = 1.0f - std::sqrt(r2);
        const float sm = base * base * (3.0f - 2.0f * base);
        const float fo = falloff < 0.2f ? 0.2f : falloff;
        const float w = (fo == 1.0f) ? sm : (fo == 2.0f ? sm * sm : std::pow(sm, fo));
        Vec2f dl{nx * ampX * w, ny * ampY * w};
        if (poisson != 0.0f && harmonic > 0) {
            const float len2 = d.x * d.x + d.y * d.y;
            if (len2 > 1e-10f) {
                const float len = std::sqrt(len2);
                const float dirX = d.x / len, dirY = d.y / len;
                const float radial = dl.x * dirX + dl.y * dirY;
                float u = 1.0f;
                if (harmonic == 2) {
                    u = 2.0f * dirX;
                } else if (harmonic > 2) {
                    float prev = 1.0f, cur = 2.0f * dirX;
                    for (int k = 3; k <= harmonic; ++k) {
                        const float nxt = 2.0f * dirX * cur - prev;
                        prev = cur;
                        cur = nxt;
                    }
                    u = cur;
                }
                const float twist = radial * poisson * dirY * u;
                dl.x += -dirY * twist;
                dl.y += dirX * twist;
            }
        }
        return dl;
    }

    float weight(const Vec2f& d) const
    {
        const float r = normRadius(d);
        const float base = std::clamp(1.0f - r, 0.0f, 1.0f);
        const float sm = base * base * (3.0f - 2.0f * base);
        const float fo = falloff < 0.2f ? 0.2f : falloff;
        return std::pow(sm, fo);
    }

    Vec2f displacement(const Vec2f& d, float w) const
    {
        const float nx = d.x / (r.x > 1.0f ? r.x : 1.0f);
        const float ny = d.y / (r.y > 1.0f ? r.y : 1.0f);
        Vec2f dl{nx * ampX, ny * ampY};
        dl.x *= w;
        dl.y *= w;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (poisson != 0.0f && harmonic > 0 && len > 1e-5f) {
            const float dirX = d.x / len;
            const float dirY = d.y / len;
            const float radial = dl.x * dirX + dl.y * dirY;
            const float phase = (float)harmonic * std::atan2(d.y, d.x);
            const float s = radial * poisson * std::sin(phase) * w;
            dl.x += -dirY * s;
            dl.y += dirX * s;
        }
        return dl;
    }
};

Frame makeFrame(const LocalWarpParams& p)
{
    Frame f;
    const float a = p.rotDeg * kPiF / 180.0f;
    f.cosA = std::cos(a);
    f.sinA = std::sin(a);
    f.c = p.center;
    f.r = p.radius;
    f.ampX = p.amp.x;
    f.ampY = p.amp.y;
    f.invRx = 1.0f / (p.radius.x > 1.0f ? p.radius.x : 1.0f);
    f.invRy = 1.0f / (p.radius.y > 1.0f ? p.radius.y : 1.0f);
    f.poisson = p.poisson;
    f.harmonic = p.harmonic;
    f.falloff = p.falloff;
    return f;
}

uint32_t premulBilinear(const uint32_t* sp, int sw, int sh, float u, float v)
{
    const float fx = u - 0.5f, fy = v - 0.5f;
    const int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
    const float tx = fx - (float)ix, ty = fy - (float)iy;
    const int wx1 = (int)(tx * 256.0f), wx0 = 256 - wx1;
    const int wy1 = (int)(ty * 256.0f), wy0 = 256 - wy1;
    const int x1 = ix + 1, y1 = iy + 1;
    const bool in00 = ix >= 0 && iy >= 0 && ix < sw && iy < sh;
    const bool in10 = x1 >= 0 && iy >= 0 && x1 < sw && iy < sh;
    const bool in01 = ix >= 0 && y1 >= 0 && ix < sw && y1 < sh;
    const bool in11 = x1 >= 0 && y1 >= 0 && x1 < sw && y1 < sh;
    if (!in00 && !in10 && !in01 && !in11) return 0;
    const size_t o00 = (size_t)iy * (size_t)sw + (size_t)ix;
    const size_t o10 = (size_t)iy * (size_t)sw + (size_t)x1;
    const size_t o01 = (size_t)y1 * (size_t)sw + (size_t)ix;
    const size_t o11 = (size_t)y1 * (size_t)sw + (size_t)x1;
    const uint32_t p00 = in00 ? sp[o00] : 0;
    const uint32_t p10 = in10 ? sp[o10] : 0;
    const uint32_t p01 = in01 ? sp[o01] : 0;
    const uint32_t p11 = in11 ? sp[o11] : 0;
    const int w00 = wx0 * wy0, w10 = wx1 * wy0, w01 = wx0 * wy1, w11 = wx1 * wy1;
    const int t00 = w00 * (int)(p00 >> 24), t10 = w10 * (int)(p10 >> 24);
    const int t01 = w01 * (int)(p01 >> 24), t11 = w11 * (int)(p11 >> 24);
    const int tot = t00 + t10 + t01 + t11;
    if (tot <= 0) return 0;
    const int r = (t00 * (int)(p00 & 0xff) + t10 * (int)(p10 & 0xff) + t01 * (int)(p01 & 0xff) +
                   t11 * (int)(p11 & 0xff)) / tot;
    const int g = (t00 * (int)((p00 >> 8) & 0xff) + t10 * (int)((p10 >> 8) & 0xff) +
                   t01 * (int)((p01 >> 8) & 0xff) + t11 * (int)((p11 >> 8) & 0xff)) / tot;
    const int b = (t00 * (int)((p00 >> 16) & 0xff) + t10 * (int)((p10 >> 16) & 0xff) +
                   t01 * (int)((p01 >> 16) & 0xff) + t11 * (int)((p11 >> 16) & 0xff)) / tot;
    const int a = tot / 65536;
    return ((uint32_t)(a & 0xff) << 24) | ((uint32_t)(b & 0xff) << 16) | ((uint32_t)(g & 0xff) << 8) |
           (uint32_t)(r & 0xff);
}

}

bool localWarpHasRegion(const LocalWarpParams& p)
{
    return p.radius.x > 1.0f && p.radius.y > 1.0f;
}

bool localWarpHasEffect(const LocalWarpParams& p)
{
    return localWarpHasRegion(p) && (std::fabs(p.amp.x) > 0.05f || std::fabs(p.amp.y) > 0.05f);
}

Vec2f localWarpField(const LocalWarpParams& p, const Vec2f& at)
{
    const Frame f = makeFrame(p);
    const Vec2f d = f.toLocal(at);
    return f.toGlobal(f.offset(d));
}

Rect localWarpRegionBounds(const LocalWarpParams& p)
{
    const float a = p.rotDeg * kPiF / 180.0f;
    const float ca = std::fabs(std::cos(a));
    const float sa = std::fabs(std::sin(a));
    const float ex = ca * p.radius.x + sa * p.radius.y;
    const float ey = sa * p.radius.x + ca * p.radius.y;
    return Rect{(int)std::floor(p.center.x - ex), (int)std::floor(p.center.y - ey),
                (int)std::ceil(2.0f * ex), (int)std::ceil(2.0f * ey)};
}

Image localWarpApplyAll(const Image& layer, int layerX, int layerY, const std::vector<LocalWarpParams>& ps,
                        int& outX, int& outY)
{
    outX = layerX;
    outY = layerY;
    if (layer.empty() || ps.empty()) return Image();

    std::vector<Frame> frames;
    frames.reserve(ps.size());
    int minX = layerX, minY = layerY;
    int maxX = layerX + layer.width();
    int maxY = layerY + layer.height();
    for (const LocalWarpParams& p : ps) {
        if (!localWarpHasRegion(p)) continue;
        frames.push_back(makeFrame(p));
        const Rect region = localWarpRegionBounds(p);
        if (region.empty()) continue;
        const float margin = std::max(std::fabs(p.amp.x), std::fabs(p.amp.y)) + 2.0f;
        minX = std::min(minX, (int)std::floor((float)region.x - margin));
        minY = std::min(minY, (int)std::floor((float)region.y - margin));
        maxX = std::max(maxX, (int)std::ceil((float)region.right() + margin));
        maxY = std::max(maxY, (int)std::ceil((float)region.bottom() + margin));
    }
    if (frames.empty()) return Image();

    const int ow = maxX - minX;
    const int oh = maxY - minY;
    if (ow <= 0 || oh <= 0 || ow > 20000 || oh > 20000) return Image();

    outX = minX;
    outY = minY;
    Image out(ow, oh);
    uint32_t* op = out.pixels();
    const uint32_t* sp = layer.pixels();
    const int sw = layer.width();
    const int sh = layer.height();
    for (int y = 0; y < oh; ++y) {
        for (int x = 0; x < ow; ++x) {
            const Vec2f doc{(float)(minX + x) + 0.5f, (float)(minY + y) + 0.5f};
            Vec2f disp{};
            for (const Frame& f : frames) {
                const Vec2f dl = f.toGlobal(f.offset(f.toLocal(doc)));
                disp.x += dl.x;
                disp.y += dl.y;
            }
            if (disp.x == 0.0f && disp.y == 0.0f) {
                const int bx = minX + x - layerX, by = minY + y - layerY;
                op[(size_t)y * (size_t)ow + (size_t)x] =
                    (bx >= 0 && by >= 0 && bx < sw && by < sh) ? sp[(size_t)by * (size_t)sw + (size_t)bx] : 0;
                continue;
            }
            const float sx = doc.x - disp.x - (float)layerX;
            const float sy = doc.y - disp.y - (float)layerY;
            op[(size_t)y * (size_t)ow + (size_t)x] = premulBilinear(sp, sw, sh, sx, sy);
        }
    }
    return out;
}

Rect localWarpPaintBounds(const LocalWarpParams& p)
{
    const Rect r = localWarpRegionBounds(p);
    const float margin = std::max(std::fabs(p.amp.x), std::fabs(p.amp.y)) + 2.0f;
    const int m = (int)std::ceil(margin);
    return Rect{r.x - m, r.y - m, r.w + 2 * m, r.h + 2 * m};
}

void localWarpBlitDocRect(const Image& src, int srcX, int srcY, uint32_t* dst, int dstW, int dstH,
                          int dstX, int dstY, Rect docRect)
{
    if (!dst || src.empty()) return;
    const int x0 = std::max(docRect.x, dstX);
    const int y0 = std::max(docRect.y, dstY);
    const int x1 = std::min(docRect.right(), dstX + dstW);
    const int y1 = std::min(docRect.bottom(), dstY + dstH);
    const uint32_t* sp = src.pixels();
    const int sw = src.width();
    const int sh = src.height();
    for (int y = y0; y < y1; ++y) {
        const int syRow = y - srcY;
        uint32_t* drow = dst + (size_t)(y - dstY) * (size_t)dstW;
        for (int x = x0; x < x1; ++x) {
            const int sx = x - srcX;
            if (sx < 0 || syRow < 0 || sx >= sw || syRow >= sh) {
                drow[x - dstX] = 0;
            } else {
                drow[x - dstX] = sp[(size_t)syRow * (size_t)sw + (size_t)sx];
            }
        }
    }
}

bool LocalWarpField::matches(const LocalWarpParams& p) const
{
    return w > 0 && rx == p.radius.x && ry == p.radius.y && tab.size() >= (size_t)(w * h * 2) &&
           base_ampX == p.amp.x && base_ampY == p.amp.y && base_poisson == p.poisson &&
           base_harmonic == p.harmonic && base_falloff == p.falloff;
}

void LocalWarpField::build(const LocalWarpParams& p)
{
    const Frame f = makeFrame(p);
    rx = p.radius.x > 1.0f ? p.radius.x : 1.0f;
    ry = p.radius.y > 1.0f ? p.radius.y : 1.0f;
    base_ampX = p.amp.x;
    base_ampY = p.amp.y;
    base_poisson = p.poisson;
    base_harmonic = p.harmonic;
    base_falloff = p.falloff;
    const float stepV = 2.0f;
    step = stepV;
    ox = -(rx + stepV);
    oy = -(ry + stepV);
    w = (int)std::ceil((rx + stepV) * 2.0f / stepV) + 1;
    h = (int)std::ceil((ry + stepV) * 2.0f / stepV) + 1;
    tab.assign((size_t)w * (size_t)h * 2, 0.0f);
    for (int y = 0; y < h; ++y) {
        const float ly = oy + (float)y * stepV;
        for (int x = 0; x < w; ++x) {
            const float lx = ox + (float)x * stepV;
            const Vec2f d = f.offset(Vec2f{lx, ly});
            tab[((size_t)y * (size_t)w + (size_t)x) * 2] = d.x;
            tab[((size_t)y * (size_t)w + (size_t)x) * 2 + 1] = d.y;
        }
    }
}

Vec2f LocalWarpField::sample(float lx, float ly) const
{
    if (w <= 1 || h <= 1) return Vec2f{0.0f, 0.0f};
    const float gx = (lx - ox) / step;
    const float gy = (ly - oy) / step;
    if (gx < 0.0f || gy < 0.0f || gx > (float)(w - 1) || gy > (float)(h - 1)) return Vec2f{0.0f, 0.0f};
    const int ix = (int)gx, iy = (int)gy;
    const float tx = gx - (float)ix, ty = gy - (float)iy;
    const int x1 = ix < w - 1 ? ix + 1 : ix;
    const int y1 = iy < h - 1 ? iy + 1 : iy;
    const size_t row0 = (size_t)iy * (size_t)w, row1 = (size_t)y1 * (size_t)w;
    const size_t i00 = (row0 + (size_t)ix) * 2, i10 = (row0 + (size_t)x1) * 2;
    const size_t i01 = (row1 + (size_t)ix) * 2, i11 = (row1 + (size_t)x1) * 2;
    const float ux = 1.0f - tx, uy = 1.0f - ty;
    const float a0 = tab[i00], b0 = tab[i10], a1 = tab[i01], b1 = tab[i11];
    const float c0 = tab[i00 + 1], c1 = tab[i10 + 1], c2 = tab[i01 + 1], c3 = tab[i11 + 1];
    return Vec2f{(a0 * ux + b0 * tx) * uy + (a1 * ux + b1 * tx) * ty,
                 (c0 * ux + c1 * tx) * uy + (c2 * ux + c3 * tx) * ty};
}

void localWarpWarpDocRectFields(const Image& src, int srcX, int srcY, const std::vector<LocalWarpParams>& ps,
                                const std::vector<LocalWarpField>& fs, uint32_t* dst, int dstW, int dstH,
                                int dstX, int dstY, Rect docRect)
{
    if (!dst || src.empty() || ps.size() != fs.size()) return;
    std::vector<float> cosA(ps.size(), 1.0f), sinA(ps.size(), 0.0f), rx(ps.size(), 1.0f), ry(ps.size(), 1.0f);
    std::vector<Vec2f> centers(ps.size());
    for (size_t i = 0; i < ps.size(); ++i) {
        const float a = ps[i].rotDeg * 3.14159265f / 180.0f;
        cosA[i] = std::cos(a);
        sinA[i] = std::sin(a);
        rx[i] = ps[i].radius.x > 1.0f ? ps[i].radius.x : 1.0f;
        ry[i] = ps[i].radius.y > 1.0f ? ps[i].radius.y : 1.0f;
        centers[i] = ps[i].center;
    }
    const int x0 = std::max(docRect.x, dstX);
    const int y0 = std::max(docRect.y, dstY);
    const int x1 = std::min(docRect.right(), dstX + dstW);
    const int y1 = std::min(docRect.bottom(), dstY + dstH);
    const uint32_t* sp = src.pixels();
    const int sw = src.width();
    const int sh = src.height();
    const size_t n = ps.size();
    for (int y = y0; y < y1; ++y) {
        uint32_t* drow = dst + (size_t)(y - dstY) * (size_t)dstW;
        for (int x = x0; x < x1; ++x) {
            const float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float dx = 0.0f, dy = 0.0f;
            for (size_t i = 0; i < n; ++i) {
                const float vx = px - centers[i].x, vy = py - centers[i].y;
                const float lx = vx * cosA[i] + vy * sinA[i];
                const float ly = -vx * sinA[i] + vy * cosA[i];
                if (lx <= -rx[i] || lx >= rx[i] || ly <= -ry[i] || ly >= ry[i]) continue;
                const Vec2f dl = fs[i].sample(lx, ly);
                if (dl.x == 0.0f && dl.y == 0.0f) continue;
                dx += dl.x * cosA[i] - dl.y * sinA[i];
                dy += dl.x * sinA[i] + dl.y * cosA[i];
            }
            if (dx == 0.0f && dy == 0.0f) {
                const int bx = x - srcX, by = y - srcY;
                drow[x - dstX] = (bx >= 0 && by >= 0 && bx < sw && by < sh)
                                     ? sp[(size_t)by * (size_t)sw + (size_t)bx]
                                     : 0;
                continue;
            }
            const float sx = px - dx - (float)srcX;
            const float sy = py - dy - (float)srcY;
            drow[x - dstX] = premulBilinear(sp, sw, sh, sx, sy);
        }
    }
}

void localWarpWarpDocRect(const Image& src, int srcX, int srcY, const std::vector<LocalWarpParams>& ps,
                          uint32_t* dst, int dstW, int dstH, int dstX, int dstY, Rect docRect)
{
    if (!dst || src.empty()) return;
    std::vector<Frame> frames;
    frames.reserve(ps.size());
    for (const LocalWarpParams& p : ps)
        if (localWarpHasRegion(p)) frames.push_back(makeFrame(p));
    if (frames.empty()) return;

    const int x0 = std::max(docRect.x, dstX);
    const int y0 = std::max(docRect.y, dstY);
    const int x1 = std::min(docRect.right(), dstX + dstW);
    const int y1 = std::min(docRect.bottom(), dstY + dstH);
    const uint32_t* sp = src.pixels();
    const int sw = src.width();
    const int sh = src.height();
    for (int y = y0; y < y1; ++y) {
        uint32_t* drow = dst + (size_t)(y - dstY) * (size_t)dstW;
        for (int x = x0; x < x1; ++x) {
            const Vec2f doc{(float)x + 0.5f, (float)y + 0.5f};
            Vec2f disp{};
            for (const Frame& f : frames) {
                const Vec2f dl = f.toGlobal(f.offset(f.toLocal(doc)));
                disp.x += dl.x;
                disp.y += dl.y;
            }
            if (disp.x == 0.0f && disp.y == 0.0f) {
                const int bx = x - srcX, by = y - srcY;
                drow[x - dstX] = (bx >= 0 && by >= 0 && bx < sw && by < sh)
                                     ? sp[(size_t)by * (size_t)sw + (size_t)bx]
                                     : 0;
                continue;
            }
            const float sx = doc.x - disp.x - (float)srcX;
            const float sy = doc.y - disp.y - (float)srcY;
            drow[x - dstX] = premulBilinear(sp, sw, sh, sx, sy);
        }
    }
}

Image localWarpApply(const Image& layer, int layerX, int layerY, const LocalWarpParams& p, int& outX, int& outY)
{
    outX = layerX;
    outY = layerY;
    if (layer.empty() || !localWarpHasRegion(p)) return Image();
    const Frame f = makeFrame(p);
    const Rect region = localWarpRegionBounds(p);
    if (region.empty()) return Image();
    const float ax = std::fabs(p.amp.x);
    const float ay = std::fabs(p.amp.y);
    const float margin = std::max(ax, ay) + 2.0f;
    const int regMinX = (int)std::floor((float)region.x - margin);
    const int regMinY = (int)std::floor((float)region.y - margin);
    const int regMaxX = (int)std::ceil((float)region.right() + margin);
    const int regMaxY = (int)std::ceil((float)region.bottom() + margin);
    const int minX = std::min(layerX, regMinX);
    const int minY = std::min(layerY, regMinY);
    const int maxX = std::max(layerX + layer.width(), regMaxX);
    const int maxY = std::max(layerY + layer.height(), regMaxY);
    const int ow = maxX - minX;
    const int oh = maxY - minY;
    if (ow <= 0 || oh <= 0 || ow > 20000 || oh > 20000) return Image();

    outX = minX;
    outY = minY;
    Image out(ow, oh);
    uint32_t* op = out.pixels();
    const uint32_t* sp = layer.pixels();
    const int sw = layer.width();
    const int sh = layer.height();
    for (int y = 0; y < oh; ++y) {
        for (int x = 0; x < ow; ++x) {
            const Vec2f doc{(float)(minX + x) + 0.5f, (float)(minY + y) + 0.5f};
            const Vec2f disp = f.toGlobal(f.offset(f.toLocal(doc)));
            if (disp.x == 0.0f && disp.y == 0.0f) {
                const int bx = minX + x - layerX, by = minY + y - layerY;
                op[(size_t)y * (size_t)ow + (size_t)x] =
                    (bx >= 0 && by >= 0 && bx < sw && by < sh) ? sp[(size_t)by * (size_t)sw + (size_t)bx] : 0;
                continue;
            }
            const float sx = doc.x - disp.x - (float)layerX;
            const float sy = doc.y - disp.y - (float)layerY;
            op[(size_t)y * (size_t)ow + (size_t)x] = premulBilinear(sp, sw, sh, sx, sy);
        }
    }
    return out;
}

}
