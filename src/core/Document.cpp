#include "core/Document.h"
#include "core/Matting.h"

#include "core/Blend.h"
#include "core/LayerStyle.h"
#include "core/Primaries.h"
#include "core/ShadowLift.h"

#include "include/core/SkPaint.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkColorFilter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>

namespace ie {

static bool textHasOutline(const Layer& l)
{
    if (l.kind != LayerKind::Text) return false;
    SkPath gp;
    return textGlyphPath(l.text, gp) && !gp.isEmpty();
}

bool Document::textLayerHasOutline(int index) const
{
    if (index < 0 || index >= (int)layers.size()) return false;
    return textHasOutline(layers[(size_t)index]);
}

namespace {

void compositeRow(uint32_t* dst, const uint32_t* src, size_t count, float opacity, BlendMode mode)
{
    if (mode == BlendMode::Normal) {
        if (opacity >= 1.0f && blend::allOpaque(src, count)) {
            std::memcpy(dst, src, count * 4);
            return;
        }
        blend::over(dst, src, count, opacity);
        return;
    }
    for (size_t i = 0; i < count; ++i) dst[i] = blend::compositePixel(dst[i], src[i], opacity, mode);
}

}

void Document::ensureCache()
{
    if (cache_.width() != width || cache_.height() != height) cache_.reset(width, height);
}

void Document::createEmpty(int w, int h, Color bg)
{
    width = std::max(1, w);
    height = std::max(1, h);
    layers.clear();
    idCounter_ = 1;

    Layer bgLayer;
    bgLayer.id = idCounter_++;
    bgLayer.name = "背景";
    bgLayer.img.reset(width, height);
    if (bg.packed() != 0) bgLayer.img.fillAll(bg.packed());
    layers.push_back(std::move(bgLayer));
    active = 0;
    selIds.clear();

    ensureCache();
    markAllDirty();
}

void Document::adopt(int w, int h, Image image, std::string name)
{
    width = std::max(1, w);
    height = std::max(1, h);
    layers.clear();
    idCounter_ = 1;

    Layer l;
    l.id = idCounter_++;
    l.name = std::move(name);
    l.img = std::move(image);
    layers.push_back(std::move(l));
    active = 0;
    selIds.clear();

    ensureCache();
    markAllDirty();
}

Layer* Document::activeLayer()
{
    if (active < 0 || active >= (int)layers.size()) return nullptr;
    return &layers[(size_t)active];
}

const Layer* Document::activeLayer() const
{
    if (active < 0 || active >= (int)layers.size()) return nullptr;
    return &layers[(size_t)active];
}

int Document::indexOfId(int id) const
{
    for (int i = 0; i < (int)layers.size(); ++i)
        if (layers[(size_t)i].id == id) return i;
    return -1;
}

int Document::nextLayerId() const
{
    int m = idCounter_;
    for (const auto& l : layers) m = std::max(m, l.id + 1);
    return m;
}

void Document::addLayer(Image img, std::string name)
{
    addLayerAt(std::move(img), 0, 0, std::move(name));
}

void Document::addLayerAt(Image img, int ox, int oy, std::string name)
{
    if (!valid() || img.empty()) return;
    Layer l;
    l.id = nextLayerId();
    l.name = std::move(name);
    l.img = std::move(img);
    l.x = ox;
    l.y = oy;

    const int at = (active >= 0 ? active + 1 : (int)layers.size());
    layers.insert(layers.begin() + at, std::move(l));
    active = at;
    selIds.clear();
    idCounter_ = std::max(idCounter_, layers[(size_t)at].id + 1);
    ++structureRev_;
    markAllDirty();
}

void Document::addEmptyLayer(std::string name)
{
    Image img(width, height);
    addLayer(std::move(img), std::move(name));
}

void Document::clampLayersState()
{
    if (layers.empty()) {
        active = -1;
        selIds.clear();
        return;
    }
    if (active < 0 || active >= (int)layers.size()) active = (int)layers.size() - 1;
    std::vector<int> keep;
    keep.reserve(selIds.size());
    for (int id : selIds) {
        const int idx = indexOfId(id);
        if (idx < 0 || idx == active) continue;
        keep.push_back(id);
    }
    selIds.swap(keep);
}

int Document::addTextLayer(const TextData& t, std::string name)
{
    if (!valid()) return -1;
    Layer l;
    l.id = nextLayerId();
    idCounter_ = l.id + 1;
    l.name = std::move(name);
    l.kind = LayerKind::Text;
    l.text = t;
    l.img = Image();
    const int at = (active >= 0 ? active + 1 : (int)layers.size());
    layers.insert(layers.begin() + at, std::move(l));
    active = at;
    selIds.clear();
    idCounter_ = std::max(idCounter_, layers[(size_t)at].id + 1);
    ++structureRev_;
    textDirty_ = true;
    refreshTextLayers();
    return at;
}

void Document::invalidateTextLayers()
{
    textDirty_ = true;
}

void Document::refreshTextLayer(int index)
{
    if (index < 0 || index >= (int)layers.size()) return;
    Layer& l = layers[(size_t)index];
    if (l.kind != LayerKind::Text) return;
    l.img = Image();
    if (l.text.text.empty()) return;
    if (l.textXf.isIdentity()) {
        const TextMetrics m = measureText(l.text);
        if (!m.ok || m.width <= 0 || m.lines <= 0) return;
        const int pad = 8;
        const int ox = (int)std::floor(l.text.x) - pad;
        const int oy = (int)std::floor(l.text.y) - pad;
        Image tmp(std::max(1, (int)std::ceil(m.width)) + pad * 2,
                  std::max(1, (int)std::ceil((float)m.lines * m.lineH)) + pad * 2);
        TextData t = l.text;
        t.x = l.text.x - (float)ox;
        t.y = l.text.y - (float)oy;
        Rect d;
        drawText(tmp, t, d, nullptr);
        const Rect cb = tmp.contentBounds();
        if (cb.empty()) return;
        l.img = cropRegion(tmp, cb);
        l.x = ox + cb.x;
        l.y = oy + cb.y;
        return;
    }
    SkPath raw;
    if (!textGlyphPath(l.text, raw) || raw.isEmpty()) return;
    SkPath tp;
    raw.transform(l.textXf, &tp);
    const SkRect b = tp.getBounds();
    if (b.isEmpty()) return;
    const int bx = (int)std::floor(b.x()) - 2;
    const int by = (int)std::floor(b.y()) - 2;
    const int bw = std::max(1, (int)std::ceil(b.right()) - bx + 2);
    const int bh = std::max(1, (int)std::ceil(b.bottom()) - by + 2);
    Image tmp(bw, bh);
    SkPath local;
    tp.transform(SkMatrix::Translate((float)-bx, (float)-by), &local);
    Rect d;
    drawGlyphPath(tmp, local, l.text.color, l.text.opacity, d);
    const Rect cb = tmp.contentBounds();
    if (cb.empty()) return;
    l.img = cropRegion(tmp, cb);
    l.x = bx + cb.x;
    l.y = by + cb.y;
}

void Document::refreshTextLayers()
{
    textDirty_ = false;
    for (int i = 0; i < (int)layers.size(); ++i) refreshTextLayer(i);
    markAllDirty();
}

void Document::demoteTextLayer(int index)
{
    if (index < 0 || index >= (int)layers.size()) return;
    Layer& l = layers[(size_t)index];
    if (l.kind != LayerKind::Text) return;
    l.kind = LayerKind::Pixel;
    l.text = TextData{};
    ++structureRev_;
}

void Document::removeLayer(int index)
{
    if (index < 0 || index >= (int)layers.size()) return;
    if (layers.size() == 1) {
        Layer& l = layers[0];
        l.img.reset(width, height);
        l.kind = LayerKind::Pixel;
        l.text = TextData{};
        l.children.clear();
        l.style = LayerStyle{};
        l.uMask.clear();
        l.uMask.shrink_to_fit();
        l.uMaskW = l.uMaskH = 0;
        ++l.uMaskRev;
        l.name = "图层 1";
        active = 0;
        selIds.clear();
        ++structureRev_;
        markAllDirty();
        return;
    }
    layers.erase(layers.begin() + index);
    active = std::min((int)layers.size() - 1, std::max(0, index - 1));
    selIds.clear();
    ++structureRev_;
    clampLayersState();
    markAllDirty();
}

void Document::duplicateLayer(int index)
{
    if (index < 0 || index >= (int)layers.size()) return;
    Layer copy = layers[(size_t)index];
    copy.id = nextLayerId();
    idCounter_ = copy.id + 1;
    for (auto& c : copy.children)
        if (c.id != 0) c.id = claimChildId();
    copy.name += " 副本";
    layers.insert(layers.begin() + index + 1, std::move(copy));
    active = index + 1;
    selIds.clear();
    ++structureRev_;
    clampLayersState();
    markAllDirty();
}

void Document::moveLayer(int from, int to)
{
    if (from < 0 || from >= (int)layers.size()) return;
    to = std::clamp(to, 0, (int)layers.size() - 1);
    if (from == to) return;
    Layer l = std::move(layers[(size_t)from]);
    layers.erase(layers.begin() + from);
    layers.insert(layers.begin() + to, std::move(l));
    active = to;
    ++structureRev_;
    clampLayersState();
    markAllDirty();
}

bool Document::bakeLayerEffects(int index)
{
    if (index < 0 || index >= (int)layers.size()) return false;
    const Layer& src = layers[(size_t)index];
    const Rect cb0 = src.img.contentBounds();
    if (cb0.empty()) return false;
    const int pad = stylePad(src.style);
    const Rect u{src.x + cb0.x - pad, src.y + cb0.y - pad, cb0.w + pad * 2, cb0.h + pad * 2};
    Document tmp;
    tmp.createEmpty(u.w, u.h, Color(0, 0, 0, 0));
    tmp.layers.clear();
    Image img(u.w, u.h);
    if (src.kind == LayerKind::Text) {
        SkImageInfo info = SkImageInfo::Make(u.w, u.h, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        std::unique_ptr<SkCanvas> cv = SkCanvas::MakeRasterDirect(info, img.pixels(), (size_t)u.w * 4);
        if (!cv) return false;
        cv->translate((float)-u.x, (float)-u.y);
        drawTextLayers(*cv, SkMatrix::I(), u.w, u.h, index);
    } else {
        Layer c = src;
        c.x -= u.x;
        c.y -= u.y;
        tmp.layers.push_back(c);
        tmp.active = 0;
        img = tmp.flatten();
    }
    const Rect cb = img.contentBounds();
    if (cb.empty()) return false;
    Layer& l = layers[(size_t)index];
    l.img = cropRegion(img, cb);
    l.x = u.x + cb.x;
    l.y = u.y + cb.y;
    l.kind = LayerKind::Pixel;
    l.text = TextData{};
    l.textXf = SkMatrix::I();
    l.style = LayerStyle{};
    l.children.clear();
    l.opacity = 1.0f;
    l.styleCache = Image{};
    l.styleKey = StyleCompositeKey{};
    l.maskSrcKey = StyleGridKey{};
    l.maskStrokeKey = StyleStrokeKey{};
    l.maskShadowKey = StyleShadowKey{};
    ++structureRev_;
    markAllDirty();
    return true;
}

void Document::mergeDown(int index)
{
    if (index <= 0 || index >= (int)layers.size()) return;
    ++structureRev_;
    const auto needsBake = [](const Layer& l) {
        return styleActive(l.style) || !l.children.empty() || l.kind == LayerKind::Text;
    };
    if (needsBake(layers[(size_t)index - 1])) bakeLayerEffects(index - 1);
    if (index < (int)layers.size() && needsBake(layers[(size_t)index])) bakeLayerEffects(index);
    Layer& top = layers[(size_t)index];
    Layer& bottom = layers[(size_t)index - 1];

    const Rect u = layerRect(bottom).unite(layerRect(top));
    if (u.empty()) {
        layers.erase(layers.begin() + index);
        active = index - 1;
        selIds.clear();
        markAllDirty();
        return;
    }

    if (bottom.opacity < 1.0f && bottom.opacity >= 0.0f && !bottom.img.empty()) {
        bottom.img.detach();
        const float op = bottom.opacity;
        uint32_t* bp = bottom.img.pixels();
        const size_t n = (size_t)bottom.img.width() * (size_t)bottom.img.height();
        for (size_t k = 0; k < n; ++k)
            bp[k] = (bp[k] & 0x00FFFFFFu) | ((uint32_t)std::lround((bp[k] >> 24) * op) << 24);
        bottom.opacity = 1.0f;
    }

    Image out(u.w, u.h);
    if (!bottom.img.empty()) {
        const Image& botImg = bottom.img;
        const int dx = bottom.x - u.x, dy = bottom.y - u.y;
        for (int y = 0; y < botImg.height(); ++y)
            std::memcpy(out.data() + (size_t)(dy + y) * (size_t)u.w * 4 + (size_t)dx * 4,
                        botImg.data() + (size_t)y * (size_t)botImg.width() * 4,
                        (size_t)botImg.width() * 4);
    }
    if (top.visible && top.opacity > 0.0f && !top.img.empty()) {
        const float op = top.opacity;
        const Image& topImg = top.img;
        const uint32_t* topPx = topImg.pixels();
        const int tdx = top.x - u.x;
        for (int y = 0; y < topImg.height(); ++y) {
            const int ty = top.y - u.y + y;
            if (ty < 0 || ty >= u.h) continue;
            const int a = std::max(0, tdx), b = std::min(u.w, tdx + topImg.width());
            if (b <= a) continue;
            uint32_t* dst = out.pixels() + (size_t)ty * (size_t)u.w + (size_t)a;
            const uint32_t* src = topPx + (size_t)y * (size_t)topImg.width() + (size_t)(a - tdx);
            compositeRow(dst, src, (size_t)(b - a), op, top.blend);
        }
    }
    bottom.img = std::move(out);
    bottom.x = u.x;
    bottom.y = u.y;
    bottom.opacity = 1.0f;
    bottom.kind = LayerKind::Pixel;
    bottom.text = TextData{};
    bottom.textXf = SkMatrix::I();
    bottom.children.clear();
    bottom.style = LayerStyle{};
    bottom.styleCache = Image{};
    bottom.styleKey = StyleCompositeKey{};
    bottom.maskSrcKey = StyleGridKey{};
    bottom.maskStrokeKey = StyleStrokeKey{};
    bottom.maskShadowKey = StyleShadowKey{};
    layers.erase(layers.begin() + index);
    active = index - 1;
    selIds.clear();
    clampLayersState();
    markAllDirty();
}

bool Document::layerSelected(int index) const
{
    if (index < 0 || index >= (int)layers.size()) return false;
    if (index == active) return true;
    const int id = layers[(size_t)index].id;
    for (int s : selIds)
        if (s == id) return true;
    return false;
}

void Document::selectOnly(int index)
{
    if (index < 0 || index >= (int)layers.size()) return;
    selIds.clear();
    active = index;
}

void Document::activateWithinSelection(int index)
{
    if (index < 0 || index >= (int)layers.size()) return;
    const std::vector<int> all = selectedIndices();
    active = index;
    selIds.clear();
    for (int i : all)
        if (i != index) selIds.push_back(layers[(size_t)i].id);
}

void Document::toggleSelect(int index)
{
    if (index < 0 || index >= (int)layers.size()) return;
    const int id = layers[(size_t)index].id;
    if (index != active) {
        for (size_t k = 0; k < selIds.size(); ++k) {
            if (selIds[k] == id) {
                selIds.erase(selIds.begin() + (long)k);
                return;
            }
        }
        selIds.push_back(layers[(size_t)active].id);
        active = index;
        return;
    }
    if (selIds.empty()) return;
    active = indexOfId(selIds.back());
    selIds.pop_back();
}

void Document::selectRange(int index)
{
    if (index < 0 || index >= (int)layers.size()) return;
    if (active < 0 || active >= (int)layers.size()) {
        selectOnly(index);
        return;
    }
    const int a = std::min(active, index), b = std::max(active, index);
    selIds.clear();
    for (int i = a; i <= b; ++i)
        if (i != index) selIds.push_back(layers[(size_t)i].id);
    active = index;
}

void Document::selectAll()
{
    selIds.clear();
    for (int i = 0; i < (int)layers.size(); ++i)
        if (i != active) selIds.push_back(layers[(size_t)i].id);
}

std::vector<int> Document::selectedIndices() const
{
    std::vector<int> out;
    out.reserve(layers.size());
    for (int i = 0; i < (int)layers.size(); ++i)
        if (layerSelected(i)) out.push_back(i);
    return out;
}

void Document::moveLayers(const std::vector<int>& indices, int to)
{
    if (indices.empty()) return;
    to = std::clamp(to, 0, (int)layers.size() - 1);
    for (int i : indices)
        if (i == to) return;

    const int activeId = (active >= 0 && active < (int)layers.size()) ? layers[(size_t)active].id : 0;
    std::vector<Layer> block;
    block.reserve(indices.size());
    int below = 0;
    for (int i = (int)layers.size() - 1; i >= 0; --i) {
        bool in = false;
        for (int k : indices)
            if (k == i) { in = true; break; }
        if (!in) continue;
        if (i < to) ++below;
        block.insert(block.begin(), std::move(layers[(size_t)i]));
        layers.erase(layers.begin() + i);
    }
    const int dst = std::clamp(to - below + (to > indices.back() ? 1 : 0), 0, (int)layers.size());
    layers.insert(layers.begin() + dst, std::make_move_iterator(block.begin()), std::make_move_iterator(block.end()));

    active = indexOfId(activeId);
    if (active < 0) active = std::min((int)layers.size() - 1, dst);
    clampLayersState();
    ++structureRev_;
    markAllDirty();
}

void Document::mergeLayers(const std::vector<int>& indices)
{
    if (indices.size() < 2) return;
    const auto needsBake = [](const Layer& l) {
        return styleActive(l.style) || !l.children.empty() || l.kind == LayerKind::Text;
    };
    for (int i : indices)
        if (i >= 0 && i < (int)layers.size() && needsBake(layers[(size_t)i])) bakeLayerEffects(i);
    const int topIndex = indices.back();

    Rect u{};
    bool first = true;
    for (int i : indices) {
        const Rect r = layerRect(layers[(size_t)i]);
        u = first ? r : u.unite(r);
        first = false;
    }

    Image out;
    if (!u.empty()) {
        out.reset(u.w, u.h);
        for (int i : indices) {
            const Layer& l = layers[(size_t)i];
            if (!l.visible || l.opacity <= 0.0f || l.img.empty()) continue;
            const int dx = l.x - u.x;
            for (int y = 0; y < l.img.height(); ++y) {
                const int dy = l.y - u.y + y;
                if (dy < 0 || dy >= u.h) continue;
                const int a = std::max(0, dx), b = std::min(u.w, dx + l.img.width());
                if (b <= a) continue;
                uint32_t* dst = out.pixels() + (size_t)dy * (size_t)u.w + (size_t)a;
                const uint32_t* src = l.img.pixels() + (size_t)y * (size_t)l.img.width() + (size_t)(a - dx);
                compositeRow(dst, src, (size_t)(b - a), l.opacity, l.blend);
            }
        }
    }

    Layer& top = layers[(size_t)topIndex];
    top.img = std::move(out);
    top.x = u.empty() ? 0 : u.x;
    top.y = u.empty() ? 0 : u.y;
    top.opacity = 1.0f;
    top.blend = BlendMode::Normal;
    top.kind = LayerKind::Pixel;
    top.text = TextData{};
    top.lockAlpha = false;
    top.visible = true;
    top.textXf = SkMatrix::I();
    top.children.clear();
    top.style = LayerStyle{};
    top.styleCache = Image{};
    top.styleKey = StyleCompositeKey{};
    top.maskSrcKey = StyleGridKey{};
    top.maskStrokeKey = StyleStrokeKey{};
    top.maskShadowKey = StyleShadowKey{};

    int removedBelow = 0;
    for (int k = (int)indices.size() - 2; k >= 0; --k) {
        if (indices[(size_t)k] < topIndex) ++removedBelow;
        layers.erase(layers.begin() + indices[(size_t)k]);
    }
    active = topIndex - removedBelow;
    selIds.clear();
    ++structureRev_;
    clampLayersState();
    markAllDirty();
}

void Document::markDirty(Rect r, int layerHint)
{
    if (r.empty()) return;
    int pad = 0;
    if (layerHint >= 0 && layerHint < (int)layers.size())
        pad = stylePad(layers[(size_t)layerHint].style);
    else
        for (const auto& l : layers) pad = std::max(pad, stylePad(l.style));
    if (pad > 0) r = Rect{r.x - pad, r.y - pad, r.w + pad * 2, r.h + pad * 2};
    pending_ = pending_.unite(r);
    ++revision_;
    if (layerHint >= 0 && layerHint < (int)layers.size())
        ++layers[(size_t)layerHint].pixRev;
    else
        for (auto& l : layers) ++l.pixRev;
}

void Document::markRecompose(Rect r)
{
    if (r.empty()) return;
    pending_ = pending_.unite(r);
    ++revision_;
}

void Document::markAllDirty()
{
    pending_ = Rect{0, 0, width, height};
    ++revision_;
    for (auto& l : layers) ++l.pixRev;
}

void Document::bumpLayerRev(int index)
{
    if (index < 0 || index >= (int)layers.size()) return;
    ++layers[(size_t)index].pixRev;
    ++revision_;
}

void Document::syncTextLayers()
{
    if (textDirty_) refreshTextLayers();
}

Rect Document::layerCanvasRect(int index) const
{
    return layerContentBounds(index).intersect(boundsRect());
}

Rect Document::layerContentBounds(int index) const
{
    if (index < 0 || index >= (int)layers.size()) return Rect{};
    const Layer& l = layers[(size_t)index];
    if (l.bboxRev == l.pixRev) {
        if (l.bboxX == l.x && l.bboxY == l.y) return l.bboxCache;
        if (!l.bboxCache.empty()) {
            l.bboxCache = Rect{l.bboxCache.x + (l.x - l.bboxX), l.bboxCache.y + (l.y - l.bboxY), l.bboxCache.w,
                               l.bboxCache.h};
        }
        l.bboxX = l.x;
        l.bboxY = l.y;
        return l.bboxCache;
    }
    const Rect cb = l.img.contentBounds();
    l.bboxCache = cb.empty() ? cb : Rect{cb.x + l.x, cb.y + l.y, cb.w, cb.h};
    l.bboxRev = l.pixRev;
    l.bboxX = l.x;
    l.bboxY = l.y;
    return l.bboxCache;
}

Rect Document::layerVisualBounds(int index) const
{
    if (index < 0 || index >= (int)layers.size()) return Rect{};
    const Rect b = layerContentBounds(index);
    const int pad = stylePad(layers[(size_t)index].style);
    if (b.empty() || pad <= 0) return b;
    return Rect{b.x - pad, b.y - pad, b.w + pad * 2, b.h + pad * 2};
}

void Document::ensureLayerRect(int index, Rect need)
{
    if (index < 0 || index >= (int)layers.size()) return;
    Layer& l = layers[(size_t)index];
    need = need.unite(layerRect(l));
    if (need == layerRect(l)) return;

    const Rect cur = layerRect(l);
    int gx0 = need.x, gy0 = need.y, gx1 = need.right(), gy1 = need.bottom();
    if (cur.empty() || need.x < cur.x) gx0 = std::min(gx0, need.x - 32);
    if (cur.empty() || need.y < cur.y) gy0 = std::min(gy0, need.y - 32);
    if (cur.empty() || need.right() > cur.right()) gx1 = std::max(gx1, need.right() + 32);
    if (cur.empty() || need.bottom() > cur.bottom()) gy1 = std::max(gy1, need.bottom() + 32);
    const Rect grown = Rect::fromLTRB(gx0, gy0, gx1, gy1);
    if (grown == cur) return;

    Image out(grown.w, grown.h);
    if (!l.img.empty()) {
        const int dx = l.x - grown.x, dy = l.y - grown.y;
        for (int y = 0; y < l.img.height(); ++y)
            std::memcpy(out.data() + (size_t)(dy + y) * (size_t)grown.w * 4 + (size_t)dx * 4,
                        l.img.data() + (size_t)y * (size_t)l.img.width() * 4,
                        (size_t)l.img.width() * 4);
    }
    l.img = std::move(out);
    l.x = grown.x;
    l.y = grown.y;
    ++l.pixRev;
}


bool Document::hasUserMask(int index) const
{
    if (index < 0 || index >= (int)layers.size()) return false;
    const Layer& l = layers[(size_t)index];
    return !l.uMask.empty() && l.uMaskW == l.img.width() && l.uMaskH == l.img.height();
}

bool Document::ensureUserMask(int index)
{
    if (index < 0 || index >= (int)layers.size()) return false;
    Layer& l = layers[(size_t)index];
    const int w = l.img.width(), h = l.img.height();
    if (w <= 0 || h <= 0) return false;
    if (!hasUserMask(index)) {
        l.uMask.assign((size_t)w * (size_t)h, 255u);
        l.uMaskW = w;
        l.uMaskH = h;
        ++l.uMaskRev;
    }
    return true;
}

void Document::applyUserMask(int index, int band)
{
    if (!hasUserMask(index)) return;
    Layer& l = layers[(size_t)index];
    const int w = l.uMaskW, h = l.uMaskH;
    std::vector<float> soft;
    bool haveSoft = false;
    if (band > 0) {
        std::vector<uint8_t> tri = maskToTrimap(l.uMask, w, h, band);
        std::string err;
        haveSoft = closedFormAlpha(l.img, tri.data(), soft, &err);
        if (haveSoft) decontaminateForeground(l.img, soft, tri.data());
        else soft.clear();
    }
    uint32_t* p = l.img.pixels();
    for (int y = 0; y < h; ++y) {
        const uint8_t* m = &l.uMask[(size_t)y * (size_t)w];
        uint32_t* row = p + (size_t)y * (size_t)l.img.width();
        for (int x = 0; x < w; ++x) {
            const uint32_t v = row[x];
            float k = (float)m[x] / 255.0f;
            if (haveSoft) k = soft[(size_t)y * (size_t)w + (size_t)x];
            const uint32_t a = (uint32_t)(((v >> 24) & 0xffu) * k + 0.5f);
            row[x] = (v & 0x00ffffffu) | (a << 24);
        }
    }
    l.uMask.clear();
    l.uMask.shrink_to_fit();
    l.uMaskW = l.uMaskH = 0;
    ++l.uMaskRev;
    bumpLayerRev(index);
    markDirty(layerRect(l), index);
}

void Document::dropUserMask(int index)
{
    if (index < 0 || index >= (int)layers.size()) return;
    Layer& l = layers[(size_t)index];
    if (l.uMask.empty()) return;
    l.uMask.clear();
    l.uMask.shrink_to_fit();
    l.uMaskW = l.uMaskH = 0;
    ++l.uMaskRev;
    bumpLayerRev(index);
    markDirty(layerRect(l), index);
}

Rect Document::takeDirty()
{
    Rect r = pending_;
    pending_ = Rect{};
    return r;
}

void Document::compositeRangeInto(uint32_t* dst, int stride, Rect r, int from, int to, int skip,
                                  OpaqueReport* info, bool skipText) const
{
    if (r.empty() || !dst) return;

    from = std::max(0, from);
    to = std::min((int)layers.size(), to);
    const Rect canvas = Rect{0, 0, width, height}.intersect(r);
    const bool hasCanvas = !canvas.empty();

    for (int y = r.y; y < r.bottom(); ++y) {
        uint32_t* row = dst + (size_t)(y - r.y) * (size_t)stride;
        std::memset(row, 0, (size_t)r.w * 4);
        bool empty = true;
        for (int i = from; i < to; ++i) {
            if (i == skip) continue;
            const Layer& l = layers[(size_t)i];
            if (!l.visible || l.opacity <= 0.0f || l.img.empty()) continue;
            if (skipText && l.kind == LayerKind::Text && textHasOutline(l)) continue;

            int sox = 0, soy = 0;
            const Image& pix = styledPixels(l, sox, soy);
            const int lx = l.x + sox, ly = l.y + soy;

            if (y < ly || y >= ly + pix.height()) continue;
            const int a = std::max(r.x, lx), b = std::min(r.right(), lx + pix.width());
            if (b <= a) continue;
            const uint32_t* s = pix.pixels() + (size_t)(y - ly) * (size_t)pix.width() + (size_t)(a - lx);
            uint32_t* seg = row + (size_t)(a - r.x);
            const int w = b - a;

            if (empty) {
                if (l.blend == BlendMode::Normal && l.opacity >= 1.0f) {
                    std::memcpy(seg, s, (size_t)w * 4);
                } else if (l.blend == BlendMode::Normal) {
                    const float op = l.opacity;
                    for (int x = 0; x < w; ++x) {
                        const uint32_t v = s[x];
                        seg[x] = (v & 0x00ffffffu) | ((uint32_t)std::lround((float)(v >> 24) * op) << 24);
                    }
                } else {
                    std::memset(seg, 0, (size_t)w * 4);
                    compositeRow(seg, s, (size_t)w, l.opacity, l.blend);
                }
                empty = false;
                continue;
            }
            compositeRow(seg, s, (size_t)w, l.opacity, l.blend);
        }
        if (info) {
            const bool inCanvas = hasCanvas && y >= canvas.y && y < canvas.bottom();
            if (empty) {
                info->all = false;
                if (inCanvas) info->doc = false;
            } else {
                if (info->all) info->all = blend::allOpaque(row, (size_t)r.w);
                if (info->doc && !info->all && inCanvas)
                    info->doc = blend::allOpaque(row + (canvas.x - r.x), (size_t)canvas.w);
            }
        }
    }
}

void Document::recomposite(Rect r)
{
    if (!valid()) return;
    if (textDirty_) {
        refreshTextLayers();
        r = Rect{0, 0, width, height};
    }
    r = r.clampTo(width, height);
    if (r.empty()) return;
    ensureCache();
    compositeInto(cache_.pixels() + (size_t)r.y * (size_t)width + (size_t)r.x, width, r, -1);
}

void Document::resizeCanvas(int nw, int nh, int ox, int oy)
{
    nw = std::max(1, nw);
    nh = std::max(1, nh);

    for (auto& l : layers) {
        l.x += ox;
        l.y += oy;
    }

    width = nw;
    height = nh;
    ensureCache();
    markAllDirty();
}

void Document::resizeImage(int nw, int nh)
{
    nw = std::max(1, nw);
    nh = std::max(1, nh);
    if (!valid() || (nw == width && nh == height)) return;

    const float sx = (float)nw / (float)width;
    const float sy = (float)nh / (float)height;

    for (auto& l : layers) {
        if (l.kind == LayerKind::Text) {
            l.text.x *= sx;
            l.text.y *= sy;
            l.text.size = std::max(1, (int)std::lround((float)l.text.size * (sx + sy) * 0.5f));
            l.text.lineSpacing *= sy;
            continue;
        }
        if (l.img.empty()) continue;
        const int lw = std::max(1, (int)std::lround((float)l.img.width() * sx));
        const int lh = std::max(1, (int)std::lround((float)l.img.height() * sy));
        l.img = scaleImage(l.img, lw, lh);
        l.x = (int)std::lround((float)l.x * sx);
        l.y = (int)std::lround((float)l.y * sy);
    }

    width = nw;
    height = nh;
    textDirty_ = true;
    ensureCache();
    markAllDirty();
}

Image Document::flatten() const
{
    if (!valid()) return Image();
    Image out(width, height);
    compositeRangeInto(out.pixels(), width, Rect{0, 0, width, height}, 0, (int)layers.size(), -1, nullptr, true);
    drawTextOnto(out, SkMatrix::I());
    return out;
}

Image Document::flattenScaled(float s) const
{
    if (!valid()) return Image();
    if (!(s > 1.0f)) return flatten();
    const float eps = 0.01f;

    const double outPixels = (double)width * s * (double)height * s;
    if (outPixels > 256000000.0) return Image();

    Document tmp;
    tmp.width = std::max(1, (int)std::lround((float)width * s));
    tmp.height = std::max(1, (int)std::lround((float)height * s));
    tmp.layers.reserve(layers.size());
    for (const Layer& l : layers) {
        Layer c;
        c.id = l.id;
        c.name = l.name;
        c.visible = l.visible;
        c.opacity = l.opacity;
        c.blend = l.blend;
        c.lockAlpha = l.lockAlpha;
        c.kind = l.kind;
        c.x = (int)std::lround((float)l.x * s);
        c.y = (int)std::lround((float)l.y * s);
        c.style = l.style;
        c.style.strokeWidth = l.style.strokeWidth * s;
        c.style.shadowDist = l.style.shadowDist * s;
        c.style.shadowSize = l.style.shadowSize * s;
        if (l.kind == LayerKind::Text) {
            if (textHasOutline(l)) continue;
            c.kind = LayerKind::Pixel;
        }
        if (!l.img.empty()) {
            const int nw = std::max(1, (int)std::lround((float)l.img.width() * s));
            const int nh = std::max(1, (int)std::lround((float)l.img.height() * s));
            c.img.reset(nw, nh);
            const uint32_t* src = l.img.pixels();
            uint32_t* dst = c.img.pixels();
            const int sw = l.img.width(), sh = l.img.height();
            for (int y = 0; y < nh; ++y) {
                const int sy = std::min(sh - 1, (int)((float)y / s));
                for (int x = 0; x < nw; ++x) {
                    const int sx = std::min(sw - 1, (int)((float)x / s));
                    dst[(size_t)y * nw + x] = src[(size_t)sy * sw + sx];
                }
            }
        }
        tmp.layers.push_back(std::move(c));
    }
    tmp.active = active;
    if (tmp.width <= 0 || tmp.height <= 0) return Image();
    Image out(tmp.width, tmp.height);
    tmp.compositeInto(out.pixels(), out.width(), Rect{0, 0, out.width(), out.height()}, -1);
    drawTextOnto(out, SkMatrix::Scale(s, s));
    (void)eps;
    return out;
}

Image Document::flattenRegionScaled(float s, Rect region, bool includeText) const
{
    if (!valid()) return Image();
    if (!(s > 0.01f)) return Image();
    region = region.intersect(Rect{0, 0, width, height});
    if (region.w <= 0 || region.h <= 0) return Image();

    const int ow = std::max(1, (int)std::lround((float)region.w * s));
    const int oh = std::max(1, (int)std::lround((float)region.h * s));
    if ((double)ow * (double)oh > 100000000.0) return Image();

    Document tmp;
    tmp.width = ow;
    tmp.height = oh;
    tmp.active = active;
    tmp.layers.reserve(layers.size());
    for (const Layer& l : layers) {
        if (!l.visible) continue;
        Layer c;
        c.id = l.id;
        c.name = l.name;
        c.opacity = l.opacity;
        c.blend = l.blend;
        c.lockAlpha = l.lockAlpha;
        c.kind = l.kind;
        c.style = l.style;
        c.style.strokeWidth = l.style.strokeWidth * s;
        c.style.shadowDist = l.style.shadowDist * s;
        c.style.shadowSize = l.style.shadowSize * s;
        c.styleClip = Rect{0, 0, ow, oh};

        if (l.kind == LayerKind::Text) {
            if (textHasOutline(l)) continue;
            c.kind = LayerKind::Pixel;
        }

        c.x = (int)std::lround((float)l.x * s) - (int)std::lround((float)region.x * s);
        c.y = (int)std::lround((float)l.y * s) - (int)std::lround((float)region.y * s);
        const Rect lr{l.x, l.y, l.img.width(), l.img.height()};
        const Rect in = lr.intersect(region);
        if (!l.img.empty() && !in.empty()) {
            const int bx = (int)std::lround((float)in.x * s) - (int)std::lround((float)region.x * s);
            const int by = (int)std::lround((float)in.y * s) - (int)std::lround((float)region.y * s);
            const int bw = std::max(1, (int)std::lround((float)in.w * s));
            const int bh = std::max(1, (int)std::lround((float)in.h * s));
            c.img.reset(bw, bh);
            c.x = bx;
            c.y = by;
            const uint32_t* src = l.img.pixels();
            uint32_t* dst = c.img.pixels();
            const int sw = l.img.width(), sh = l.img.height();
            for (int y = 0; y < bh; ++y) {
                const int sy = std::clamp((int)(in.y + (float)y / s) - l.y, 0, sh - 1);
                for (int x = 0; x < bw; ++x) {
                    const int sx = std::clamp((int)(in.x + (float)x / s) - l.x, 0, sw - 1);
                    dst[(size_t)y * (size_t)bw + (size_t)x] = src[(size_t)sy * (size_t)sw + (size_t)sx];
                }
            }
        }
        tmp.layers.push_back(std::move(c));
    }

    Image out(ow, oh);
    tmp.compositeInto(out.pixels(), out.width(), Rect{0, 0, out.width(), out.height()}, -1);
    if (includeText) {
        const SkMatrix m = SkMatrix::Concat(SkMatrix::Translate(-(float)region.x * s, -(float)region.y * s),
                                            SkMatrix::Scale(s, s));
        drawTextOnto(out, m);
    }
    return out;
}

namespace {

sk_sp<SkColorFilter> textTriFilter(const Layer& l)
{
    for (const LayerChild& c : l.children) {
        if (c.kind != ChildKind::Tri || !c.active()) continue;
        float m[20] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
        const float ir = 1.0f - c.r, ig = 1.0f - c.g, ib = 1.0f - c.b;
        const float kr = 0.299f * ir, kg = 0.587f * ig, kb = 0.114f * ib;
        m[0] = c.r + kr;
        m[1] = kg;
        m[2] = kb;
        m[5] = kr;
        m[6] = c.g + kg;
        m[7] = kb;
        m[10] = kr;
        m[11] = kg;
        m[12] = c.b + kb;
        if (!c.keepLuma) {
            m[1] = m[2] = m[5] = m[7] = m[10] = m[11] = 0.0f;
            m[0] = c.r;
            m[6] = c.g;
            m[12] = c.b;
        }
        return SkColorFilters::Matrix(m);
    }
    return nullptr;
}

}

SkRect Document::layerDrawBounds(int index) const
{
    if (index < 0 || index >= (int)layers.size()) return SkRect::MakeEmpty();
    const Layer& l = layers[(size_t)index];
    if (l.kind == LayerKind::Text) {
        SkPath gp;
        if (!textGlyphPath(l.text, gp) || gp.isEmpty()) return SkRect::MakeEmpty();
        SkPath tp;
        gp.transform(l.textXf, &tp);
        return tp.getBounds();
    }
    const Rect cb = l.img.contentBounds();
    if (cb.empty()) return SkRect::MakeEmpty();
    return SkRect::MakeXYWH((float)(l.x + cb.x), (float)(l.y + cb.y), (float)cb.w, (float)cb.h);
}

void Document::drawTextOnto(Image& out, const SkMatrix& docToPix) const
{
    if (out.empty()) return;
    bool any = false;
    for (const Layer& l : layers) {
        if (l.kind == LayerKind::Text && l.visible && l.opacity > 0.0f) {
            any = true;
            break;
        }
    }
    if (!any) return;
    SkImageInfo info = SkImageInfo::Make(out.width(), out.height(), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    std::unique_ptr<SkCanvas> cv = SkCanvas::MakeRasterDirect(info, out.pixels(), (size_t)out.width() * 4);
    if (!cv) return;
    cv->concat(docToPix);
    drawTextLayers(*cv, docToPix, out.width(), out.height());
}

const SkPath* Document::cachedTextPath(const Layer& l) const
{
    unsigned long long h = 1469598103934665603ull;
    const auto mix = [&h](unsigned long long v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    for (unsigned char c : l.text.text) mix((unsigned long long)c + 7u);
    mix((unsigned long long)(unsigned int)l.text.size);
    mix((unsigned long long)(unsigned int)std::lround(l.text.lineSpacing * 64.0f));
    mix((unsigned long long)(unsigned int)std::lround(l.text.x * 64.0f) + 4096ull);
    mix((unsigned long long)(unsigned int)std::lround(l.text.y * 64.0f) + 4096ull);
    for (const char* p = l.text.font.c_str(); *p; ++p) mix((unsigned long long)(unsigned char)*p + 11u);
    const SkScalar m[9] = {l.textXf.getScaleX(), l.textXf.getSkewX(), l.textXf.getTranslateX(),
                           l.textXf.getSkewY(), l.textXf.getScaleY(), l.textXf.getTranslateY(),
                           l.textXf.getPerspX(), l.textXf.getPerspY(), 0.0f};
    for (int i = 0; i < 9; ++i) mix((unsigned long long)(unsigned int)std::lround(m[i] * 4096.0f));

    TextPathEntry& e = textPaths_[l.id];
    if (e.key == h && !e.path.isEmpty()) return &e.path;
    SkPath gp;
    if (!textGlyphPath(l.text, gp) || gp.isEmpty()) {
        e.key = 0;
        e.path.reset();
        return nullptr;
    }
    e.path = gp;
    e.key = h;
    return &e.path;
}

void Document::drawTextLayers(SkCanvas& canvas, const SkMatrix& docToDevice, int clipW, int clipH,
                              int onlyLayer) const
{
    (void)clipW;
    (void)clipH;
    const float dev = std::max(0.01f, (float)std::sqrt(std::fabs((double)docToDevice.getScaleX() *
                                                                 (double)docToDevice.getScaleY())));
    for (int li = 0; li < (int)layers.size(); ++li) {
        const Layer& l = layers[(size_t)li];
        if (onlyLayer >= 0 && li != onlyLayer) continue;
        if (l.kind != LayerKind::Text || !l.visible || l.opacity <= 0.0f) continue;
        const SkPath* pgp = cachedTextPath(l);
        if (!pgp) continue;
        SkPath tp;
        pgp->transform(l.textXf, &tp);
        const float op = l.opacity > 1.0f ? 1.0f : (l.opacity < 0.0f ? 0.0f : l.opacity);
        const float ta = (float)l.text.color.a / 255.0f * l.text.opacity * op;
        if (ta <= 0.0f) continue;
        const SkRect tb = tp.getBounds();
        const LayerStyle& st = l.style;
        (void)tb;

        const SkBlendMode docBlend = blend::skBlend(l.blend);

        if (st.shadowOn && st.shadowOpacity > 0.0f) {
            const float rad = st.shadowAngle * 3.14159265f / 180.0f;
            const float dx = std::cos(rad) * st.shadowDist;
            const float dy = -std::sin(rad) * st.shadowDist;
            SkPaint p;
            p.setAntiAlias(true);
            p.setColor4f(SkColor4f{st.shadowColor[0], st.shadowColor[1], st.shadowColor[2],
                                   st.shadowColor[3] * st.shadowOpacity / 100.0f * op});
            if (docBlend != SkBlendMode::kSrcOver) p.setBlendMode(docBlend);
            const float sigma = std::max(0.6f, st.shadowSize * 0.5f) * dev;
            p.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma));
            canvas.save();
            canvas.translate(dx, dy);
            canvas.drawPath(tp, p);
            canvas.restore();
        }

        if (st.strokeOn && st.strokeWidth >= 0.5f) {
            SkPaint p;
            p.setAntiAlias(true);
            p.setStyle(SkPaint::kStroke_Style);
            p.setStrokeJoin(SkPaint::kRound_Join);
            p.setColor4f(SkColor4f{st.strokeColor[0], st.strokeColor[1], st.strokeColor[2],
                                   st.strokeColor[3] * st.strokeOpacity / 100.0f * op});
            if (docBlend != SkBlendMode::kSrcOver) p.setBlendMode(docBlend);
            if (st.strokePos == StrokePos::Inside) {
                p.setStrokeWidth(st.strokeWidth * 2.0f);
                canvas.save();
                canvas.clipPath(tp, true);
                canvas.drawPath(tp, p);
                canvas.restore();
            } else {
                p.setStrokeWidth(st.strokePos == StrokePos::Center ? st.strokeWidth : st.strokeWidth * 2.0f);
                canvas.drawPath(tp, p);
            }
        }

        float fr = (float)l.text.color.r / 255.0f;
        float fg = (float)l.text.color.g / 255.0f;
        float fb = (float)l.text.color.b / 255.0f;
        if (st.overlayOn) {
            const float t = std::clamp(st.overlayOpacity / 100.0f, 0.0f, 1.0f);
            fr = fr * (1.0f - t) + st.overlayColor[0] * t;
            fg = fg * (1.0f - t) + st.overlayColor[1] * t;
            fb = fb * (1.0f - t) + st.overlayColor[2] * t;
        }
        SkPaint fp;
        fp.setAntiAlias(true);
        fp.setColor4f(SkColor4f{fr, fg, fb, ta});
        if (docBlend != SkBlendMode::kSrcOver) fp.setBlendMode(docBlend);
        if (sk_sp<SkColorFilter> cf = textTriFilter(l)) fp.setColorFilter(std::move(cf));
        canvas.drawPath(tp, fp);
    }
}

}
