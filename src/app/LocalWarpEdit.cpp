#include "app/Editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ie {

namespace {

double clampField(int field, double v)
{
    switch (field) {
    case 0:
    case 1:
        return std::clamp(v, -400.0, 400.0);
    case 2:
        return std::clamp(v, -180.0, 180.0);
    case 3:
        return std::clamp(v, -1.0, 1.0);
    case 4:
        return std::clamp(std::round(v), 0.0, 8.0);
    case 5:
        return std::clamp(v, 0.2, 8.0);
    default:
        return v;
    }
}

LocalWarpParams makeDefaultRegion(const Rect& content, int layerX, int layerY)
{
    LocalWarpParams p;
    p.center = Vec2f{(float)(layerX + content.x) + (float)content.w * 0.5f,
                     (float)(layerY + content.y) + (float)content.h * 0.5f};
    p.radius = Vec2f{150.0f, 100.0f};
    const float shortSide = (float)std::max(2, std::min(content.w, content.h));
    p.amp = Vec2f{shortSide * 0.08f, shortSide * 0.08f};
    return p;
}

bool regionHit(const LocalWarpParams& p, const Vec2f& pt, float& r_out)
{
    const float a = p.rotDeg * 3.14159265f / 180.0f;
    const float ca = std::cos(a), sa = std::sin(a);
    const float dx = pt.x - p.center.x, dy = pt.y - p.center.y;
    const float lx = dx * ca + dy * sa;
    const float ly = -dx * sa + dy * ca;
    const float nx = lx / (p.radius.x > 1.0f ? p.radius.x : 1.0f);
    const float ny = ly / (p.radius.y > 1.0f ? p.radius.y : 1.0f);
    r_out = std::sqrt(nx * nx + ny * ny);
    return r_out <= 1.0f;
}

}

bool Editor::beginLocalWarp(int layer)
{
    if (ft_.active) commitFreeTransform();
    if (mw_.active) commitMeshWarp();
    if (layer < 0 || layer >= (int)doc.layers.size()) return false;
    Layer& l = doc.layers[(size_t)layer];
    if (l.kind == LayerKind::Text || l.img.empty()) return false;
    const Rect cb = l.img.contentBounds();
    if (cb.empty()) return false;

    lw_ = LocalWarpState{};
    lw_.layer = layer;
    lw_.base = l.img;
    lw_.oldX = l.x;
    lw_.oldY = l.y;
    lw_.regions.clear();
    lw_.regions.push_back(makeDefaultRegion(cb, l.x, l.y));
    lw_.activeRegion = 0;
    lw_.active = true;
    return true;
}

Vec2f localWarpToLocal(const LocalWarpParams& p, const Vec2f& docPt)
{
    const float a = p.rotDeg * 3.14159265f / 180.0f;
    const float ca = std::cos(a), sa = std::sin(a);
    const float dx = docPt.x - p.center.x, dy = docPt.y - p.center.y;
    return Vec2f{dx * ca + dy * sa, -dx * sa + dy * ca};
}

Vec2f localWarpToDoc(const LocalWarpParams& p, const Vec2f& local)
{
    const float a = p.rotDeg * 3.14159265f / 180.0f;
    const float ca = std::cos(a), sa = std::sin(a);
    return Vec2f{p.center.x + local.x * ca - local.y * sa, p.center.y + local.x * sa + local.y * ca};
}

namespace {

bool hitEllipse(const LocalWarpParams& p, const Vec2f& pt, float& outR)
{
    const float a = p.rotDeg * 3.14159265f / 180.0f;
    const float ca = std::cos(a), sa = std::sin(a);
    const float dx = pt.x - p.center.x, dy = pt.y - p.center.y;
    const float lx = dx * ca + dy * sa;
    const float ly = -dx * sa + dy * ca;
    const float rx = p.radius.x > 1.0f ? p.radius.x : 1.0f;
    const float ry = p.radius.y > 1.0f ? p.radius.y : 1.0f;
    const float nx = lx / rx, ny = ly / ry;
    outR = std::sqrt(nx * nx + ny * ny);
    return outR <= 1.0f;
}

bool paramsSame(const LocalWarpParams& a, const LocalWarpParams& b)
{
    return a.center.x == b.center.x && a.center.y == b.center.y && a.radius.x == b.radius.x &&
           a.radius.y == b.radius.y && a.rotDeg == b.rotDeg && a.amp.x == b.amp.x && a.amp.y == b.amp.y &&
           a.poisson == b.poisson && a.harmonic == b.harmonic && a.falloff == b.falloff;
}

float normalizeDeg(float d)
{
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

}

void Editor::localWarpPress(const Vec2f& pt)
{
    if (!lw_.active || lw_.regions.empty()) return;
    lw_.dragFrom = pt;
    lw_.dragMode = 0;
    const float tolGood = 10.0f;
    if (lw_.activeRegion >= 0 && lw_.activeRegion < (int)lw_.regions.size()) {
        const LocalWarpParams& cur = lw_.regions[(size_t)lw_.activeRegion];
        const Vec2f ld = localWarpToLocal(cur, pt);
        const float arx = std::fabs(ld.x), ary = std::fabs(ld.y);
        if (std::fabs(ary) <= tolGood && std::fabs(arx - cur.radius.x) <= tolGood) {
            lw_.dragMode = 2;
            lw_.startRadius = cur.radius;
            return;
        }
        if (std::fabs(arx) <= tolGood && std::fabs(ary - cur.radius.y) <= tolGood) {
            lw_.dragMode = 3;
            lw_.startRadius = cur.radius;
            return;
        }
    }
    for (int i = 0; i < (int)lw_.regions.size(); ++i) {
        float r = 0.0f;
        if (hitEllipse(lw_.regions[(size_t)i], pt, r)) {
            lw_.activeRegion = i;
            lw_.dragMode = 1;
            const Vec2f c = lw_.regions[(size_t)i].center;
            lw_.grabOffset = Vec2f{pt.x - c.x, pt.y - c.y};
            return;
        }
    }
    if (lw_.activeRegion >= 0 && lw_.activeRegion < (int)lw_.regions.size()) {
        const LocalWarpParams& cur = lw_.regions[(size_t)lw_.activeRegion];
        const Vec2f ld = localWarpToLocal(cur, pt);
        const float rx = cur.radius.x > 1.0f ? cur.radius.x : 1.0f;
        const float ry = cur.radius.y > 1.0f ? cur.radius.y : 1.0f;
        const float r = std::sqrt((ld.x / rx) * (ld.x / rx) + (ld.y / ry) * (ld.y / ry));
        const float edge = (r - 1.0f) * (rx < ry ? rx : ry);
        if (r > 1.0f && edge <= tolGood) {
            lw_.dragMode = 4;
            lw_.startRot = cur.rotDeg;
            lw_.startAngle = std::atan2(ld.y, ld.x);
            return;
        }
    }
    LocalWarpParams fresh;
    fresh.center = pt;
    fresh.radius = Vec2f{4.0f, 4.0f};
    const float shortSide = 100.0f;
    fresh.amp = Vec2f{shortSide * 0.08f, shortSide * 0.08f};
    lw_.regions.push_back(fresh);
    lw_.activeRegion = (int)lw_.regions.size() - 1;
    lw_.dragMode = 5;
}

void Editor::localWarpDrag(const Vec2f& pt)
{
    if (!lw_.active || lw_.activeRegion < 0 || lw_.activeRegion >= (int)lw_.regions.size()) return;
    LocalWarpParams& cur = lw_.regions[(size_t)lw_.activeRegion];
    if (lw_.dragMode == 1) {
        cur.center = Vec2f{pt.x - lw_.grabOffset.x, pt.y - lw_.grabOffset.y};
    } else if (lw_.dragMode == 2 || lw_.dragMode == 3) {
        const Vec2f ld = localWarpToLocal(cur, pt);
        if (lw_.dragMode == 2) {
            cur.radius.x = std::max(4.0f, std::fabs(ld.x));
        } else {
            cur.radius.y = std::max(4.0f, std::fabs(ld.y));
        }
    } else if (lw_.dragMode == 4) {
        const Vec2f ld = localWarpToLocal(cur, pt);
        const float ang = std::atan2(ld.y, ld.x);
        float delta = (ang - lw_.startAngle) * 180.0f / 3.14159265f;
        cur.rotDeg = normalizeDeg(lw_.startRot + delta);
    } else if (lw_.dragMode == 5) {
        const float dx = pt.x - lw_.dragFrom.x, dy = pt.y - lw_.dragFrom.y;
        cur.center = Vec2f{(lw_.dragFrom.x + pt.x) * 0.5f, (lw_.dragFrom.y + pt.y) * 0.5f};
        cur.radius = Vec2f{std::max(4.0f, std::fabs(dx) * 0.5f), std::max(4.0f, std::fabs(dy) * 0.5f)};
    } else {
        return;
    }
    localWarpPreview(false);
    lw_.moved = true;
}

void Editor::localWarpRelease()
{
    if (!lw_.active) return;
    lw_.dragMode = 0;
    localWarpPreview(false);
}

bool Editor::localWarpScale(float factor)
{
    if (!lw_.active || lw_.activeRegion < 0 || lw_.activeRegion >= (int)lw_.regions.size()) return false;
    if (factor <= 0.0f) return false;
    LocalWarpParams& cur = lw_.regions[(size_t)lw_.activeRegion];
    cur.radius.x = std::clamp(cur.radius.x * factor, 4.0f, 20000.0f);
    cur.radius.y = std::clamp(cur.radius.y * factor, 4.0f, 20000.0f);
    localWarpPreview(false);
    lw_.moved = true;
    return true;
}

void Editor::localWarpSetField(int field, double v)
{
    if (!lw_.active || lw_.activeRegion < 0 || lw_.activeRegion >= (int)lw_.regions.size()) return;
    const double cv = clampField(field, v);
    LocalWarpParams& cur = lw_.regions[(size_t)lw_.activeRegion];
    switch (field) {
    case 0: cur.amp.x = (float)cv; break;
    case 1: cur.amp.y = (float)cv; break;
    case 2: cur.rotDeg = (float)cv; break;
    case 3: cur.poisson = (float)cv; break;
    case 4: cur.harmonic = (int)cv; break;
    case 5: cur.falloff = (float)cv; break;
    default: return;
    }
    localWarpPreview(false);
    lw_.moved = true;
}

double Editor::localWarpField(int field) const
{
    if (lw_.activeRegion < 0 || lw_.activeRegion >= (int)lw_.regions.size()) return 0.0;
    const LocalWarpParams& p = lw_.regions[(size_t)lw_.activeRegion];
    switch (field) {
    case 0: return (double)p.amp.x;
    case 1: return (double)p.amp.y;
    case 2: return (double)p.rotDeg;
    case 3: return (double)p.poisson;
    case 4: return (double)p.harmonic;
    case 5: return (double)p.falloff;
    default: return 0.0;
    }
}

void Editor::localWarpCopyRegion()
{
    if (!lw_.active || lw_.activeRegion < 0 || lw_.activeRegion >= (int)lw_.regions.size()) return;
    lwClip_ = lw_.regions[(size_t)lw_.activeRegion];
    lwHasClip_ = true;
}

void Editor::localWarpPasteRegion()
{
    if (!lw_.active) return;
    LocalWarpParams np = lwHasClip_ ? lwClip_ : LocalWarpParams{};
    if (!lwHasClip_) {
        np.radius = Vec2f{150.0f, 100.0f};
        np.amp = Vec2f{8.0f, 8.0f};
    }
    np.center = Vec2f{np.center.x + 24.0f, np.center.y + 24.0f};
    lw_.regions.push_back(np);
    lw_.activeRegion = (int)lw_.regions.size() - 1;
    localWarpPreview(false);
    lw_.moved = true;
}

void Editor::localWarpDeleteRegion()
{
    if (!lw_.active || lw_.regions.empty()) return;
    if (lw_.activeRegion < 0 || lw_.activeRegion >= (int)lw_.regions.size()) return;
    lw_.regions.erase(lw_.regions.begin() + lw_.activeRegion);
    if (lw_.regions.empty()) {
        lw_.activeRegion = -1;
    } else if (lw_.activeRegion >= (int)lw_.regions.size()) {
        lw_.activeRegion = (int)lw_.regions.size() - 1;
    }
    localWarpPreview(false);
    lw_.moved = true;
}

int Editor::localWarpActiveRegionAt(const Vec2f& docPt) const
{
    for (int i = 0; i < (int)lw_.regions.size(); ++i) {
        float r = 0.0f;
        if (hitEllipse(lw_.regions[(size_t)i], docPt, r)) return i;
    }
    return -1;
}

bool Editor::localWarpContains(const Vec2f& docPt) const
{
    return localWarpActiveRegionAt(docPt) >= 0;
}

void Editor::localWarpPreview(bool highQuality)
{
    (void)highQuality;
    if (!lw_.active) return;
    if (lw_.base.empty()) return;
    const Rect prevRect = lw_.lastRect;

    std::vector<LocalWarpParams> live;
    std::vector<int> liveIdx;
    for (int i = 0; i < (int)lw_.regions.size(); ++i) {
        if (localWarpHasEffect(lw_.regions[(size_t)i])) {
            live.push_back(lw_.regions[(size_t)i]);
            liveIdx.push_back(i);
        }
    }

    if (live.empty()) {
        lw_.preview = Image();
        lw_.outPix = nullptr;
        lw_.outW = lw_.outH = 0;
        if (!prevRect.empty()) doc.markDirty(prevRect);
        lw_.lastRect = Rect{};
        lw_.applied = false;
        lw_.moved = false;
        return;
    }

    if (lw_.fields.size() != lw_.regions.size()) lw_.fields.resize(lw_.regions.size());
    for (int i = 0; i < (int)lw_.regions.size(); ++i)
        if (!lw_.fields[(size_t)i].matches(lw_.regions[(size_t)i]))
            lw_.fields[(size_t)i].build(lw_.regions[(size_t)i]);
    lw_.liveFields.clear();
    for (int gi : liveIdx) lw_.liveFields.push_back(lw_.fields[(size_t)gi]);

    Rect need{};
    for (const LocalWarpParams& p : live) {
        const Rect r = localWarpPaintBounds(p);
        need = need.empty() ? r : need.unite(r);
    }
    if (need.w <= 0 || need.h <= 0 || need.w > 20000 || need.h > 20000) return;

    if (lw_.preview.empty() || need.w != lw_.outW || need.h != lw_.outH) {
        lw_.preview = Image(need.w, need.h);
        lw_.outPix = lw_.preview.pixels();
    }

    const bool geometrySame =
        lw_.lastRect.x == need.x && lw_.lastRect.y == need.y && lw_.lastRect.w == need.w &&
        lw_.lastRect.h == need.h;
    const bool countSame =
        (int)lw_.prevRegions.size() == (int)lw_.regions.size() && (int)lw_.prevRects.size() == (int)lw_.regions.size();
    const bool full = !geometrySame || !countSame || lw_.outBoxX != need.x || lw_.outBoxY != need.y;

    lw_.outX = need.x;
    lw_.outY = need.y;
    lw_.outW = need.w;
    lw_.outH = need.h;

    Rect dirty{};
    if (full) {
        localWarpBlitDocRect(lw_.base, lw_.oldX, lw_.oldY, lw_.outPix, need.w, need.h, need.x, need.y, need);
        for (int k = 0; k < (int)live.size(); ++k)
            localWarpWarpDocRectFields(lw_.base, lw_.oldX, lw_.oldY, live, lw_.liveFields, lw_.outPix, need.w,
                                       need.h, need.x, need.y, localWarpPaintBounds(live[(size_t)k]));
        lw_.prevRegions = lw_.regions;
        lw_.prevRects.assign(lw_.regions.size(), Rect{});
        for (int k = 0; k < (int)live.size(); ++k)
            lw_.prevRects[(size_t)liveIdx[(size_t)k]] = localWarpPaintBounds(live[(size_t)k]);
        dirty = need;
    } else {
        for (int k = 0; k < (int)live.size(); ++k) {
            const int gi = liveIdx[(size_t)k];
            const LocalWarpParams& now = live[(size_t)k];
            bool changed = !paramsSame(now, lw_.prevRegions[(size_t)gi]);
            if (!changed) continue;
            const Rect r = localWarpPaintBounds(now);
            const Rect old = lw_.prevRects[(size_t)gi];
            if (!old.empty())
                localWarpBlitDocRect(lw_.base, lw_.oldX, lw_.oldY, lw_.outPix, need.w, need.h, need.x, need.y,
                                     old);
            localWarpWarpDocRectFields(lw_.base, lw_.oldX, lw_.oldY, live, lw_.liveFields, lw_.outPix, need.w,
                                       need.h, need.x, need.y, r);
            lw_.prevRects[(size_t)gi] = r;
            lw_.prevRegions[(size_t)gi] = now;
            dirty = dirty.empty() ? (old.empty() ? r : old.unite(r)) : dirty.unite(old.empty() ? r : old.unite(r));
        }
    }

    lw_.outBoxX = need.x;
    lw_.outBoxY = need.y;
    doc.markDirty(prevRect.empty() ? need : prevRect.unite(need));
    lw_.lastRect = need;
    lw_.applied = true;
    lw_.moved = true;
}

void Editor::commitLocalWarp()
{
    if (!lw_.active) return;
    Layer& l0 = doc.layers[(size_t)lw_.layer];
    const Rect oldB = doc.layerVisualBounds(lw_.layer);
    lw_.outPix = nullptr;

    std::vector<LocalWarpParams> live;
    for (const LocalWarpParams& p : lw_.regions)
        if (localWarpHasEffect(p)) live.push_back(p);
    int ox = 0, oy = 0;
    Image result;
    if (!live.empty()) result = localWarpApplyAll(lw_.base, lw_.oldX, lw_.oldY, live, ox, oy);

    if (result.empty()) {
        l0.img = lw_.base;
        l0.x = lw_.oldX;
        l0.y = lw_.oldY;
        doc.markDirty(oldB.unite(doc.layerVisualBounds(lw_.layer)));
        lw_ = LocalWarpState{};
        return;
    }

    l0.img = lw_.base;
    l0.x = lw_.oldX;
    l0.y = lw_.oldY;
    hist.record(doc, sel, "局部变形");
    l0.img = std::move(result);
    l0.x = ox;
    l0.y = oy;
    doc.markDirty(oldB.unite(doc.layerVisualBounds(lw_.layer)));
    afterEdit("局部变形完成");
    lw_ = LocalWarpState{};
}

void Editor::cancelLocalWarp()
{
    if (!lw_.active) return;
    const Rect prevRect = lw_.lastRect;
    Layer& l = doc.layers[(size_t)lw_.layer];
    l.img = lw_.base;
    l.x = lw_.oldX;
    l.y = lw_.oldY;
    if (!prevRect.empty()) doc.markDirty(prevRect);
    lw_ = LocalWarpState{};
}

}
