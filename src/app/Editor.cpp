#include "app/Editor.h"

#include "core/Blend.h"
#include "core/Clipboard.h"
#include "core/ImageIo.h"
#include "core/LayerStyle.h"
#include "core/Ora.h"
#include "core/TextRender.h"
#include "core/Transform.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <utility>

namespace ie {

namespace {
constexpr float kPi = 3.14159265358979f;

bool sameTextData(const TextData& a, const TextData& b)
{
    return a.text == b.text && a.x == b.x && a.y == b.y && a.size == b.size &&
           a.lineSpacing == b.lineSpacing && a.opacity == b.opacity && a.color.r == b.color.r &&
           a.color.g == b.color.g && a.color.b == b.color.b && a.color.a == b.color.a;
}
}

Color Palette::fgColor() const
{
    return Color((uint8_t)(fg[0] * 255.0f + 0.5f), (uint8_t)(fg[1] * 255.0f + 0.5f), (uint8_t)(fg[2] * 255.0f + 0.5f),
                 (uint8_t)(fg[3] * 255.0f + 0.5f));
}

Color Palette::bgColor() const
{
    return Color((uint8_t)(bg[0] * 255.0f + 0.5f), (uint8_t)(bg[1] * 255.0f + 0.5f), (uint8_t)(bg[2] * 255.0f + 0.5f),
                 (uint8_t)(bg[3] * 255.0f + 0.5f));
}

void Palette::swap()
{
    for (int i = 0; i < 4; ++i) std::swap(fg[i], bg[i]);
}

void Palette::reset()
{
    for (int i = 0; i < 3; ++i) {
        fg[i] = 0.0f;
        bg[i] = 1.0f;
    }
    fg[3] = 1.0f;
    bg[3] = 1.0f;
}

void Palette::setFg(const float* c)
{
    for (int i = 0; i < 4; ++i) fg[i] = c[i];
}

void Palette::setBg(const float* c)
{
    for (int i = 0; i < 4; ++i) bg[i] = c[i];
}

void Editor::swapPalette()
{
    palette.swap();
}

namespace {

std::string baseName(const std::string& path)
{
    const size_t a = path.find_last_of("/\\");
    return (a == std::string::npos) ? path : path.substr(a + 1);
}

bool isOraFile(const std::string& path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = path.substr(dot);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e == ".ora";
}

}

void Editor::newDocument(int w, int h, Color bg)
{
    endMove();
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    doc.createEmpty(w, h, bg);
    sel.reset(doc.width, doc.height);
    hist.reset(doc);
    textSessionActive_ = false;
    textSessionLayerId_ = -1;
    view.fit(doc.width, doc.height);
    title = "无标题";
    filePath.clear();
    modified = false;
    pendingFit = true;
}

bool Editor::open(const std::string& path)
{
    endMove();
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    try {
        if (isOraFile(path)) {
            if (!ora::load(path, doc)) {
                return false;
            }
        } else {
            Image img;
            if (!imageio::load(path, img)) {
                return false;
            }
            doc.adopt(img.width(), img.height(), std::move(img), baseName(path));
        }
    } catch (const std::exception& e) {
        return false;
    } catch (...) {
        return false;
    }
    sel.reset(doc.width, doc.height);
    hist.reset(doc);
    textSessionActive_ = false;
    textSessionLayerId_ = -1;
    view.fit(doc.width, doc.height);
    filePath = path;
    title = baseName(path);
    modified = false;
    pendingFit = true;
    return true;
}

bool Editor::saveTo(const std::string& path)
{
    return saveTo(path, saveOpts);
}

bool Editor::saveTo(const std::string& path, const imageio::SaveOptions& opt)
{
    try {
        if (isOraFile(path)) {
            if (!ora::save(path, doc)) {
                return false;
            }
        } else {
            const Image flat = doc.flatten();
            if (flat.empty()) {
                return false;
            }
            if (!imageio::save(path, flat, opt)) {
                return false;
            }
        }
    } catch (const std::exception& e) {
        return false;
    } catch (...) {
        return false;
    }
    filePath = path;
    title = baseName(path);
    modified = false;
    return true;
}

void Editor::afterEdit(const char* label)
{
    modified = true;
}

Rect Editor::selectionOrCanvas() const
{
    if (sel.active && !sel.bounds().empty()) return sel.bounds();
    return Rect{0, 0, doc.width, doc.height};
}

void Editor::copySelection()
{
    const Layer* l = doc.activeLayer();
    if (!l) return;
    const Rect r = selectionOrCanvas();
    Image out(r.w, r.h);
    uint8_t* dst = out.data();
    const uint8_t* mask = sel.maskPtr();
    const float layerOp = l->opacity;
    const int lw = l->img.width(), lh = l->img.height();
    const uint32_t* src = l->img.pixels();
    for (int y = 0; y < r.h; ++y) {
        const int syi = r.y + y;
        const int ly = syi - l->y;
        if (ly < 0 || ly >= lh) continue;
        for (int x = 0; x < r.w; ++x) {
            const int sxi = r.x + x, lx = sxi - l->x;
            if (lx < 0 || lx >= lw) continue;
            const size_t di = ((size_t)y * r.w + x) * 4;
            if (mask && !(sxi >= 0 && syi >= 0 && sxi < doc.width && syi < doc.height)) continue;
            if (mask && !mask[(size_t)syi * doc.width + sxi]) continue;
            const uint32_t v = src[(size_t)ly * (size_t)lw + (size_t)lx];
            std::memcpy(dst + di, &v, 4);
            if (layerOp < 1.0f) dst[di + 3] = (uint8_t)(dst[di + 3] * layerOp + 0.5f);
        }
    }
    copyOrigin_ = Vec2i{r.x, r.y};
    hasCopyOrigin_ = true;
    clipboard::setImage(out);
}

void Editor::cutSelection()
{
    if (!doc.activeLayer()) return;
    copySelection();
    deleteSelection();
}

void Editor::deleteByKey()
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    if (sel.active && !sel.bounds().empty()) {
        deleteSelection();
        return;
    }
    deleteSelectedLayers();
}

void Editor::deleteSelection()
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Layer* l = editableLayer();
    if (!l || l->img.empty()) return;
    const Rect r = selectionOrCanvas();
    const Rect changed = r.intersect(layerRect(*l));
    if (changed.empty()) return;
    hist.record(doc, sel, "删除");
    l->img.detach();
    uint32_t* p = l->img.pixels();
    const uint8_t* mask = sel.maskPtr();
    const int lw = l->img.width(), lh = l->img.height();
    for (int y = changed.y; y < changed.bottom(); ++y) {
        const int ly = y - l->y;
        for (int x = changed.x; x < changed.right(); ++x) {
            const int lx = x - l->x;
            if (lx < 0 || lx >= lw || ly < 0 || ly >= lh) continue;
            if (mask && (x < 0 || y < 0 || x >= doc.width || y >= doc.height)) continue;
            if (mask && !mask[(size_t)y * doc.width + x]) continue;
            p[(size_t)ly * (size_t)lw + (size_t)lx] = 0;
        }
    }
    doc.markDirty(changed, doc.active);
    afterEdit("已删除");
}

namespace {
constexpr int kTextBoxHitPad = 4;

bool imageHasVisiblePixels(const Image& img)
{
    if (img.empty()) return false;
    const uint32_t* p = img.pixels();
    if (!p) return false;
    const size_t n = (size_t)img.width() * (size_t)img.height();
    for (size_t i = 0; i < n; ++i)
        if (((p[i] >> 24) & 0xffu) != 0) return true;
    return false;
}
}

void Editor::paste()
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Image img;
    if (!clipboard::getImage(img) || img.empty()) {
        return;
    }
    if (!imageHasVisiblePixels(img)) {
        return;
    }

    int ox = 0, oy = 0;
    if (hasCopyOrigin_) {
        ox = copyOrigin_.x;
        oy = copyOrigin_.y;
    } else if (sel.active && !sel.bounds().empty()) {
        ox = sel.bounds().x;
        oy = sel.bounds().y;
    } else {
        ox = std::max(0, (doc.width - img.width()) / 2);
        oy = std::max(0, (doc.height - img.height()) / 2);
    }
    ox = std::clamp(ox, 1 - img.width(), std::max(1 - img.width(), doc.width - 1));
    oy = std::clamp(oy, 1 - img.height(), std::max(1 - img.height(), doc.height - 1));

    hist.record(doc, sel, "粘贴");
    doc.addLayerAt(std::move(img), ox, oy, "粘贴");
    afterEdit("已粘贴");
}

void Editor::selectAll()
{
    Selection before = sel;
    sel.selectAll();
    recordSelectionChange(before, "全选");
}

void Editor::deselect()
{
    if (!sel.active) return;
    Selection before = sel;
    sel.clear();
    recordSelectionChange(before, "取消选择");
}

void Editor::invertSelection()
{
    if (!sel.active) sel.selectAll();
    Selection before = sel;
    sel.invert();
    recordSelectionChange(before, "反选");
}

void Editor::recordSelectionChange(const Selection& before, const char* label)
{
    if (before.sameAs(sel)) return;
    hist.record(doc, before, label);
}

bool Editor::colorAtPixel(int ix, int iy, Color& out) const
{
    if (!doc.valid()) return false;
    if (ix < 0 || iy < 0 || ix >= doc.width || iy >= doc.height) return false;
    uint32_t px = 0;
    doc.compositeInto(&px, 1, Rect{ix, iy, 1, 1}, -1, nullptr);
    out = Color::fromPacked(px);
    return true;
}

bool Editor::wandSelect(int x, int y, int tolerance, WandMode mode)
{
    if (!doc.valid()) return false;
    if (x < 0 || y < 0 || x >= doc.width || y >= doc.height) {
        return false;
    }
    Layer* l = doc.activeLayer();
    if (!l) {
        return false;
    }
    if (l->kind == LayerKind::Text) {
        doc.refreshTextLayers();
        l = doc.activeLayer();
        if (!l) return false;
    }
    Image only(doc.width, doc.height);
    if (!l->img.empty()) only.blit(l->img, l->x, l->y);
    const uint32_t* src = only.pixels();
    if (!src) return false;

    switch (mode) {
    case WandMode::Replace: sel.selectFlood(src, x, y, tolerance, false); break;
    case WandMode::Add: sel.selectFlood(src, x, y, tolerance, true); break;
    case WandMode::Subtract: sel.subtractFlood(src, x, y, tolerance); break;
    case WandMode::Intersect: sel.intersectFlood(src, x, y, tolerance); break;
    }
    return true;
}

bool Editor::bucketFill(int x, int y, int tolerance, BucketFill content)
{
    if (!doc.valid()) return false;
    if (x < 0 || y < 0 || x >= doc.width || y >= doc.height) {
        return false;
    }
    if (!editableLayer()) return false;

    Layer* lay = doc.activeLayer();
    if (!lay) return false;
    const int lw = lay->img.width(), lh = lay->img.height();
    const int lox = lay->x, loy = lay->y;
    const uint32_t* lp = lay->img.pixels();
    const auto layerPixel = [&](int cx, int cy) -> uint32_t {
        const int lx = cx - lox, ly = cy - loy;
        if (!lp || lx < 0 || ly < 0 || lx >= lw || ly >= lh) return 0;
        return lp[(size_t)ly * (size_t)lw + (size_t)lx];
    };

    const int W = doc.width, H = doc.height;
    const uint32_t seed = layerPixel(x, y);
    const int r0 = (int)(seed & 0xff), g0 = (int)((seed >> 8) & 0xff);
    const int b0 = (int)((seed >> 16) & 0xff), a0 = (int)(seed >> 24);
    const auto match = [&](uint32_t v) {
        return std::abs((int)(v & 0xff) - r0) <= tolerance && std::abs((int)((v >> 8) & 0xff) - g0) <= tolerance &&
               std::abs((int)((v >> 16) & 0xff) - b0) <= tolerance && std::abs((int)(v >> 24) - a0) <= tolerance;
    };

    const uint8_t* mask = sel.maskPtr();
    std::vector<uint8_t> seen((size_t)W * (size_t)H, 0);
    std::vector<int> region, stack;
    region.reserve(1024);
    stack.reserve(1024);
    const int start = y * W + x;
    if (mask && !mask[(size_t)start]) {
        return false;
    }
    seen[(size_t)start] = 1;
    stack.push_back(start);
    int minX = x, maxX = x, minY = y, maxY = y;
    while (!stack.empty()) {
        const int cur = stack.back();
        stack.pop_back();
        region.push_back(cur);
        const int cx = cur % W, cy = cur / W;
        if (cx < minX) minX = cx;
        if (cx > maxX) maxX = cx;
        if (cy < minY) minY = cy;
        if (cy > maxY) maxY = cy;
        const int nb[4] = {(cy > 0) ? cur - W : -1, (cy + 1 < H) ? cur + W : -1, (cx > 0) ? cur - 1 : -1,
                           (cx + 1 < W) ? cur + 1 : -1};
        for (int k = 0; k < 4; ++k) {
            const int idx = nb[k];
            if (idx < 0 || seen[(size_t)idx]) continue;
            seen[(size_t)idx] = 1;
            if (!match(layerPixel(idx % W, idx / W))) continue;
            if (mask && !mask[(size_t)idx]) continue;
            stack.push_back(idx);
        }
    }
    if (region.empty()) return false;

    const Rect area = Rect::fromLTRB(minX, minY, maxX + 1, maxY + 1);
    hist.record(doc, sel, "油漆桶");
    doc.ensureLayerRect(doc.active, area);
    Layer* l = doc.activeLayer();
    if (!l) return false;

    const uint32_t fgp = palette.fgColor().packed();
    const uint32_t bgp = palette.bgColor().packed();
    uint32_t* p = l->img.pixels();
    const int iw = l->img.width(), ih = l->img.height();
    for (int idx : region) {
        const int cx = idx % W, cy = idx / W;
        const int lx = cx - l->x, ly = cy - l->y;
        if (lx < 0 || ly < 0 || lx >= iw || ly >= ih) continue;
        uint32_t src = fgp;
        if (content == BucketFill::Background)
            src = bgp;
        else if (content == BucketFill::Pattern)
            src = ((((cx >> 3) + (cy >> 3)) & 1) ? fgp : bgp);
        p[(size_t)ly * (size_t)iw + (size_t)lx] =
            blend::paintPixel(p[(size_t)ly * (size_t)iw + (size_t)lx], src, 1.0f, l->lockAlpha);
    }

    doc.markDirty(area, doc.active);
    afterEdit("油漆桶");
    return true;
}


void Editor::cropToSelection()
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    if (!sel.active || sel.bounds().empty()) {
        return;
    }
    const Rect r = sel.bounds().intersect(doc.boundsRect());
    if (r.empty()) return;
    if (r.x == 0 && r.y == 0 && r.w == doc.width && r.h == doc.height) {
        return;
    }
    hist.record(doc, sel, "裁切");
    doc.resizeCanvas(r.w, r.h, -r.x, -r.y);
    sel.reset(doc.width, doc.height);
    view.fit(doc.width, doc.height);
    afterEdit("已裁切到选区");
}

void Editor::trimCanvas()
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    if (!doc.valid()) return;
    const Image flat = doc.flatten();
    const int W = flat.width(), H = flat.height();
    if (W < 2 || H < 2) return;

    const auto match = [&](uint32_t v) { return (v >> 24) == 0; };

    int top = 0;
    while (top < H - 1) {
        const uint32_t* row = flat.pixels() + (size_t)top * W;
        bool all = true;
        for (int x = 0; x < W; ++x)
            if (!match(row[(size_t)x])) { all = false; break; }
        if (!all) break;
        ++top;
    }
    int bottom = 0;
    while (bottom < H - 1 - top) {
        const uint32_t* row = flat.pixels() + (size_t)(H - 1 - bottom) * W;
        bool all = true;
        for (int x = 0; x < W; ++x)
            if (!match(row[(size_t)x])) { all = false; break; }
        if (!all) break;
        ++bottom;
    }
    int left = 0;
    while (left < W - 1) {
        bool all = true;
        for (int y = top; y < H - bottom; ++y)
            if (!match(flat.pixels()[(size_t)y * W + left])) { all = false; break; }
        if (!all) break;
        ++left;
    }
    int right = 0;
    while (right < W - 1 - left) {
        bool all = true;
        for (int y = top; y < H - bottom; ++y)
            if (!match(flat.pixels()[(size_t)y * W + (W - 1 - right)])) { all = false; break; }
        if (!all) break;
        ++right;
    }

    if (top + bottom + left + right == 0) {
        return;
    }
    hist.record(doc, sel, "自动裁切");
    doc.resizeCanvas(W - left - right, H - top - bottom, -left, -top);
    sel.reset(doc.width, doc.height);
    view.fit(doc.width, doc.height);
    afterEdit("已自动裁切边缘");
}

void Editor::resizeCanvas(int nw, int nh, int ox, int oy)
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    if (nw == doc.width && nh == doc.height && ox == 0 && oy == 0) {
        return;
    }
    hist.record(doc, sel, "画布大小");
    doc.resizeCanvas(nw, nh, ox, oy);
    sel.reset(doc.width, doc.height);
    view.fit(doc.width, doc.height);
    afterEdit("画布尺寸已修改");
}

void Editor::resizeCanvasAligned(int nw, int nh, int ax, int ay)
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    ax = std::clamp(ax, 0, 2);
    ay = std::clamp(ay, 0, 2);
    bool same = (nw == doc.width && nh == doc.height);
    if (same) {
        for (const auto& l : doc.layers) {
            const int tx = (int)std::lround((double)(nw - l.img.width()) * (ax * 0.5));
            const int ty = (int)std::lround((double)(nh - l.img.height()) * (ay * 0.5));
            if (l.x != tx || l.y != ty) { same = false; break; }
        }
    }
    if (same) {
        return;
    }
    hist.record(doc, sel, "画布大小");
    doc.resizeCanvas(nw, nh, 0, 0);
    for (auto& l : doc.layers) {
        l.x = (int)std::lround((double)(nw - l.img.width()) * (ax * 0.5));
        l.y = (int)std::lround((double)(nh - l.img.height()) * (ay * 0.5));
    }
    sel.reset(doc.width, doc.height);
    view.fit(doc.width, doc.height);
    afterEdit("画布尺寸已修改");
}

void Editor::resizeImage(int nw, int nh){
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    if (nw == doc.width && nh == doc.height) {
        return;
    }
    hist.record(doc, sel, "图像大小");
    doc.resizeImage(nw, nh);
    sel.reset(doc.width, doc.height);
    view.fit(doc.width, doc.height);
    afterEdit("图像尺寸已修改");
}

void Editor::strokeSelection(int width, Color c)
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Layer* l = editableLayer();
    if (!l || !sel.active || width <= 0) {
        return;
    }
    hist.record(doc, sel, "描边");
    const Rect r = sel.bounds();
    const int pad = width / 2 + 2;
    const Rect roi = Rect{r.x - pad, r.y - pad, r.w + pad * 2, r.h + pad * 2};
    doc.ensureLayerRect(doc.active, roi);
    l = doc.activeLayer();
    blend::strokeMask(l->img, l->x, l->y, sel.maskPtr(), doc.width, doc.height, width, c);
    doc.markDirty(roi, doc.active);
    afterEdit("描边");
}


static bool toLayerROI(const Layer& l, Rect& roi)
{
    const Rect t = roi.intersect(layerRect(l));
    if (t.empty()) return false;
    roi = Rect{t.x - l.x, t.y - l.y, t.w, t.h};
    return true;
}

void Editor::applyBrightnessContrast(float brightness, float contrast)
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Layer* l = editableLayer();
    if (!l || l->img.empty()) return;
    const Rect cspace = selectionOrCanvas();
    Rect r = cspace;
    if (!toLayerROI(*l, r)) return;
    if (brightness == 0.0f && contrast == 0.0f) return;
    hist.record(doc, sel, "亮/对比");
    l->img.detach();
    blend::brightnessContrast(l->img, r, brightness, contrast);
    doc.markDirty(cspace.intersect(layerRect(*l)), doc.active);
    afterEdit("亮/对比");
}

namespace {

Layer* layerById(Document& doc, int id)
{
    for (auto& l : doc.layers)
        if (l.id == id) return &l;
    return nullptr;
}

}

bool Editor::beginPixelPreview(std::string label)
{
    cancelPixelPreview();
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Layer* l = editableLayer();
    if (!l || l->img.empty()) return false;
    const Rect cspace = selectionOrCanvas();
    Rect roi = cspace;
    if (!toLayerROI(*l, roi)) return false;
    roi = roi.clampTo(l->img.width(), l->img.height());
    if (roi.empty()) return false;

    const bool sameSeg = segment_.layerId == l->id && segment_.roi == roi && !segment_.base.empty();
    if (sameSeg) {
        previewOrig_ = segment_.base;
    } else {
        previewOrig_.resize((size_t)roi.w * (size_t)roi.h);
        const uint32_t* src = l->img.pixels();
        for (int y = 0; y < roi.h; ++y)
            std::memcpy(&previewOrig_[(size_t)y * (size_t)roi.w],
                        src + (size_t)(roi.y + y) * (size_t)l->img.width() + roi.x,
                        sizeof(uint32_t) * (size_t)roi.w);
        segment_.layerId = l->id;
        segment_.roi = roi;
        segment_.base = previewOrig_;
    }

    hist.record(doc, sel, std::move(label));
    preview_.layerId = l->id;
    preview_.cspace = cspace;
    preview_.roi = roi;
    preview_.committed = false;
    return true;
}

void Editor::previewPixels(const std::function<void(Image&, Rect)>& op)
{
    if (preview_.layerId < 0) return;
    Layer* l = layerById(doc, preview_.layerId);
    if (!l || l->img.empty()) {
        cancelPixelPreview();
        return;
    }
    const Rect roi = preview_.roi.clampTo(l->img.width(), l->img.height());
    if (roi.empty() || roi.w != preview_.roi.w || roi.h != preview_.roi.h) {
        cancelPixelPreview();
        return;
    }
    l->img.detach();
    uint32_t* dst = l->img.pixels();
    for (int y = 0; y < roi.h; ++y)
        std::memcpy(dst + (size_t)(roi.y + y) * (size_t)l->img.width() + roi.x,
                    &previewOrig_[(size_t)y * (size_t)roi.w], sizeof(uint32_t) * (size_t)roi.w);
    op(l->img, roi);
    doc.markDirty(preview_.cspace.intersect(layerRect(*l)), doc.active);
    afterEdit("调整");
}

void Editor::commitPixelPreview()
{
    if (preview_.layerId < 0) return;
    preview_.committed = true;
    preview_.layerId = -1;
    previewOrig_.clear();
}

void Editor::cancelPixelPreview()
{
    if (preview_.layerId < 0) return;
    const bool committed = preview_.committed;
    preview_.layerId = -1;
    preview_.committed = false;
    previewOrig_.clear();
    if (committed) return;
    hist.dropLast(doc, sel);
}

void Editor::dropPixelPreviewSession()
{
    preview_.layerId = -1;
    preview_.committed = false;
    previewOrig_.clear();
}

void Editor::dropPixelSegment()
{
    segment_.layerId = -1;
    segment_.roi = Rect{};
    segment_.base.clear();
}

void Editor::applyInvert()
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Layer* l = editableLayer();
    if (!l || l->img.empty()) return;
    hist.record(doc, sel, "反相");
    const Rect cspace = selectionOrCanvas();
    Rect r = cspace;
    if (!toLayerROI(*l, r)) return;
    l->img.detach();
    blend::invert(l->img, r);
    doc.markDirty(cspace.intersect(layerRect(*l)), doc.active);
    afterEdit("反相");
}

void Editor::applyDesaturate()
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Layer* l = editableLayer();
    if (!l || l->img.empty()) return;
    hist.record(doc, sel, "去色");
    const Rect cspace = selectionOrCanvas();
    Rect r = cspace;
    if (!toLayerROI(*l, r)) return;
    l->img.detach();
    blend::desaturate(l->img, r);
    doc.markDirty(cspace.intersect(layerRect(*l)), doc.active);
    afterEdit("去色");
}

void Editor::applyThreshold(int level)
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Layer* l = editableLayer();
    if (!l || l->img.empty()) return;
    const Rect cspace = selectionOrCanvas();
    Rect r = cspace;
    if (!toLayerROI(*l, r)) return;
    hist.record(doc, sel, "阈值");
    l->img.detach();
    blend::threshold(l->img, r, level);
    doc.markDirty(cspace.intersect(layerRect(*l)), doc.active);
    afterEdit("阈值");
}

void Editor::applyHueShift(int degrees)
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Layer* l = editableLayer();
    if (!l || l->img.empty()) return;
    const Rect cspace = selectionOrCanvas();
    Rect r = cspace;
    if (!toLayerROI(*l, r)) return;
    if (degrees == 0) return;
    hist.record(doc, sel, "色相");
    l->img.detach();
    blend::hueShift(l->img, r, degrees);
    doc.markDirty(cspace.intersect(layerRect(*l)), doc.active);
    afterEdit("色相");
}

void Editor::mergeImageLayer()
{
    if (!doc.valid()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    const int idx = doc.active;
    if (idx < 0 || idx >= (int)doc.layers.size()) return;
    const Layer& l = doc.layers[(size_t)idx];
    if (!styleActive(l.style) && l.children.empty() && l.kind != LayerKind::Text) return;
    if (l.img.contentBounds().empty()) return;
    hist.record(doc, sel, "合并图像");
    doc.bakeLayerEffects(idx);
    doc.markAllDirty();
    afterEdit("合并图像");
}

void Editor::addEmptyLayer()
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    hist.record(doc, sel, "新建图层");
    doc.addEmptyLayer("图层 " + std::to_string(doc.layers.size() + 1));
    afterEdit("新建图层");
}

std::string Editor::textLayerName(const TextData& t)
{
    if (t.text.empty()) return "文字";
    std::string out = "文字: ";
    size_t i = 0, n = 0;
    while (i < t.text.size() && n < 12) {
        const unsigned char c = (unsigned char)t.text[i];
        const size_t step = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        if (t.text[i] == '\n') break;
        out.append(t.text, i, std::min(step, t.text.size() - i));
        i += step;
        ++n;
    }
    if (i < t.text.size()) out += "…";
    return out;
}

int Editor::addTextLayer(const TextData& t)
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    hist.record(doc, sel, "文字");
    const int idx = doc.addTextLayer(t, textLayerName(t));
    afterEdit("添加文字");
    return idx;
}

void Editor::requestTextEdit(int layerIndex, const Vec2f& at)
{
    if (layerIndex < 0 || layerIndex >= (int)doc.layers.size()) return;
    if (doc.layers[(size_t)layerIndex].kind != LayerKind::Text) return;
    textEditReqLayerId_ = doc.layers[(size_t)layerIndex].id;
    textEditReqAt_ = at;
}

bool Editor::requestTextEditAt(float clientX, float clientY)
{
    if (!doc.valid()) return false;
    const Vec2i ip = view.pixelAt(clientX, clientY, doc.width, doc.height);
    const int hit = layerIndexAt(ip.x, ip.y);
    if (hit < 0 || doc.layers[(size_t)hit].kind != LayerKind::Text) return false;
    requestTextEdit(hit, canvasImage(clientX, clientY));
    return true;
}

bool Editor::takeTextEditRequest(int& layerIndex, Vec2f& at)
{
    if (textEditReqLayerId_ < 0) return false;
    const int id = textEditReqLayerId_;
    const Vec2f p = textEditReqAt_;
    textEditReqLayerId_ = -1;
    const int idx = doc.indexOfId(id);
    layerIndex = idx;
    at = p;
    return idx >= 0 && doc.layers[(size_t)idx].kind == LayerKind::Text;
}

void Editor::setTextContent(int index, const std::string& text)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Layer& l = doc.layers[(size_t)index];
    if (l.kind != LayerKind::Text) return;
    hist.record(doc, sel, "编辑文字");
    l.text.text = text;
    l.name = textLayerName(l.text);
    doc.touchStructure();
    doc.refreshTextLayers();
    afterEdit("编辑文字");
}

bool Editor::beginTextSession(int index)
{
    if (!doc.valid() || index < 0 || index >= (int)doc.layers.size()) return false;
    if (doc.layers[(size_t)index].kind != LayerKind::Text) return false;
    if (textSessionActive_) cancelTextSession();
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    textSessionModifiedPrev_ = modified;
    hist.record(doc, sel, "编辑文字");
    textSessionActive_ = true;
    textSessionWasNew_ = false;
    textSessionLayerId_ = doc.layers[(size_t)index].id;
    textSessionOriginal_ = doc.layers[(size_t)index].text;
    return true;
}

int Editor::beginNewTextSession(const TextData& t)
{
    if (!doc.valid()) return -1;
    if (textSessionActive_) cancelTextSession();
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    textSessionModifiedPrev_ = modified;
    hist.record(doc, sel, "文字");
    const int idx = doc.addTextLayer(t, textLayerName(t));
    textSessionActive_ = true;
    textSessionWasNew_ = true;
    textSessionLayerId_ = doc.layers[(size_t)idx].id;
    modified = true;
    return idx;
}

bool Editor::updateTextEditing(const std::string& text)
{
    if (!textSessionActive_) return false;
    const int idx = doc.indexOfId(textSessionLayerId_);
    if (idx < 0) return false;
    Layer& l = doc.layers[(size_t)idx];
    if (l.kind != LayerKind::Text || l.text.text == text) return false;
    l.text.text = text;
    const std::string name = textLayerName(l.text);
    if (l.name != name) {
        l.name = name;
        doc.touchStructure();
    }
    doc.refreshTextLayers();
    modified = true;
    return true;
}

void Editor::commitTextSession()
{
    if (!textSessionActive_) return;
    textSessionActive_ = false;
    const int idx = doc.indexOfId(textSessionLayerId_);
    textSessionLayerId_ = -1;

    const bool alive = idx >= 0;
    const bool empty = !alive || doc.layers[(size_t)idx].kind != LayerKind::Text ||
                       doc.layers[(size_t)idx].text.text.empty();
    if (!textSessionWasNew_ && alive && doc.layers[(size_t)idx].kind == LayerKind::Text &&
        sameTextData(doc.layers[(size_t)idx].text, textSessionOriginal_)) {
        hist.dropLast(doc, sel);
        modified = textSessionModifiedPrev_;
        sel.reset(doc.width, doc.height);
        return;
    }
    if (empty) {
        if (textSessionWasNew_) {
            hist.dropLast(doc, sel);
            modified = textSessionModifiedPrev_;
        } else if (alive && doc.layers.size() > 1) {
            doc.removeLayer(idx);
        } else {
        }
        sel.reset(doc.width, doc.height);
        return;
    }
    modified = true;
    sel.reset(doc.width, doc.height);
}

void Editor::cancelTextSession()
{
    if (!textSessionActive_) return;
    textSessionActive_ = false;
    textSessionLayerId_ = -1;
    hist.dropLast(doc, sel);
    sel.reset(doc.width, doc.height);
    modified = textSessionModifiedPrev_;
}

void Editor::applyTextStyle(int index, int size, float lineSpacing, float opacity, const float rgba[4])
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Layer& l = doc.layers[(size_t)index];
    if (l.kind != LayerKind::Text) return;
    l.text.size = std::max(1, size);
    l.text.lineSpacing = lineSpacing;
    l.text.opacity = std::clamp(opacity, 0.0f, 1.0f);
    l.text.color = Color((uint8_t)std::lround(std::clamp(rgba[0], 0.0f, 1.0f) * 255.0f),
                         (uint8_t)std::lround(std::clamp(rgba[1], 0.0f, 1.0f) * 255.0f),
                         (uint8_t)std::lround(std::clamp(rgba[2], 0.0f, 1.0f) * 255.0f),
                         (uint8_t)std::lround(std::clamp(rgba[3], 0.0f, 1.0f) * 255.0f));
    doc.refreshTextLayers();
    if (stepOpen_) recordStep("文字样式");
    modified = true;
}

void Editor::applyTextFont(int index, const std::string& family)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Layer& l = doc.layers[(size_t)index];
    if (l.kind != LayerKind::Text) return;
    if (l.text.font == family) return;
    l.text.font = textFontIndex(family) >= 0 ? family : std::string();
    doc.refreshTextLayers();
    modified = true;
}

void Editor::demoteTextLayer(int index)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    if (doc.layers[(size_t)index].kind != LayerKind::Text) return;
    doc.demoteTextLayer(index);
}

Layer* Editor::editableLayer()
{
    Layer* l = doc.activeLayer();
    if (!l) return nullptr;
    if (l->kind == LayerKind::Text) {
        doc.demoteTextLayer(doc.active);
    }
    return doc.activeLayer();
}

bool Editor::activeHasContent() const
{
    if (doc.active < 0 || doc.active >= (int)doc.layers.size()) return false;
    if (doc.layers[(size_t)doc.active].kind == LayerKind::Text) return true;
    return !doc.layerContentBounds(doc.active).empty();
}

bool Editor::activeHasPixels() const
{
    if (doc.active < 0 || doc.active >= (int)doc.layers.size()) return false;
    if (doc.layers[(size_t)doc.active].kind == LayerKind::Text) return false;
    return !doc.layerContentBounds(doc.active).empty();
}

bool Editor::placeImageAsLayer(const std::string& path)
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    if (isOraFile(path)) return placeOraAsLayers(path);
    Image img;
    if (!imageio::load(path, img) || img.empty()) {
        return false;
    }
    std::string name = path;
    const size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos) name = name.substr(slash + 1);

    const int ox = (doc.width - img.width()) / 2;
    const int oy = (doc.height - img.height()) / 2;
    hist.record(doc, sel, "置入图像");
    doc.addLayerAt(std::move(img), ox, oy, name);
    afterEdit("已置入图像为图层");
    return true;
}

void Editor::undo()
{
    if (mw_.active) {
        undoMeshStep();
        return;
    }
    if (textSessionActive_) {
        cancelTextSession();
        return;
    }
    endMove();
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    dropPixelPreviewSession();
    endEditStep();
    if (!hist.canUndo()) return;
    endMove();
    hist.undo(doc, sel);
    doc.markAllDirty();
}

void Editor::redo()
{
    if (mw_.active) {
        redoMeshStep();
        return;
    }
    endMove();
    if (ft_.active) cancelFreeTransform();
    dropPixelPreviewSession();
    endEditStep();
    if (!hist.canRedo()) return;
    endMove();
    hist.redo(doc, sel);
    doc.markAllDirty();
}

void Editor::setActiveLayer(int index)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    if (index == doc.active) {
        doc.selectOnly(index);
        return;
    }
    endMove();
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    doc.selectOnly(index);
}

void Editor::toggleLayerSelection(int index)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    doc.toggleSelect(index);
}

void Editor::extendLayerSelection(int index){
    if (index < 0 || index >= (int)doc.layers.size()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    endMove();
    doc.selectRange(index);
}

void Editor::selectAllLayers()
{
    if (doc.layers.empty()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    endMove();
    doc.selectAll();
}

void Editor::moveSelectedLayers(int to)
{
    const std::vector<int> idx = doc.selectedIndices();
    if (idx.empty() || to < 0 || to >= (int)doc.layers.size()) return;
    for (int i : idx)
        if (i == to) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    hist.record(doc, sel, "移动图层");
    doc.moveLayers(idx, to);
    afterEdit("移动图层");
}

void Editor::mergeSelectedLayers()
{
    const std::vector<int> idx = doc.selectedIndices();
    if (idx.size() < 2) {
        return;
    }
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    hist.record(doc, sel, "合并图层");
    doc.mergeLayers(idx);
    afterEdit("合并图层");
}

void Editor::setLayerVisible(int index, bool visible)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    doc.layers[(size_t)index].visible = visible;
    doc.touchStructure();
    markLayerDirty(index);
    modified = true;
}

void Editor::renameLayer(int index, const char* name)
{
    if (index < 0 || index >= (int)doc.layers.size() || !name) return;
    doc.layers[(size_t)index].name = name;
    doc.touchStructure();
    modified = true;
}

void Editor::duplicateLayer(int index)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    hist.record(doc, sel, "复制图层");
    doc.duplicateLayer(index);
    afterEdit("复制图层");
}

void Editor::removeLayer(int index)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    hist.record(doc, sel, "删除图层");
    doc.removeLayer(index);
    afterEdit("删除图层");
}

void Editor::deleteSelectedLayers()
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    std::vector<int> idxs = doc.selectedIndices();
    if (idxs.empty() && doc.active >= 0) idxs.push_back(doc.active);
    if (idxs.empty()) return;
    hist.record(doc, sel, "删除图层");
    if (doc.layers.size() <= 1) {
        doc.removeLayer(doc.active >= 0 ? doc.active : 0);
        afterEdit("删除图层");
        return;
    }
    for (int i = (int)idxs.size() - 1; i >= 0; --i) {
        const int idx = idxs[(size_t)i];
        if (idx < 0 || idx >= (int)doc.layers.size()) continue;
        if (doc.layers.size() <= 1) break;
        doc.removeLayer(idx);
    }
    afterEdit("删除图层");
}

void Editor::moveLayer(int index, int to)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    if (to < 0 || to >= (int)doc.layers.size() || to == index) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    hist.record(doc, sel, "移动图层");
    doc.moveLayer(index, to);
    afterEdit("移动图层");
}

void Editor::mergeLayerDown(int index)
{
    if (index <= 0 || index >= (int)doc.layers.size()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    hist.record(doc, sel, "合并图层");
    doc.mergeDown(index);
    afterEdit("合并图层");
}

void Editor::beginLayerOpacityEdit()
{
    beginEditStep("图层不透明度");
}

void Editor::beginEditStep(const char* label)
{
    stepOpen_ = true;
    stepRecorded_ = false;
    stepLabel_ = label ? label : "编辑";
}

void Editor::endEditStep()
{
    stepOpen_ = false;
    stepRecorded_ = false;
    stepLabel_.clear();
}

void Editor::recordStep(const char* label)
{
    if (stepOpen_) {
        if (stepRecorded_) return;
        stepRecorded_ = true;
        hist.record(doc, sel, stepLabel_.empty() ? label : stepLabel_);
        return;
    }
    hist.record(doc, sel, label);
}

void Editor::centerActiveLayer()
{
    if (doc.active < 0) return;
    std::vector<int> list = doc.selectedIndices();
    if (list.empty()) list.push_back(doc.active);
    const bool multi = list.size() > 1;

    std::vector<std::pair<int, int>> delta(list.size(), {0, 0});
    std::vector<bool> hasContent(list.size(), false);
    Rect before, after;
    size_t moved = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        const int k = list[i];
        const Rect content = doc.layerContentBounds(k);
        if (content.empty()) continue;
        hasContent[i] = true;
        const double cx = (double)content.x + (double)content.w * 0.5;
        const double cy = (double)content.y + (double)content.h * 0.5;
        const int dx = (int)std::lround((double)doc.width * 0.5 - cx);
        const int dy = (int)std::lround((double)doc.height * 0.5 - cy);
        delta[i] = {dx, dy};
        before = before.unite(doc.layerVisualBounds(k));
        if (dx != 0 || dy != 0) ++moved;
    }

    if (moved == 0) return;

    hist.record(doc, sel, "图层居中");
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    for (size_t i = 0; i < list.size(); ++i) {
        if (delta[i].first == 0 && delta[i].second == 0) continue;
        Layer& l = doc.layers[(size_t)list[i]];
        l.x += delta[i].first;
        l.y += delta[i].second;
        if (l.kind == LayerKind::Text) {
            l.text.x += (float)delta[i].first;
            l.text.y += (float)delta[i].second;
        }
        after = after.unite(doc.layerVisualBounds(list[i]));
    }
    doc.markRecompose(before.unite(after));
    modified = true;
    afterEdit("图层居中");
}

void Editor::setLayerOpacity(int index, float opacity)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    const float v = std::clamp(opacity, 0.0f, 1.0f);
    if (doc.layers[(size_t)index].opacity == v) return;
    recordStep("图层不透明度");
    doc.layers[(size_t)index].opacity = v;
    markLayerDirty(index);
    modified = true;
}

bool Editor::cancelMoveRestore()
{
    if (!moveActive_) return false;
    const bool recorded = moveRecorded_;
    if (moveLayer_ >= 0 && moveLayer_ < (int)doc.layers.size()) {
        const Rect oldB = doc.layerVisualBounds(moveLayer_);
        Layer& l = doc.layers[(size_t)moveLayer_];
        l.x = moveBaseX_;
        l.y = moveBaseY_;
        moveDx_ = moveDy_ = 0;
        doc.markRecompose(oldB.unite(doc.layerVisualBounds(moveLayer_)));
    }
    if (recorded) hist.dropLast(doc, sel);
    moveDx_ = moveDy_ = 0;
    endMove();
    return true;
}

int Editor::importLayer(const Layer& src)
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    hist.record(doc, sel, "置入图层");
    int idx = -1;
    if (src.kind == LayerKind::Text) {
        idx = doc.addTextLayer(src.text, src.name);
    } else {
        doc.addLayerAt(src.img, src.x, src.y, src.name);
        idx = doc.active;
    }
    if (idx < 0 || idx >= (int)doc.layers.size()) return -1;
    Layer& l = doc.layers[(size_t)idx];
    l.visible = src.visible;
    l.opacity = src.opacity;
    l.blend = src.blend;
    l.lockAlpha = src.lockAlpha;
    l.style = clampStyleLimits(src.style);
    l.children = src.children;
    for (auto& c : l.children)
        if (c.id != 0) c.id = doc.claimChildId();
    l.styleCache = Image{};
    l.styleKey = StyleCompositeKey{};
    l.maskSrcKey = StyleGridKey{};
    l.maskStrokeKey = StyleStrokeKey{};
    l.maskShadowKey = StyleShadowKey{};
    doc.touchStructure();
    afterEdit("置入图层");
    return idx;
}

bool Editor::placeOraAsLayers(const std::string& path)
{
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    Document srcDoc;
    if (!ora::load(path, srcDoc)) {
        return false;
    }
    if (srcDoc.layers.empty()) {
        return false;
    }
    if (!doc.valid()) {
        return false;
    }

    const int ox = (doc.width - srcDoc.width) / 2;
    const int oy = (doc.height - srcDoc.height) / 2;

    hist.record(doc, sel, "置入 ORA");
    int added = 0;
    for (const Layer& l : srcDoc.layers) {
        if (l.kind == LayerKind::Text) {
            TextData t = l.text;
            t.x += (float)ox;
            t.y += (float)oy;
            const int at = doc.addTextLayer(t, l.name);
            if (at < 0) continue;
            Layer& dst = doc.layers[(size_t)at];
            dst.visible = l.visible;
            dst.opacity = l.opacity;
            dst.blend = l.blend;
            dst.lockAlpha = l.lockAlpha;
            dst.style = clampStyleLimits(l.style);
            dst.styleCache = Image{};
            dst.styleKey = StyleCompositeKey{};
            dst.maskSrcKey = StyleGridKey{};
            dst.maskStrokeKey = StyleStrokeKey{};
            dst.maskShadowKey = StyleShadowKey{};
            ++added;
            continue;
        }
        const size_t base = doc.layers.size();
        doc.addLayerAt(l.img, l.x + ox, l.y + oy, l.name);
        if (doc.layers.size() != base + 1) continue;
        Layer& dst = doc.layers[base];
        dst.visible = l.visible;
        dst.opacity = l.opacity;
        dst.blend = l.blend;
        dst.lockAlpha = l.lockAlpha;
        dst.style = clampStyleLimits(l.style);
        dst.styleCache = Image{};
        dst.styleKey = StyleCompositeKey{};
        dst.maskSrcKey = StyleGridKey{};
        dst.maskStrokeKey = StyleStrokeKey{};
        dst.maskShadowKey = StyleShadowKey{};
        ++added;
    }
    if (added == 0) {
        hist.dropLast(doc, sel);
        return false;
    }
    doc.touchStructure();
    afterEdit("置入 ORA 图层");
    return true;
}

void Editor::markLayerDirty(int index)
{
    const Rect b = doc.layerVisualBounds(index);
    if (b.empty()) return;
    doc.markRecompose(b);
}

void Editor::setLayerBlendMode(int index, BlendMode mode)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    Layer& l = doc.layers[(size_t)index];
    if (l.blend == mode) return;
    hist.record(doc, sel, "混合模式");
    l.blend = mode;
    markLayerDirty(index);
    afterEdit(blendModeName(mode));
}

void Editor::beginLayerStyleEdit()
{
    beginEditStep("图层样式");
}

static bool styleEquals(const LayerStyle& a, const LayerStyle& b)
{
    if (a.strokeOn != b.strokeOn || a.strokeWidth != b.strokeWidth || a.strokePos != b.strokePos ||
        a.strokeOpacity != b.strokeOpacity)
        return false;
    for (int i = 0; i < 4; ++i)
        if (a.strokeColor[i] != b.strokeColor[i]) return false;
    if (a.shadowOn != b.shadowOn || a.shadowDist != b.shadowDist || a.shadowSize != b.shadowSize ||
        a.shadowAngle != b.shadowAngle || a.shadowOpacity != b.shadowOpacity)
        return false;
    for (int i = 0; i < 4; ++i)
        if (a.shadowColor[i] != b.shadowColor[i]) return false;
    if (a.overlayOn != b.overlayOn || a.overlayOpacity != b.overlayOpacity) return false;
    for (int i = 0; i < 4; ++i)
        if (a.overlayColor[i] != b.overlayColor[i]) return false;
    return true;
}

void Editor::applyLayerSettings(int index, BlendMode blend, float opacity, bool lockAlpha, const LayerStyle& style)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    Layer& l = doc.layers[(size_t)index];
    const float op = std::clamp(opacity, 0.0f, 1.0f);
    const LayerStyle st = clampStyleLimits(style);
    if (l.blend == blend && l.opacity == op && l.lockAlpha == lockAlpha && styleEquals(l.style, st)) {
        return;
    }
    recordStep("图层样式");
    const Rect before = doc.layerVisualBounds(index);
    l.blend = blend;
    l.opacity = op;
    l.lockAlpha = lockAlpha;
    l.style = st;
    const Rect after = doc.layerVisualBounds(index);
    doc.markRecompose(before.unite(after));
    afterEdit("图层样式");
}

void Editor::setLayerLockAlpha(int index, bool lock)
{
    if (index < 0 || index >= (int)doc.layers.size()) return;
    Layer& l = doc.layers[(size_t)index];
    if (l.lockAlpha == lock) return;
    l.lockAlpha = lock;
}

LayerChild* Editor::triChild(int index)
{
    if (index < 0 || index >= (int)doc.layers.size()) return nullptr;
    for (auto& c : doc.layers[(size_t)index].children)
        if (c.kind == ChildKind::Tri) return &c;
    return nullptr;
}

const LayerChild* Editor::triChild(int index) const
{
    if (index < 0 || index >= (int)doc.layers.size()) return nullptr;
    for (const auto& c : doc.layers[(size_t)index].children)
        if (c.kind == ChildKind::Tri) return &c;
    return nullptr;
}

LayerChild& Editor::ensureTriChild(int index)
{
    static LayerChild empty;
    if (index < 0 || index >= (int)doc.layers.size()) return empty;
    Layer& l = doc.layers[(size_t)index];
    for (auto& c : l.children)
        if (c.kind == ChildKind::Tri) return c;
    LayerChild c;
    c.kind = ChildKind::Tri;
    c.id = doc.claimChildId();
    l.children.push_back(c);
    doc.touchStructure();
    return l.children.back();
}

LayerChild* Editor::liftChild(int index)
{
    if (index < 0 || index >= (int)doc.layers.size()) return nullptr;
    for (auto& c : doc.layers[(size_t)index].children)
        if (c.kind == ChildKind::Lift) return &c;
    return nullptr;
}

const LayerChild* Editor::liftChild(int index) const
{
    if (index < 0 || index >= (int)doc.layers.size()) return nullptr;
    for (const auto& c : doc.layers[(size_t)index].children)
        if (c.kind == ChildKind::Lift) return &c;
    return nullptr;
}

LayerChild& Editor::ensureLiftChild(int index)
{
    static LayerChild empty;
    if (index < 0 || index >= (int)doc.layers.size()) return empty;
    Layer& l = doc.layers[(size_t)index];
    for (auto& c : l.children)
        if (c.kind == ChildKind::Lift) return c;
    LayerChild c;
    c.kind = ChildKind::Lift;
    c.id = doc.claimChildId();
    l.children.push_back(c);
    doc.touchStructure();
    return l.children.back();
}

bool Editor::applyLayerLift(int index, float strength, float radius, float sensitivity, float softness,
                            float maxGain)
{
    if (index < 0 || index >= (int)doc.layers.size()) return false;
    strength = std::clamp(strength, 0.0f, 1.0f);
    radius = std::clamp(radius, 0.0f, 200.0f);
    sensitivity = std::clamp(sensitivity, 0.2f, 0.9f);
    softness = std::clamp(softness, 0.0f, 200.0f);
    maxGain = std::clamp(maxGain, 1.5f, 6.0f);
    const LayerChild* old = liftChild(index);
    if (old && old->liftStrength == strength && old->liftRadius == radius &&
        old->liftSensitivity == sensitivity && old->liftSoftness == softness && old->liftMaxGain == maxGain)
        return false;
    if (!old && strength <= 0.0f && radius <= 0.0f) return false;
    recordStep("衡影");
    LayerChild& c = ensureLiftChild(index);
    c.liftStrength = strength;
    c.liftRadius = radius;
    c.liftSensitivity = sensitivity;
    c.liftSoftness = softness;
    c.liftMaxGain = maxGain;
    doc.bumpLayerRev(index);
    doc.markRecompose(doc.layerVisualBounds(index));
    afterEdit("衡影");
    return true;
}

void Editor::beginChildEdit(const char* label)
{
    beginEditStep(label);
}

bool Editor::applyLayerTri(int index, float r, float g, float b, bool keepLuma)
{
    if (index < 0 || index >= (int)doc.layers.size()) return false;
    r = std::clamp(r, 0.0f, 2.0f);
    g = std::clamp(g, 0.0f, 2.0f);
    b = std::clamp(b, 0.0f, 2.0f);
    const LayerChild* old = triChild(index);
    if (old && old->r == r && old->g == g && old->b == b && old->keepLuma == keepLuma) return false;
    if (!old && r == 1.0f && g == 1.0f && b == 1.0f) return false;
    recordStep("三色");
    LayerChild& c = ensureTriChild(index);
    if (c.kind != ChildKind::Tri) return false;
    c.r = r;
    c.g = g;
    c.b = b;
    c.keepLuma = keepLuma;
    doc.bumpLayerRev(index);
    doc.markRecompose(doc.layerVisualBounds(index));
    afterEdit("三色");
    return true;
}

bool Editor::setTriChildOn(int index, bool on)
{
    if (index < 0 || index >= (int)doc.layers.size()) return false;
    LayerChild* c = triChild(index);
    if (!c || c->on == on) return false;
    hist.record(doc, sel, on ? "启用三色" : "停用三色");
    c->on = on;
    doc.markRecompose(doc.layerVisualBounds(index));
    afterEdit(on ? "启用三色" : "停用三色");
    return true;
}

Rect Editor::activeLayerBounds() const
{
    if (doc.active < 0) return Rect{};
    return doc.layerContentBounds(doc.active);
}

bool Editor::dragBounds(Rect& out) const
{
    out = Rect{};
    if (!moveActive_) return false;
    if (!moveGroupIds_.empty() && !moveDuplicate_) {
        Rect u{};
        for (int id : moveGroupIds_) {
            const int idx = doc.indexOfId(id);
            if (idx < 0) continue;
            const Rect r = layerRect(doc.layers[(size_t)idx]);
            u = u.empty() ? r : u.unite(r);
        }
        out = u;
        return !out.empty();
    }
    if (moveLayer_ < 0 || moveLayer_ >= (int)doc.layers.size()) return false;
    out = layerRect(doc.layers[(size_t)moveLayer_]);
    return !out.empty();
}

int Editor::layerIndexAt(int x, int y) const
{
    for (int i = (int)doc.layers.size() - 1; i >= 0; --i) {
        const Layer& l = doc.layers[(size_t)i];
        if (!l.visible || l.opacity <= 0.0f) continue;
        if (l.kind == LayerKind::Text) {
            SkRect b = doc.layerDrawBounds(i);
            const Rect box = textBoxRect(l.text);
            if (!box.empty()) {
                SkRect bs = SkRect::MakeLTRB((float)box.x, (float)box.y, (float)box.right(), (float)box.bottom());
                SkRect m;
                l.textXf.mapRect(&m, bs);
                if (b.isEmpty()) b = m;
                else b.join(m);
            }
            if (b.isEmpty()) continue;
            const float pad = (float)kTextBoxHitPad;
            if ((float)x >= b.left() - pad && (float)x <= b.right() + pad && (float)y >= b.top() - pad &&
                (float)y <= b.bottom() + pad)
                return i;
            continue;
        }
        if ((layerPixelAt(l, x, y) >> 24) == 0) continue;
        return i;
    }
    return -1;
}

bool Editor::beginMoveAt(int x, int y, bool keepCurrent)
{
    movePressHit_ = -1;
    if (!sel.active && !keepCurrent) {
        const int hit = layerIndexAt(x, y);
        if (hit < 0) {
            return false;
        }
        if (doc.selectedIndices().size() >= 2 && doc.layerSelected(hit)) {
            doc.activateWithinSelection(hit);
            movePressHit_ = hit;
        } else if (hit != doc.active) {
            doc.active = hit;
            doc.selIds.clear();
        }
    }
    return beginMove();
}

void Editor::collapseMovePressTo(int index)
{
    movePressHit_ = -1;
    if (index < 0 || index >= (int)doc.layers.size()) return;
    setActiveLayer(index);
    movePressHit_ = -1;
}

bool Editor::beginMove()
{
    if (moveActive_) endMove();
    Layer* l = doc.activeLayer();
    if (!l) return false;

    moveTextLayer_ = false;
    moveTextByXf_ = false;
    if (l->kind == LayerKind::Text) {
        moveTextLayer_ = true;
        moveTextPos_ = Vec2f{l->text.x, l->text.y};
        if (!l->textXf.isIdentity()) {
            moveTextByXf_ = true;
            moveTextXf_ = l->textXf;
        }
    }

    moveLayer_ = doc.active;
    moveDx_ = moveDy_ = moveLastDx_ = moveLastDy_ = 0;
    moveActive_ = true;
    moveRecorded_ = false;
    moveDuplicate_ = false;
    moveBaseX_ = l->x;
    moveBaseY_ = l->y;

    moveGroupIds_.clear();
    moveGroupX_.clear();
    moveGroupY_.clear();
    if (doc.selectedIndices().size() >= 2) {
        for (int i : doc.selectedIndices()) {
            moveGroupIds_.push_back(doc.layers[(size_t)i].id);
            moveGroupX_.push_back(doc.layers[(size_t)i].x);
            moveGroupY_.push_back(doc.layers[(size_t)i].y);
        }
    }
    return true;
}

Vec2i Editor::snapMoveDelta(int dx, int dy)
{
    snapGuideX_ = -1.0f;
    snapGuideY_ = -1.0f;
    if (!moveActive_) return Vec2i{dx, dy};

    Rect cur{};
    if (!moveGroupIds_.empty() && !moveDuplicate_) {
        for (int id : moveGroupIds_) {
            const int idx = doc.indexOfId(id);
            if (idx < 0) continue;
            const Rect b = doc.layerVisualBounds(idx);
            cur = cur.empty() ? b : cur.unite(b);
        }
    } else if (moveLayer_ >= 0 && moveLayer_ < (int)doc.layers.size()) {
        cur = doc.layerVisualBounds(moveLayer_);
    }
    if (cur.empty()) return Vec2i{dx, dy};

    const Rect base{cur.x - moveLastDx_, cur.y - moveLastDy_, cur.w, cur.h};
    const Rect moved{base.x + dx, base.y + dy, base.w, base.h};

    std::vector<float> xs, ys;
    xs = {0.0f, (float)doc.width * 0.5f, (float)doc.width};
    ys = {0.0f, (float)doc.height * 0.5f, (float)doc.height};
    for (int i = 0; i < (int)doc.layers.size(); ++i) {
        if (i == moveLayer_) continue;
        if (!moveGroupIds_.empty() && !moveDuplicate_ &&
            std::find(moveGroupIds_.begin(), moveGroupIds_.end(), doc.layers[(size_t)i].id) !=
                moveGroupIds_.end())
            continue;
        const Rect b = doc.layerVisualBounds(i);
        if (b.empty()) continue;
        xs.push_back((float)b.x);
        xs.push_back((float)(b.x + b.w * 0.5f));
        xs.push_back((float)b.right());
        ys.push_back((float)b.y);
        ys.push_back((float)(b.y + b.h * 0.5f));
        ys.push_back((float)b.bottom());
    }

    const float kSnap = 10.0f;
    float bestX = kSnap + 1.0f, bestY = kSnap + 1.0f;
    float edgeX = 0.0f, edgeY = 0.0f;
    const float anchorsX[3] = {(float)moved.x, (float)moved.x + moved.w * 0.5f, (float)moved.right()};
    for (float a : anchorsX)
        for (float t : xs) {
            const float d = std::abs(t - a);
            if (d <= kSnap && d < bestX) {
                bestX = d;
                edgeX = t - a;
                snapGuideX_ = t;
            }
        }
    const float anchorsY[3] = {(float)moved.y, (float)moved.y + moved.h * 0.5f, (float)moved.bottom()};
    for (float a : anchorsY)
        for (float t : ys) {
            const float d = std::abs(t - a);
            if (d <= kSnap && d < bestY) {
                bestY = d;
                edgeY = t - a;
                snapGuideY_ = t;
            }
        }
    const int outX = bestX <= kSnap ? dx + (int)std::lround(edgeX) : dx;
    const int outY = bestY <= kSnap ? dy + (int)std::lround(edgeY) : dy;
    if (bestX > kSnap) snapGuideX_ = -1.0f;
    if (bestY > kSnap) snapGuideY_ = -1.0f;
    return Vec2i{outX, outY};
}

namespace {
Image rotatePixels(const Image& src, int mode)
{
    const int w = src.width(), h = src.height();
    if (src.empty()) return Image();
    const bool swap = (mode == 1 || mode == 2);
    Image dst(swap ? h : w, swap ? w : h);
    const uint32_t* sp = src.pixels();
    uint32_t* dp = dst.pixels();
    const int dw = dst.width();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int dx = x, dy = y;
            switch (mode) {
                case 0: dx = w - 1 - x; dy = h - 1 - y; break;
                case 1: dx = h - 1 - y; dy = x; break;
                case 2: dx = y; dy = w - 1 - x; break;
                case 3: dx = w - 1 - x; break;
                case 4: dy = h - 1 - y; break;
                default: break;
            }
            dp[(size_t)dy * (size_t)dw + (size_t)dx] = sp[(size_t)y * (size_t)w + (size_t)x];
        }
    return dst;
}

SkMatrix rotateMatrix(int mode)
{
    SkMatrix m;
    switch (mode) {
        case 0: m.setAll(-1, 0, 0, 0, -1, 0, 0, 0, 1); break;
        case 1: m.setAll(0, -1, 0, 1, 0, 0, 0, 0, 1); break;
        case 2: m.setAll(0, 1, 0, -1, 0, 0, 0, 0, 1); break;
        case 3: m.setAll(-1, 0, 0, 0, 1, 0, 0, 0, 1); break;
        default: m.setAll(1, 0, 0, 0, -1, 0, 0, 0, 1); break;
    }
    return m;
}

SkMatrix canvasRotate(int mode, int W, int H)
{
    SkMatrix m;
    switch (mode) {
        case 0: m.setAll(-1, 0, (float)W, 0, -1, (float)H, 0, 0, 1); break;
        case 1: m.setAll(0, -1, (float)H, 1, 0, 0, 0, 0, 1); break;
        case 2: m.setAll(0, 1, 0, -1, 0, (float)W, 0, 0, 1); break;
        case 3: m.setAll(-1, 0, (float)W, 0, 1, 0, 0, 0, 1); break;
        default: m.setAll(1, 0, 0, 0, -1, (float)H, 0, 0, 1); break;
    }
    return m;
}

SkMatrix aboutCenter(const SkMatrix& r, float cx, float cy)
{
    SkMatrix pre, post;
    pre.setTranslate(-cx, -cy);
    post.setTranslate(cx, cy);
    const SkMatrix mid = SkMatrix::Concat(r, pre);
    return SkMatrix::Concat(post, mid);
}
}

void Editor::rotateCanvas(int mode)
{
    if (!doc.valid()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    if (moveActive_) endMove();
    const int W = doc.width, H = doc.height;
    const SkMatrix m = canvasRotate(mode, W, H);
    hist.record(doc, sel, "图像旋转");
    for (int i = 0; i < (int)doc.layers.size(); ++i) {
        Layer& l = doc.layers[(size_t)i];
        if (l.kind == LayerKind::Text) {
            l.textXf = SkMatrix::Concat(m, l.textXf);
            continue;
        }
        const int lw = l.img.width(), lh = l.img.height();
        if (lw > 0 && lh > 0) l.img = rotatePixels(l.img, mode);
        const SkPoint q[4] = {{(float)l.x, (float)l.y}, {(float)(l.x + lw), (float)l.y},
                              {(float)l.x, (float)(l.y + lh)}, {(float)(l.x + lw), (float)(l.y + lh)}};
        float x0 = 1e30f, y0 = 1e30f;
        for (const SkPoint& p : q) {
            SkPoint t;
            m.mapPoints(&t, &p, 1);
            x0 = std::min(x0, t.x());
            y0 = std::min(y0, t.y());
        }
        l.x = (int)std::lround(x0);
        l.y = (int)std::lround(y0);
    }
    const bool swap = (mode == 1 || mode == 2);
    doc.resizeCanvas(swap ? H : W, swap ? W : H, 0, 0);
    doc.invalidateTextLayers();
    doc.refreshTextLayers();
    sel.reset(doc.width, doc.height);
    doc.markAllDirty();
    afterEdit("图像旋转");
}

void Editor::rotateLayer(int mode)
{
    if (!doc.valid()) return;
    const int idx = doc.active;
    if (idx < 0 || idx >= (int)doc.layers.size()) return;
    if (ft_.active) cancelFreeTransform();
    if (mw_.active) cancelMeshWarp();
    if (moveActive_) endMove();
    Layer& l = doc.layers[(size_t)idx];
    if (l.kind == LayerKind::Text) {
        const Rect b = doc.layerVisualBounds(idx);
        if (b.empty()) return;
        hist.record(doc, sel, "图层旋转");
        const SkMatrix m = aboutCenter(rotateMatrix(mode), (float)b.x + b.w * 0.5f, (float)b.y + b.h * 0.5f);
        l.textXf = SkMatrix::Concat(m, l.textXf);
        doc.invalidateTextLayers();
        doc.refreshTextLayers();
        doc.markAllDirty();
        afterEdit("图层旋转");
        return;
    }
    if (l.img.empty()) return;
    hist.record(doc, sel, "图层旋转");
    const int cx = l.x + l.img.width() / 2, cy = l.y + l.img.height() / 2;
    l.img = rotatePixels(l.img, mode);
    l.x = cx - l.img.width() / 2;
    l.y = cy - l.img.height() / 2;
    doc.touchStructure();
    doc.markAllDirty();
    afterEdit("图层旋转");
}

void Editor::updateMove(int dx, int dy, bool duplicate)
{
    if (!moveActive_) return;
    if (dx == moveDx_ && dy == moveDy_ && duplicate == moveDuplicate_) return;

    if (!moveRecorded_) {
        hist.record(doc, sel, duplicate ? "复制并移动" : "移动");
        moveRecorded_ = true;
    }

    if (duplicate && !moveDuplicate_) {
        moveDuplicate_ = true;
        doc.duplicateLayer(moveLayer_);
        moveLayer_ = doc.active;
    }

    moveDx_ = dx;
    moveDy_ = dy;

    if (!moveGroupIds_.empty() && !moveDuplicate_) {
        Rect dirty{};
        for (size_t k = 0; k < moveGroupIds_.size(); ++k) {
            const int idx = doc.indexOfId(moveGroupIds_[k]);
            if (idx < 0) continue;
            const Rect oldB = doc.layerVisualBounds(idx);
            Layer& gl = doc.layers[(size_t)idx];
            gl.x = moveGroupX_[k] + dx;
            gl.y = moveGroupY_[k] + dy;
            dirty = dirty.empty() ? oldB.unite(doc.layerVisualBounds(idx))
                                  : dirty.unite(oldB).unite(doc.layerVisualBounds(idx));
        }
        doc.markRecompose(dirty);
    } else {
        Layer& l = doc.layers[(size_t)moveLayer_];
        const Rect oldB = doc.layerVisualBounds(moveLayer_);
        if (moveTextLayer_ && l.kind == LayerKind::Text) {
            if (moveTextByXf_) {
                SkMatrix t;
                t.setTranslate((float)dx, (float)dy);
                l.textXf = SkMatrix::Concat(t, moveTextXf_);
                doc.refreshTextLayer(moveLayer_);
            } else {
                l.text.x = moveTextPos_.x + (float)dx;
                l.text.y = moveTextPos_.y + (float)dy;
                doc.invalidateTextLayers();
                doc.refreshTextLayers();
            }
        } else {
            l.x = moveBaseX_ + dx;
            l.y = moveBaseY_ + dy;
        }
        const Rect newB = doc.layerVisualBounds(moveLayer_);
        doc.markRecompose(oldB.unite(newB));
    }
    afterEdit(duplicate ? "复制并移动" : "移动");
    moveLastDx_ = dx;
    moveLastDy_ = dy;
}

void Editor::endMove()
{
    if (!moveActive_) return;
    snapGuideX_ = -1.0f;
    snapGuideY_ = -1.0f;
    doc.markAllDirty();
    if (moveTextLayer_ && moveRecorded_ && moveLayer_ >= 0 && moveLayer_ < (int)doc.layers.size()) {
        Layer& l = doc.layers[(size_t)moveLayer_];
        if (l.kind == LayerKind::Text) {
            if (moveTextByXf_) {
                SkMatrix t;
                t.setTranslate((float)moveDx_, (float)moveDy_);
                l.textXf = SkMatrix::Concat(t, moveTextXf_);
                doc.refreshTextLayer(moveLayer_);
            } else {
                l.text.x = moveTextPos_.x + (float)moveDx_;
                l.text.y = moveTextPos_.y + (float)moveDy_;
            }
        }
    }
    moveTextLayer_ = false;
    moveTextByXf_ = false;
    moveActive_ = false;
    moveRecorded_ = false;
    moveDuplicate_ = false;
    moveLayer_ = -1;
    moveDx_ = moveDy_ = moveLastDx_ = moveLastDy_ = 0;
}

bool Editor::beginFreeTransform(int layer)
{
    if (layer < 0 || layer >= (int)doc.layers.size()) return false;
    endMove();
    if (ft_.active) commitFreeTransform();

    Layer& l = doc.layers[(size_t)layer];
    const bool isText = l.kind == LayerKind::Text;
    if (l.img.empty()) return false;
    const Rect srcRect = l.img.contentBounds();
    if (srcRect.empty()) return false;
    ft_.layer = layer;
    ft_.base = l.img;
    ft_.srcRect = srcRect;
    ft_.oldX = l.x;
    ft_.oldY = l.y;
    const SkRect gb = doc.layerDrawBounds(layer);
    if (!gb.isEmpty()) {
        ft_.xfRect = gb;
        ft_.srcRect = Rect{(int)std::floor(gb.left()) - ft_.oldX, (int)std::floor(gb.top()) - ft_.oldY,
                           std::max(1, (int)std::ceil(gb.width())), std::max(1, (int)std::ceil(gb.height()))};
    }
    const float fcx = gb.isEmpty() ? (float)ft_.oldX + (float)ft_.srcRect.x + (float)ft_.srcRect.w * 0.5f
                                   : gb.centerX();
    const float fcy = gb.isEmpty() ? (float)ft_.oldY + (float)ft_.srcRect.y + (float)ft_.srcRect.h * 0.5f
                                   : gb.centerY();
    ft_.xf = XForm{fcx, fcy, 1.0f, 1.0f, 0.0f};
    ft_.entry = ft_.xf;
    ft_.dragStart = ft_.xf;
    ft_.isText = isText;
    ft_.baseXf = l.textXf;
    ft_.active = true;
    ft_.moved = false;
    ft_.dragging = false;
    ft_.grab = -1;
    doc.active = layer;
    doc.selIds.clear();
    return true;
}

static double steadyMs()
{
    using namespace std::chrono;
    return duration_cast<duration<double, std::milli>>(steady_clock::now().time_since_epoch()).count();
}

void Editor::transformBeginGrab(int grab, const Vec2f& pt, bool freeCorner)
{
    if (!ft_.active) return;
    ft_.grab = grab;
    ft_.grabPt = pt;
    ft_.dragStart = ft_.xf;
    ft_.dragging = true;
    ft_.lastQuadMs = 0.0;
    ft_.freeCorner = freeCorner && !ft_.isText && grab >= 1 && grab <= 4;
    if (ft_.freeCorner) {
        ft_.freeCornerIdx = grab - 1;
        Vec2f q[4];
        ft_.freeCorner = false;
        transformQuad(q);
        ft_.freeCorner = true;
        for (int i = 0; i < 4; ++i) ft_.freeQuad[i] = q[i];
    }
}

void Editor::transformUpdateGrab(const Vec2f& cur, bool shift)
{
    if (!ft_.active || !ft_.dragging) return;
    if (ft_.freeCorner) {
        ft_.freeQuad[ft_.freeCornerIdx] = cur;
        ft_.moved = true;
        const double nowMs = steadyMs();
        if (ft_.lastQuadMs > 0.0 && nowMs - ft_.lastQuadMs < 12.0) return;
        ft_.lastQuadMs = nowMs;
        transformApply();
        ft_.previewed = true;
        return;
    }
    const XForm& st = ft_.dragStart;
    XForm n = st;
    switch (ft_.grab) {
    case 0:
        n.cx = st.cx + (cur.x - ft_.grabPt.x);
        n.cy = st.cy + (cur.y - ft_.grabPt.y);
        break;
    case 9: {
        n.rot = std::atan2(cur.y - st.cy, cur.x - st.cx) - std::atan2(ft_.grabPt.y - st.cy, ft_.grabPt.x - st.cx) +
                st.rot;
        if (shift) n.rot = std::round(n.rot / (float)(kPi / 12.0)) * (float)(kPi / 12.0);
        break;
    }
    default: {
        const float c = std::cos(-n.rot), s = std::sin(-n.rot);
        const float lx = c * (cur.x - n.cx) - s * (cur.y - n.cy);
        const float ly = s * (cur.x - n.cx) + c * (cur.y - n.cy);
        const float hw = (float)ft_.srcRect.w * 0.5f, hh = (float)ft_.srcRect.h * 0.5f;
        float sx = n.sx, sy = n.sy;
        switch (ft_.grab) {
        case 1: sx = -lx / hw; sy = -ly / hh; break;
        case 2: sx = lx / hw; sy = -ly / hh; break;
        case 3: sx = lx / hw; sy = ly / hh; break;
        case 4: sx = -lx / hw; sy = ly / hh; break;
        case 5: sy = -ly / hh; break;
        case 6: sx = lx / hw; break;
        case 7: sy = ly / hh; break;
        case 8: sx = -lx / hw; break;
        }
        if (!shift && ft_.grab >= 1 && ft_.grab <= 4) {
            const float uni = std::max(std::abs(sx), std::abs(sy));
            sx = uni;
            sy = uni;
        }
        n.sx = std::clamp(sx, 0.05f, 100.0f);
        n.sy = std::clamp(sy, 0.05f, 100.0f);
        break;
    }
    }

    ft_.xf = n;
    if (std::abs(ft_.xf.sx - ft_.entry.sx) > 1e-4f || std::abs(ft_.xf.sy - ft_.entry.sy) > 1e-4f ||
        std::abs(ft_.xf.rot - ft_.entry.rot) > 1e-4f || std::abs(ft_.xf.cx - ft_.entry.cx) > 1e-3f ||
        std::abs(ft_.xf.cy - ft_.entry.cy) > 1e-3f)
        ft_.moved = true;
    transformApply();
    if (ft_.dragging) ft_.previewed = true;
}

void Editor::transformNudge(float dx, float dy)
{
    if (!ft_.active) return;
    ft_.xf.cx += dx;
    ft_.xf.cy += dy;
    if (std::abs(dx) > 0 || std::abs(dy) > 0) ft_.moved = true;
    transformApply();
}

void Editor::transformEndGrab()
{
    if (!ft_.active || !ft_.dragging) return;
    ft_.dragging = false;
    if (ft_.moved || ft_.previewed) transformApply();
    ft_.previewed = false;
}

void Editor::transformApply()
{
    if (!ft_.active || ft_.layer < 0) return;
    const Rect oldB = doc.layerVisualBounds(ft_.layer);
    if (ft_.isText) {
        Layer& lt = doc.layers[(size_t)ft_.layer];
        lt.textXf = textXfForCurrent();
        doc.refreshTextLayer(ft_.layer);
        const Rect newBT = doc.layerVisualBounds(ft_.layer);
        doc.markDirty(oldB.unite(newBT));
        return;
    }
    if (ft_.freeCorner) {
        int ox = 0, oy = 0;
        Image img = quadRasterize(ft_.base, ft_.srcRect, ft_.oldX, ft_.oldY, ft_.freeQuad, ox, oy, ft_.dragging);
        Layer& l0 = doc.layers[(size_t)ft_.layer];
        l0.img = std::move(img);
        l0.x = ox;
        l0.y = oy;
        doc.markDirty(oldB.unite(doc.layerVisualBounds(ft_.layer)));
        return;
    }
    int ox = 0, oy = 0;
    if (std::abs(ft_.xf.sx - 1.0f) < 1e-4f && std::abs(ft_.xf.sy - 1.0f) < 1e-4f && ft_.xf.rot == 0.0f) {
        Layer& l = doc.layers[(size_t)ft_.layer];
        l.img = ft_.base;
        l.x = ft_.oldX + (int)std::lround(ft_.xf.cx - ft_.entry.cx);
        l.y = ft_.oldY + (int)std::lround(ft_.xf.cy - ft_.entry.cy);
        doc.markDirty(oldB.unite(doc.layerVisualBounds(ft_.layer)));
        return;
    }
    Image img = xformRasterize(ft_.base, ft_.srcRect, ft_.xf, ft_.oldX, ft_.oldY, ox, oy, ft_.dragging);
    Layer& l = doc.layers[(size_t)ft_.layer];
    l.img = std::move(img);
    l.x = ox;
    l.y = oy;
    const Rect newB = doc.layerVisualBounds(ft_.layer);
    doc.markDirty(oldB.unite(newB));
}

SkMatrix Editor::textXfForCurrent() const
{
    SkMatrix delta = xformMatrixAbout(ft_.entry.cx, ft_.entry.cy, ft_.xf);
    SkMatrix res;
    res.setConcat(delta, ft_.baseXf);
    return res;
}

void Editor::setMeshDensity(int n)
{
    meshDensity = n < 2 ? 2 : (n > 16 ? 16 : n);
    if (mw_.active) {
        const int layer = mw_.layer;
        cancelMeshWarp();
        beginMeshWarp(layer);
    }
}

void Editor::buildMeshGrid()
{
    const int n = mw_.n < 2 ? 2 : (mw_.n > 16 ? 16 : mw_.n);
    mw_.n = n;
    mw_.pts.assign((size_t)(n + 1) * (size_t)(n + 1), Vec2f{});
    const float x0 = (float)(mw_.oldX + mw_.srcRect.x);
    const float y0 = (float)(mw_.oldY + mw_.srcRect.y);
    const float dx = (float)mw_.srcRect.w / (float)n;
    const float dy = (float)mw_.srcRect.h / (float)n;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            mw_.pts[(size_t)j * (size_t)(n + 1) + (size_t)i] = Vec2f{x0 + dx * (float)i, y0 + dy * (float)j};
    mw_.basePts = mw_.pts;
}

int Editor::nearestMeshVertex(const Vec2f& pt) const
{
    if (!mw_.active || mw_.pts.empty()) return -1;
    int best = -1;
    float bestD = 0.0f;
    for (int i = 0; i < (int)mw_.pts.size(); ++i) {
        const float d = (mw_.pts[(size_t)i].x - pt.x) * (mw_.pts[(size_t)i].x - pt.x) +
                        (mw_.pts[(size_t)i].y - pt.y) * (mw_.pts[(size_t)i].y - pt.y);
        if (best < 0 || d < bestD) {
            best = i;
            bestD = d;
        }
    }
    return best;
}

void Editor::rebuildMeshPts()
{
    if (!mw_.active || mw_.basePts.empty()) return;
    mw_.pts = mw_.basePts;
    for (const MeshPin& pin : mw_.pins) {
        if (pin.vertex < 0 || pin.vertex >= (int)mw_.pts.size()) continue;
        const float dx = pin.cur.x - pin.origin.x;
        const float dy = pin.cur.y - pin.origin.y;
        if (dx == 0.0f && dy == 0.0f) continue;
        mw_.pts[(size_t)pin.vertex].x += dx;
        mw_.pts[(size_t)pin.vertex].y += dy;
    }
}

bool Editor::beginMeshWarp(int layer)
{
    if (ft_.active) commitFreeTransform();
    if (mw_.active) commitMeshWarp();
    if (layer < 0 || layer >= (int)doc.layers.size()) return false;
    Layer& l = doc.layers[(size_t)layer];
    if (l.kind == LayerKind::Text || l.img.empty()) return false;
    const Rect cb = l.img.contentBounds();
    if (cb.empty()) return false;
    mw_ = MeshWarpState{};
    mw_.layer = layer;
    mw_.base = l.img;
    mw_.srcRect = cb;
    mw_.oldX = l.x;
    mw_.oldY = l.y;
    mw_.n = meshDensity;
    buildMeshGrid();
    mw_.pins.clear();
    mw_.pins.reserve(mw_.basePts.size());
    for (size_t i = 0; i < mw_.basePts.size(); ++i) {
        MeshPin pin;
        pin.origin = mw_.basePts[i];
        pin.cur = mw_.basePts[i];
        pin.vertex = (int)i;
        mw_.pins.push_back(pin);
    }
    mw_.active = true;
    return true;
}

bool Editor::meshHasPinAt(const Vec2f& pt, float tol) const
{
    if (!mw_.active) return false;
    for (const MeshPin& pin : mw_.pins)
        if (std::hypot(pin.cur.x - pt.x, pin.cur.y - pt.y) <= tol) return true;
    return false;
}

bool Editor::meshDeletePinAt(const Vec2f& pt, float tol)
{
    if (!mw_.active) return false;
    int hitPin = -1;
    int hitExtra = -1;
    float best = tol;
    for (int i = 0; i < (int)mw_.pins.size(); ++i) {
        const float d = std::hypot(mw_.pins[(size_t)i].cur.x - pt.x, mw_.pins[(size_t)i].cur.y - pt.y);
        if (d <= best) {
            best = d;
            hitPin = i;
            hitExtra = -1;
        }
    }
    for (int i = 0; i < (int)mw_.extras.size(); ++i) {
        const float d = std::hypot(mw_.extras[(size_t)i].pos.x - pt.x, mw_.extras[(size_t)i].pos.y - pt.y);
        if (d <= best) {
            best = d;
            hitExtra = i;
            hitPin = -1;
        }
    }
    if (hitPin < 0 && hitExtra < 0) return false;
    if (hitExtra >= 0) {
        mw_.extras.erase(mw_.extras.begin() + hitExtra);
        for (size_t i = mw_.steps.size(); i > 0; --i) {
            MeshStep& st = mw_.steps[i - 1];
            if (st.kind == 1 && st.idx == hitExtra) mw_.steps.erase(mw_.steps.begin() + (long)(i - 1));
            else if (st.kind == 1 && st.idx > hitExtra) st.idx -= 1;
        }
        for (size_t i = mw_.redoSteps.size(); i > 0; --i) {
            MeshStep& st = mw_.redoSteps[i - 1];
            if (st.kind == 1 && st.idx == hitExtra) mw_.redoSteps.erase(mw_.redoSteps.begin() + (long)(i - 1));
            else if (st.kind == 1 && st.idx > hitExtra) st.idx -= 1;
        }
    } else {
        mw_.pins.erase(mw_.pins.begin() + hitPin);
        for (size_t i = mw_.steps.size(); i > 0; --i) {
            MeshStep& st = mw_.steps[i - 1];
            if (st.kind == 0 && st.idx == hitPin) mw_.steps.erase(mw_.steps.begin() + (long)(i - 1));
            else if (st.kind == 0 && st.idx > hitPin) st.idx -= 1;
        }
        for (size_t i = mw_.redoSteps.size(); i > 0; --i) {
            MeshStep& st = mw_.redoSteps[i - 1];
            if (st.kind == 0 && st.idx == hitPin) mw_.redoSteps.erase(mw_.redoSteps.begin() + (long)(i - 1));
            else if (st.kind == 0 && st.idx > hitPin) st.idx -= 1;
        }
    }
    mw_.dragPin = -1;
    mw_.dragExtra = -1;
    rebuildMeshPts();
    bool any = false;
    for (const MeshPin& pin : mw_.pins)
        if (pin.cur.x != pin.origin.x || pin.cur.y != pin.origin.y) {
            any = true;
            break;
        }
    for (const MeshExtra& e : mw_.extras)
        if (e.pos.x != e.start.x || e.pos.y != e.start.y) {
            any = true;
            break;
        }
    mw_.moved = any;
    meshApply(false);
    return true;
}

void Editor::meshSurfaceData(std::vector<Vec2f>& ep, std::vector<Vec2f>& ed) const
{
    ep.clear();
    ed.clear();
    if (!mw_.active) return;
    for (const MeshExtra& e : mw_.extras) {
        ep.push_back(e.param);
        const Vec2f rest = meshC1Surface(mw_.pts, mw_.n, {}, {}, e.param.x, e.param.y);
        ed.push_back(Vec2f{e.pos.x - rest.x, e.pos.y - rest.y});
    }
    const size_t cnt = mw_.extras.size();
    for (int it = 0; it < 8; ++it) {
        for (size_t k = 0; k < cnt; ++k) {
            Vec2f acc = meshC1Surface(mw_.pts, mw_.n, {}, {}, ep[k].x, ep[k].y);
            for (size_t j = 0; j < cnt; ++j) {
                if (j == k) continue;
                const float bw = meshC1BumpWeight(ep[j], ep[k].x, ep[k].y);
                if (bw == 0.0f) continue;
                acc.x += ed[j].x * bw;
                acc.y += ed[j].y * bw;
            }
            ed[k] = Vec2f{mw_.extras[k].pos.x - acc.x, mw_.extras[k].pos.y - acc.y};
        }
    }
}

void Editor::meshApply(bool highQuality)
{
    if (!mw_.active) return;
    rebuildMeshPts();
    bool any = false;
    for (const MeshPin& pin : mw_.pins)
        if (pin.cur.x != pin.origin.x || pin.cur.y != pin.origin.y) {
            any = true;
            break;
        }
    for (const MeshExtra& e : mw_.extras)
        if (e.pos.x != e.start.x || e.pos.y != e.start.y) {
            any = true;
            break;
        }
    if (!any) {
        Layer& lb = doc.layers[(size_t)mw_.layer];
        const Rect oldR = doc.layerVisualBounds(mw_.layer);
        lb.img = mw_.base;
        lb.x = mw_.oldX;
        lb.y = mw_.oldY;
        doc.markDirty(oldR.unite(doc.layerVisualBounds(mw_.layer)));
        return;
    }
    std::vector<Vec2f> ep, ed;
    meshSurfaceData(ep, ed);
    const Rect oldB = doc.layerVisualBounds(mw_.layer);
    int ox = 0, oy = 0;
    Image img = meshC1Rasterize(mw_.base, mw_.srcRect, mw_.oldX, mw_.oldY, mw_.pts, mw_.n, ep, ed, ox, oy,
                                highQuality, 3);
    if (img.empty()) return;
    Layer& l = doc.layers[(size_t)mw_.layer];
    l.img = std::move(img);
    l.x = ox;
    l.y = oy;
    doc.markDirty(oldB.unite(doc.layerVisualBounds(mw_.layer)));
}

void Editor::meshPress(const Vec2f& pt, float tol)
{
    if (!mw_.active) return;
    rebuildMeshPts();
    mw_.pressPt = pt;
    mw_.dragActive = false;
    mw_.dragIdx = -1;
    mw_.dragPin = -1;
    mw_.dragExtra = -1;
    float best = tol;
    for (int i = 0; i < (int)mw_.pins.size(); ++i) {
        const float d = std::hypot(mw_.pins[(size_t)i].cur.x - pt.x, mw_.pins[(size_t)i].cur.y - pt.y);
        if (d <= best) {
            best = d;
            mw_.dragPin = i;
            mw_.dragExtra = -1;
        }
    }
    for (int i = 0; i < (int)mw_.extras.size(); ++i) {
        const float d = std::hypot(mw_.extras[(size_t)i].pos.x - pt.x, mw_.extras[(size_t)i].pos.y - pt.y);
        if (d <= best) {
            best = d;
            mw_.dragExtra = i;
            mw_.dragPin = -1;
        }
    }
    mw_.dragIdx = nearestMeshVertex(pt);
    if (mw_.dragPin >= 0) {
        mw_.dragFrom = mw_.pins[(size_t)mw_.dragPin].cur;
        return;
    }
    if (mw_.dragExtra >= 0) {
        mw_.dragFrom = mw_.extras[(size_t)mw_.dragExtra].pos;
        return;
    }
    float gi = 0.0f, gj = 0.0f;
    if (!meshC1Inverse(mw_.pts, mw_.n, pt, gi, gj)) return;
    MeshExtra e;
    e.param = Vec2f{gi, gj};
    e.src = Vec2f{(float)mw_.srcRect.x + (float)mw_.srcRect.w * gi / (float)mw_.n,
                  (float)mw_.srcRect.y + (float)mw_.srcRect.h * gj / (float)mw_.n};
    e.start = pt;
    e.pos = pt;
    mw_.extras.push_back(e);
    mw_.dragExtra = (int)mw_.extras.size() - 1;
    mw_.dragFrom = pt;
}

void Editor::meshDrag(const Vec2f& pt)
{
    if (!mw_.active) return;
    if (mw_.dragPin < 0 && mw_.dragExtra < 0) return;
    if (!mw_.dragActive) {
        if (std::hypot(pt.x - mw_.pressPt.x, pt.y - mw_.pressPt.y) < 3.0f) return;
        mw_.dragActive = true;
    }
    const Vec2f np{mw_.dragFrom.x + (pt.x - mw_.pressPt.x), mw_.dragFrom.y + (pt.y - mw_.pressPt.y)};
    if (mw_.dragPin >= 0) {
        mw_.pins[(size_t)mw_.dragPin].cur = np;
    } else {
        mw_.extras[(size_t)mw_.dragExtra].pos = np;
    }
    rebuildMeshPts();
    mw_.moved = false;
    for (const MeshPin& pin : mw_.pins)
        if (pin.cur.x != pin.origin.x || pin.cur.y != pin.origin.y) {
            mw_.moved = true;
            break;
        }
    for (const MeshExtra& e : mw_.extras)
        if (e.pos.x != e.start.x || e.pos.y != e.start.y) {
            mw_.moved = true;
            break;
        }
    const double nowMs = steadyMs();
    if (mw_.lastMs > 0.0 && nowMs - mw_.lastMs < 12.0) return;
    mw_.lastMs = nowMs;
    meshApply(false);
}

void Editor::meshRelease()
{
    if (!mw_.active) return;
    Vec2f now{};
    bool moved = false;
    if (!mw_.dragActive) {
        mw_.dragIdx = -1;
        mw_.dragPin = -1;
        mw_.dragExtra = -1;
        return;
    }
    if (mw_.dragPin >= 0) {
        now = mw_.pins[(size_t)mw_.dragPin].cur;
        moved = now.x != mw_.dragFrom.x || now.y != mw_.dragFrom.y;
        if (moved) {
            MeshStep st;
            st.idx = mw_.dragPin;
            st.kind = 0;
            st.from = mw_.dragFrom;
            st.to = now;
            mw_.steps.push_back(st);
            mw_.redoSteps.clear();
        }
    } else if (mw_.dragExtra >= 0) {
        now = mw_.extras[(size_t)mw_.dragExtra].pos;
        moved = now.x != mw_.dragFrom.x || now.y != mw_.dragFrom.y;
        if (moved) {
            MeshStep st;
            st.idx = mw_.dragExtra;
            st.kind = 1;
            st.from = mw_.dragFrom;
            st.to = now;
            mw_.steps.push_back(st);
            mw_.redoSteps.clear();
        }
    }
    if (moved) meshApply(false);
    mw_.dragIdx = -1;
    mw_.dragPin = -1;
    mw_.dragExtra = -1;
}

void Editor::commitMeshWarp()
{
    if (!mw_.active) return;
    if (!mw_.moved) {
        Layer& l0 = doc.layers[(size_t)mw_.layer];
        const Rect oldB = doc.layerVisualBounds(mw_.layer);
        l0.img = mw_.base;
        l0.x = mw_.oldX;
        l0.y = mw_.oldY;
        doc.markDirty(oldB.unite(doc.layerVisualBounds(mw_.layer)));
        mw_ = MeshWarpState{};
        return;
    }
    meshApply(true);
    Layer& l = doc.layers[(size_t)mw_.layer];
    Image result = l.img;
    const int rx = l.x, ry = l.y;
    const Rect oldB = doc.layerVisualBounds(mw_.layer);
    l.img = mw_.base;
    l.x = mw_.oldX;
    l.y = mw_.oldY;
    hist.record(doc, sel, "网络变形");
    l.img = std::move(result);
    l.x = rx;
    l.y = ry;
    doc.markDirty(oldB.unite(doc.layerVisualBounds(mw_.layer)));
    afterEdit("网络变形完成");
    mw_ = MeshWarpState{};
}

bool Editor::undoMeshStep()
{
    if (!mw_.active || mw_.steps.empty()) return false;
    const MeshStep st = mw_.steps.back();
    mw_.steps.pop_back();
    if (st.kind == 0) {
        if (st.idx >= 0 && st.idx < (int)mw_.pins.size()) mw_.pins[(size_t)st.idx].cur = st.from;
    } else if (st.idx >= 0 && st.idx < (int)mw_.extras.size()) {
        mw_.extras[(size_t)st.idx].pos = st.from;
    }
    mw_.redoSteps.push_back(st);
    mw_.moved = !mw_.steps.empty();
    meshApply(false);
    return true;
}

bool Editor::redoMeshStep()
{
    if (!mw_.active || mw_.redoSteps.empty()) return false;
    const MeshStep st = mw_.redoSteps.back();
    mw_.redoSteps.pop_back();
    if (st.kind == 0) {
        if (st.idx >= 0 && st.idx < (int)mw_.pins.size()) mw_.pins[(size_t)st.idx].cur = st.to;
    } else if (st.idx >= 0 && st.idx < (int)mw_.extras.size()) {
        mw_.extras[(size_t)st.idx].pos = st.to;
    }
    mw_.steps.push_back(st);
    mw_.moved = true;
    meshApply(false);
    return true;
}

void Editor::cancelMeshWarp()
{
    if (!mw_.active) return;
    Layer& l = doc.layers[(size_t)mw_.layer];
    const Rect oldB = doc.layerVisualBounds(mw_.layer);
    l.img = mw_.base;
    l.x = mw_.oldX;
    l.y = mw_.oldY;
    doc.markDirty(oldB.unite(doc.layerVisualBounds(mw_.layer)));
    mw_ = MeshWarpState{};
}

void Editor::commitFreeTransform()
{
    if (!ft_.active) return;
    if (ft_.dragging) {
        ft_.dragging = false;
        if (ft_.moved || ft_.previewed) transformApply();
    }
    ft_.previewed = false;
    if (ft_.moved) {
        Layer& l = doc.layers[(size_t)ft_.layer];
        if (l.kind == LayerKind::Text) {
            const SkMatrix target = textXfForCurrent();
            l.textXf = ft_.baseXf;
            doc.refreshTextLayer(ft_.layer);
            const Rect oldB = doc.layerVisualBounds(ft_.layer);
            hist.record(doc, sel, "文字变形");
            l.textXf = target;
            doc.refreshTextLayer(ft_.layer);
            const Rect newB = doc.layerVisualBounds(ft_.layer);
            doc.markDirty(oldB.unite(newB));
            afterEdit("文字变形完成（仍是文字层）");
        } else {
            Image result = l.img;
            const int rx = l.x, ry = l.y;
            l.img = ft_.base;
            l.x = ft_.oldX;
            l.y = ft_.oldY;
            const Rect oldB = doc.layerVisualBounds(ft_.layer);
            hist.record(doc, sel, "自由变换");
            l.img = std::move(result);
            l.x = rx;
            l.y = ry;
            const Rect newB = doc.layerVisualBounds(ft_.layer);
            doc.markDirty(oldB.unite(newB));
            afterEdit("自由变换完成");
        }
    } else {
    }
    ft_ = FreeTransformState{};
}

void Editor::cancelFreeTransform()
{
    if (!ft_.active) return;
    if (ft_.layer >= 0 && ft_.layer < (int)doc.layers.size()) {
        Layer& l = doc.layers[(size_t)ft_.layer];
        const Rect curB = doc.layerVisualBounds(ft_.layer);
        l.img = ft_.base;
        l.x = ft_.oldX;
        l.y = ft_.oldY;
        if (ft_.isText) {
            l.textXf = ft_.baseXf;
            doc.refreshTextLayer(ft_.layer);
        }
        const Rect oldB = doc.layerVisualBounds(ft_.layer);
        doc.markDirty(curB.unite(oldB));
    }
    ft_ = FreeTransformState{};
}
void Editor::transformQuad(Vec2f out[4]) const
{
    if (!ft_.active || ft_.srcRect.empty()) {
        for (int i = 0; i < 4; ++i) out[i] = Vec2f{};
        return;
    }
    if (ft_.freeCorner) {
        for (int i = 0; i < 4; ++i) out[i] = ft_.freeQuad[i];
        return;
    }
    SkRect src;
    if (!ft_.xfRect.isEmpty()) {
        src = ft_.xfRect;
    } else {
        src = SkRect::MakeXYWH((float)(ft_.oldX + ft_.srcRect.x), (float)(ft_.oldY + ft_.srcRect.y),
                               (float)ft_.srcRect.w, (float)ft_.srcRect.h);
    }
    const SkMatrix m = xformMatrix(src, ft_.xf);
    SkPoint pts[4] = {SkPoint::Make(src.left(), src.top()), SkPoint::Make(src.right(), src.top()),
                      SkPoint::Make(src.right(), src.bottom()), SkPoint::Make(src.left(), src.bottom())};
    m.mapPoints(pts, 4);
    for (int i = 0; i < 4; ++i) out[i] = Vec2f{pts[i].x(), pts[i].y()};
}

void Editor::zoomIn()
{
    view.setZoomAt(view.zoom * 1.25f, view.centerX(), view.centerY(), doc.width, doc.height);
}

void Editor::zoomOut()
{
    view.setZoomAt(view.zoom / 1.25f, view.centerX(), view.centerY(), doc.width, doc.height);
}

void Editor::fitView()
{
    view.fit(doc.width, doc.height);
}

void Editor::actualPixels()
{
    view.setZoomAt(1.0f, view.centerX(), view.centerY(), doc.width, doc.height);
}

}