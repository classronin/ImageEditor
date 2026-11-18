#include "core/Selection.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ie {

void Selection::reset(int w, int h)
{
    width = std::max(0, w);
    height = std::max(0, h);
    mask_ = std::make_shared<std::vector<uint8_t>>((size_t)width * height, 0);
    active = false;
    segs_.clear();
    bounds_ = Rect{};
    lastRect_ = Rect{};
    rectMode_ = false;
}

void Selection::fillMask(Rect r, uint8_t value)
{
    detachMask();
    r = r.clampTo(width, height);
    if (r.empty()) return;
    for (int y = r.y; y < r.bottom(); ++y)
        std::memset((*mask_).data() + (size_t)y * width + (size_t)r.x, value, (size_t)r.w);
}

void Selection::fillEllipse(Rect r0, uint8_t value)
{
    detachMask();
    Rect r = r0.clampTo(width, height);
    if (r.empty()) return;
    const double cx = (double)r.x + (double)r.w * 0.5;
    const double cy = (double)r.y + (double)r.h * 0.5;
    const double rx = (double)r.w * 0.5;
    const double ry = (double)r.h * 0.5;
    if (rx <= 0.0 || ry <= 0.0) return;
    for (int y = r.y; y < r.bottom(); ++y) {
        const double dy = ((double)y + 0.5 - cy) / ry;
        const double k = 1.0 - dy * dy;
        if (k <= 0.0) continue;
        const double hw = rx * std::sqrt(k);
        int x0 = (int)std::ceil(cx - hw - 0.5);
        int x1 = (int)std::floor(cx + hw - 0.5);
        x0 = std::max(x0, 0);
        x1 = std::min(x1, width - 1);
        if (x0 > x1) continue;
        std::memset((*mask_).data() + (size_t)y * width + (size_t)x0, value, (size_t)(x1 - x0 + 1));
    }
}

void Selection::clear()
{
    detachMask();
    if (active) {
        if (rectMode_ && !lastRect_.empty())
            fillMask(lastRect_, 0);
        else
            std::fill((*mask_).begin(), (*mask_).end(), 0);
    }
    active = false;
    segs_.clear();
    bounds_ = Rect{};
    lastRect_ = Rect{};
    rectMode_ = false;
}

void Selection::selectAll()
{
    detachMask();
    if (width <= 0 || height <= 0) return;
    std::fill((*mask_).begin(), (*mask_).end(), 255);
    active = true;
    rectMode_ = true;
    bounds_ = Rect{0, 0, width, height};
    lastRect_ = bounds_;
    segs_.clear();
    segs_.push_back({0.f, 0.f, (float)width, 0.f});
    segs_.push_back({0.f, (float)height, (float)width, (float)height});
    segs_.push_back({0.f, 0.f, 0.f, (float)height});
    segs_.push_back({(float)width, 0.f, (float)width, (float)height});
}

void Selection::setRect(Rect r)
{
    detachMask();
    r = r.clampTo(width, height);
    if (r.empty()) {
        clear();
        return;
    }
    if (!active) {
        std::fill((*mask_).begin(), (*mask_).end(), 0);
    } else if (rectMode_ && !lastRect_.empty()) {
        fillMask(lastRect_, 0);
    } else {
        std::fill((*mask_).begin(), (*mask_).end(), 0);
    }
    fillMask(r, 255);

    active = true;
    rectMode_ = true;
    lastRect_ = r;
    bounds_ = r;

    const float x0 = (float)r.x, y0 = (float)r.y;
    const float x1 = (float)r.right(), y1 = (float)r.bottom();
    segs_.clear();
    segs_.push_back({x0, y0, x1, y0});
    segs_.push_back({x0, y1, x1, y1});
    segs_.push_back({x0, y0, x0, y1});
    segs_.push_back({x1, y0, x1, y1});
}

void Selection::addRect(Rect r)
{
    r = r.clampTo(width, height);
    if (r.empty()) return;
    fillMask(r, 255);
    active = true;
    rectMode_ = false;
    rebuildOutline();
}

void Selection::subtractRect(Rect r)
{
    r = r.clampTo(width, height);
    if (r.empty() || !active) return;
    fillMask(r, 0);
    rectMode_ = false;
    rebuildOutline();
    if (bounds_.empty()) clear();
}

void Selection::setEllipse(Rect r)
{
    detachMask();
    r = r.clampTo(width, height);
    if (r.empty()) {
        clear();
        return;
    }
    std::fill((*mask_).begin(), (*mask_).end(), 0);
    fillEllipse(r, 255);
    active = true;
    rectMode_ = false;
    lastRect_ = Rect{};
    rebuildOutline();
}

void Selection::addEllipse(Rect r)
{
    r = r.clampTo(width, height);
    if (r.empty()) return;
    fillEllipse(r, 255);
    active = true;
    rectMode_ = false;
    rebuildOutline();
}

void Selection::subtractEllipse(Rect r)
{
    r = r.clampTo(width, height);
    if (r.empty() || !active) return;
    fillEllipse(r, 0);
    rectMode_ = false;
    rebuildOutline();
    if (bounds_.empty()) clear();
}

void Selection::intersectWith(const Selection& o)
{
    if (!active) return;
    detachMask();
    if (!o.active || o.width != width || o.height != height) {
        clear();
        return;
    }
    auto& m = *mask_;
    const auto& om = o.mask();
    for (size_t i = 0; i < m.size(); ++i) m[i] = (m[i] && om[i]) ? 255 : 0;
    rectMode_ = false;
    rebuildOutline();
    if (bounds_.empty()) clear();
}

void Selection::intersectRect(Rect r)
{
    if (!active) return;
    Selection tmp;
    tmp.reset(width, height);
    tmp.setRect(r);
    intersectWith(tmp);
}

void Selection::intersectEllipse(Rect r)
{
    if (!active) return;
    Selection tmp;
    tmp.reset(width, height);
    tmp.setEllipse(r);
    intersectWith(tmp);
}

void Selection::intersectPolygon(const std::vector<Vec2f>& pts)
{
    if (!active) return;
    Selection tmp;
    tmp.reset(width, height);
    tmp.selectPolygon(pts);
    intersectWith(tmp);
}

void Selection::intersectFlood(const uint32_t* canvas, int sx, int sy, int tolerance)
{
    if (!active) return;
    Selection tmp;
    tmp.reset(width, height);
    tmp.selectFlood(canvas, sx, sy, tolerance, false);
    intersectWith(tmp);
}

void Selection::invert()
{
    if (width <= 0 || height <= 0) return;
    detachMask();
    for (auto& v : (*mask_)) v = v ? 0 : 255;
    active = true;
    rectMode_ = false;
    rebuildOutline();
    if (bounds_.empty()) clear();
}

void Selection::fillPolygon(const std::vector<Vec2f>& pts, uint8_t value)
{
    detachMask();
    const int n = (int)pts.size();
    if (n < 3 || width <= 0 || height <= 0) return;

    float minY = pts[0].y, maxY = pts[0].y;
    for (int i = 1; i < n; ++i) {
        minY = std::min(minY, pts[i].y);
        maxY = std::max(maxY, pts[i].y);
    }
    const int y0 = std::max(0, (int)std::floor(minY));
    const int y1 = std::min(height - 1, (int)std::ceil(maxY));
    if (y0 > y1) return;

    std::vector<float> xs;
    xs.reserve((size_t)n);
    for (int y = y0; y <= y1; ++y) {
        const float ly = (float)y + 0.5f;
        xs.clear();
        for (int i = 0; i < n; ++i) {
            const Vec2f& a = pts[(size_t)i];
            const Vec2f& b = pts[(size_t)((i + 1) % n)];
            if ((a.y <= ly) != (b.y <= ly)) {
                const float t = (ly - a.y) / (b.y - a.y);
                xs.push_back(a.x + t * (b.x - a.x));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            const int xa = std::max(0, (int)std::ceil(xs[k] - 0.5f));
            const int xb = std::min(width, (int)std::floor(xs[k + 1] - 0.5f) + 1);
            if (xb <= xa) continue;
            std::memset((*mask_).data() + (size_t)y * (size_t)width + (size_t)xa, value, (size_t)(xb - xa));
        }
    }
}

void Selection::selectPolygon(const std::vector<Vec2f>& pts)
{
    detachMask();
    if (width <= 0 || height <= 0) return;
    std::fill((*mask_).begin(), (*mask_).end(), 0);
    fillPolygon(pts, 255);
    active = true;
    rectMode_ = false;
    rebuildOutline();
    if (bounds_.empty()) clear();
}

void Selection::addPolygon(const std::vector<Vec2f>& pts)
{
    detachMask();
    if (width <= 0 || height <= 0) return;
    if (!active) std::fill((*mask_).begin(), (*mask_).end(), 0);
    fillPolygon(pts, 255);
    active = true;
    rectMode_ = false;
    rebuildOutline();
    if (bounds_.empty()) clear();
}

void Selection::subtractPolygon(const std::vector<Vec2f>& pts)
{
    if (!active || width <= 0 || height <= 0) return;
    fillPolygon(pts, 0);
    rectMode_ = false;
    rebuildOutline();
    if (bounds_.empty()) clear();
}

void Selection::floodFill(const uint32_t* canvas, int sx, int sy, int tolerance, uint8_t value)
{
    detachMask();
    if (!canvas || width <= 0 || height <= 0) return;
    if (sx < 0 || sy < 0 || sx >= width || sy >= height) return;

    const uint32_t seed = canvas[(size_t)sy * (size_t)width + (size_t)sx];
    const int r0 = (int)(seed & 0xff), g0 = (int)((seed >> 8) & 0xff);
    const int b0 = (int)((seed >> 16) & 0xff), a0 = (int)(seed >> 24);
    const auto match = [&](uint32_t v) {
        return std::abs((int)(v & 0xff) - r0) <= tolerance && std::abs((int)((v >> 8) & 0xff) - g0) <= tolerance &&
               std::abs((int)((v >> 16) & 0xff) - b0) <= tolerance && std::abs((int)(v >> 24) - a0) <= tolerance;
    };

    std::vector<uint8_t> scratch((size_t)width * (size_t)height, 0);
    std::vector<int> stack;
    stack.reserve(1024);

    const int start = sy * width + sx;
    (*mask_)[(size_t)start] = value;
    scratch[(size_t)start] = 1;
    stack.push_back(start);

    while (!stack.empty()) {
        const int cur = stack.back();
        stack.pop_back();
        const int x = cur % width, y = cur / width;
        const int nb[4] = {(y > 0) ? cur - width : -1, (y + 1 < height) ? cur + width : -1,
                           (x > 0) ? cur - 1 : -1, (x + 1 < width) ? cur + 1 : -1};
        for (int k = 0; k < 4; ++k) {
            const int idx = nb[k];
            if (idx < 0 || scratch[(size_t)idx]) continue;
            if (!match(canvas[(size_t)idx])) continue;
            scratch[(size_t)idx] = 1;
            (*mask_)[(size_t)idx] = value;
            stack.push_back(idx);
        }
    }
}

void Selection::selectFlood(const uint32_t* canvas, int sx, int sy, int tolerance, bool add)
{
    detachMask();
    if (width <= 0 || height <= 0) return;
    if (!active || !add) std::fill((*mask_).begin(), (*mask_).end(), 0);
    floodFill(canvas, sx, sy, tolerance, 255);
    active = true;
    rectMode_ = false;
    rebuildOutline();
    if (bounds_.empty()) clear();
}

void Selection::subtractFlood(const uint32_t* canvas, int sx, int sy, int tolerance)
{
    if (!active || width <= 0 || height <= 0) return;
    floodFill(canvas, sx, sy, tolerance, 0);
    rectMode_ = false;
    rebuildOutline();
    if (bounds_.empty()) clear();
}

void Selection::translate(int dx, int dy)
{
    detachMask();
    if (!active || width <= 0 || height <= 0) return;
    if (dx == 0 && dy == 0) return;
    std::vector<uint8_t> out((*mask_).size(), 0);
    bool any = false;
    for (int y = 0; y < height; ++y) {
        const int ny = y + dy;
        if (ny < 0 || ny >= height) continue;
        for (int x = 0; x < width; ++x) {
            if (!(*mask_)[(size_t)y * width + x]) continue;
            const int nx = x + dx;
            if (nx < 0 || nx >= width) continue;
            out[(size_t)ny * width + nx] = (*mask_)[(size_t)y * width + x];
            any = true;
        }
    }
    if (!any) {
        clear();
        return;
    }
    (*mask_).swap(out);
    rebuildOutline();
}

bool Selection::contains(int x, int y) const
{
    if (!active) return false;
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    return (*mask_)[(size_t)y * width + x] != 0;
}

void Selection::detachMask()
{
    if (!mask_) {
        mask_ = std::make_shared<std::vector<uint8_t>>((size_t)std::max(0, width) * (size_t)std::max(0, height), 0);
        return;
    }
    if (mask_.use_count() > 1) mask_ = std::make_shared<std::vector<uint8_t>>(*mask_);
}

bool Selection::sameAs(const Selection& o) const
{
    if (active != o.active || width != o.width || height != o.height) return false;
    if (!active) return true;
    if (bounds_ != o.bounds_) return false;
    return *mask_ == *o.mask_;
}

void Selection::setAlphaMask(const uint8_t* alpha, int aw, int ah, int ox, int oy, uint8_t threshold)
{
    detachMask();
    if (width <= 0 || height <= 0 || !alpha || aw <= 0 || ah <= 0) return;
    std::fill((*mask_).begin(), (*mask_).end(), 0);
    bool any = false;
    for (int y = 0; y < ah; ++y) {
        const int dy = oy + y;
        if (dy < 0 || dy >= height) continue;
        const uint8_t* src = alpha + (size_t)y * (size_t)aw;
        uint8_t* dst = (*mask_).data() + (size_t)dy * (size_t)width;
        for (int x = 0; x < aw; ++x) {
            const int dx = ox + x;
            if (dx < 0 || dx >= width) continue;
            if (src[x] < threshold) continue;
            dst[dx] = 255;
            any = true;
        }
    }
    active = any;
    rectMode_ = false;
    lastRect_ = Rect{};
    rebuildOutline();
}

void Selection::addMask(const Selection& o)
{
    if (o.width != width || o.height != height) return;
    if (!o.active) return;
    detachMask();
    const std::vector<uint8_t>& src = o.mask();
    for (size_t i = 0; i < mask_->size(); ++i)
        if (src[i] > (*mask_)[i]) (*mask_)[i] = src[i];
    active = true;
    rectMode_ = false;
    lastRect_ = Rect{};
    rebuildOutline();
}

void Selection::subtractMask(const Selection& o)
{
    if (o.width != width || o.height != height) return;
    if (!active || !o.active) return;
    detachMask();
    const std::vector<uint8_t>& src = o.mask();
    bool any = false;
    for (size_t i = 0; i < mask_->size(); ++i) {
        if (src[i]) (*mask_)[i] = 0;
        if ((*mask_)[i]) any = true;
    }
    active = any;
    rectMode_ = false;
    lastRect_ = Rect{};
    rebuildOutline();
}

void Selection::intersectMask(const Selection& o)
{
    if (o.width != width || o.height != height) return;
    detachMask();
    const std::vector<uint8_t>& src = o.mask();
    bool any = false;
    for (size_t i = 0; i < mask_->size(); ++i) {
        if (!src[i] || !o.active) (*mask_)[i] = 0;
        if ((*mask_)[i]) any = true;
    }
    active = any;
    rectMode_ = false;
    lastRect_ = Rect{};
    rebuildOutline();
}

void Selection::rebuildOutline()
{
    segs_.clear();
    if (!active || width <= 0 || height <= 0) {
        bounds_ = Rect{};
        return;
    }

    int minX = width, minY = height, maxX = -1, maxY = -1;

    for (int y = 0; y <= height; ++y) {
        int runStart = -1;
        for (int x = 0; x <= width; ++x) {
            bool diff = false;
            if (x < width) {
                const bool above = (y > 0) && (*mask_)[(size_t)(y - 1) * width + x];
                const bool below = (y < height) && (*mask_)[(size_t)y * width + x];
                diff = above != below;
                if (below) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
            if (diff && runStart < 0)
                runStart = x;
            else if (!diff && runStart >= 0) {
                segs_.push_back({(float)runStart, (float)y, (float)x, (float)y});
                runStart = -1;
            }
        }
    }

    for (int x = 0; x <= width; ++x) {
        int runStart = -1;
        for (int y = 0; y <= height; ++y) {
            bool diff = false;
            if (y < height) {
                const bool left = (x > 0) && (*mask_)[(size_t)y * width + (x - 1)];
                const bool right = (x < width) && (*mask_)[(size_t)y * width + x];
                diff = left != right;
            }
            if (diff && runStart < 0)
                runStart = y;
            else if (!diff && runStart >= 0) {
                segs_.push_back({(float)x, (float)runStart, (float)x, (float)y});
                runStart = -1;
            }
        }
    }

    bounds_ = (maxX >= 0) ? Rect::fromLTRB(minX, minY, maxX + 1, maxY + 1) : Rect{};
}

}
