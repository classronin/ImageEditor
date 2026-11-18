#include "nodes/CanvasNode.h"
#include "include/core/SkBitmap.h"

#include "app/Editor.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPath.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkSamplingOptions.h"
#include "nodes/Style.h"
#include "core/Blend.h"
#include "core/LayerStyle.h"
#include "nodes/Guides.h"
#include "tools/Tool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
}

namespace ie {

CanvasNode::CanvasNode() = default;
bool CanvasNode::wantsAnimation() const
{
    return panning_ || (tool_ && tool_->busy());
}

bool CanvasNode::onWheel(const WheelEvent& e)
{
    if (!ed_ || !frame_.contains((int)e.pos.x, (int)e.pos.y)) return false;
    if (tool_ && inputSrc_ && tool_->onWheel(*ed_, *inputSrc_)) {
        markDirty(frame_);
        return true;
    }
    const float factor = std::pow(1.2f, e.dy > 0 ? 1.0f : -1.0f);
    ed_->view.setZoomAt(ed_->view.zoom * factor, e.pos.x, e.pos.y, ed_->doc.width, ed_->doc.height);
    markDirty(frame_);
    return true;
}

bool CanvasNode::onKey(const KeyEvent& e)
{
    if (!ed_ || !tool_ || !inputSrc_) return false;
    if (!tool_->canvasTextActive()) return false;

    if (!e.pressed) {
        if (!e.repeat) return true;
    }
    if (!tool_->canvasTextKey(*ed_, e)) return true;
    tool_->update(*ed_, *inputSrc_);
    return true;
}

bool CanvasNode::cursorAt(float x, float y, CursorShape& out)
{
    if (!tool_) return false;
    const bool inside = frame_.contains((int)x, (int)y);
    if (!inside && !tool_->busy() && !tool_->canvasTextActive()) return false;
    if (inputSrc_ && inputSrc_->space) {
        out = CursorShape::Hand;
        return true;
    }
    if (pickMode_) {
        out = CursorShape::Cross;
        return true;
    }
    static const InputFrame kEmpty{};
    out = tool_->cursor(inputSrc_ ? *inputSrc_ : kEmpty);
    return true;
}

bool CanvasNode::onPointer(const PointerEvent& e)
{
    const bool inside = frame_.contains((int)e.pos.x, (int)e.pos.y);

    const bool spaceHeld = inputSrc_ && inputSrc_->space;
    int panBtn = e.button;
    if (panBySpace_ || (spaceHeld && e.button == 0 && !panning_)) panBtn = 2;
    const CanvasViewState::PanStep st =
        CanvasViewState::panStep(panning_, e.pressed, e.released, panBtn, inside);
    if (st.begin) {
        panLast_ = e.pos;
        panBySpace_ = spaceHeld && e.button == 0;
    }
    if (st.move && ed_) {
        ed_->view.panBy(e.pos.x - panLast_.x, e.pos.y - panLast_.y);
        panLast_ = e.pos;
        markDirty(frame_);
    }
    panning_ = st.panning;
    if (st.end) panBySpace_ = false;
    if (st.begin || st.move || st.end || (st.panning && (e.button == 2 || panBySpace_))) return true;
    if (panning_) return true;

    if (!inside && dragBtn_ < 0) { panning_ = false; return false; }

    if (!ed_ || !tool_ || !inputSrc_) return false;
    const InputFrame& in = *inputSrc_;

    if (e.pressed) {
        dragBtn_ = -1;
        if (tool_->acceptsButton(e.button)) {
            dragBtn_ = e.button;
            tool_->onPress(*ed_, in, e.button);
            return true;
        }
        if (e.button == 1) {
            if (ed_->meshWarpActive()) {
                const Vec2f ip = ed_->canvasImage(e.pos.x, e.pos.y);
                const float z = ed_->view.zoom > 0.05f ? ed_->view.zoom : 0.05f;
                float tol = 10.0f / z;
                if (tol < 2.0f) tol = 2.0f;
                if (ed_->meshDeletePinAt(ip, tol)) return true;
            }
            ed_->requestCanvasMenu(e.pos.x, e.pos.y);
            return true;
        }
        return false;
    }
    if (e.released) {
        const bool was = dragBtn_ == e.button;
        if (was) dragBtn_ = -1;
        if (was && tool_->acceptsButton(e.button)) tool_->onRelease(*ed_, in, e.button);
        return true;
    }
    if (dragBtn_ >= 0 && in.down[dragBtn_]) {
        if (tool_->acceptsButton(dragBtn_)) tool_->onDrag(*ed_, in, dragBtn_);
        return true;
    }
    if (!inside) { panning_ = false; return false; }
    if (in.down[e.button]) {
        if (tool_->acceptsButton(e.button)) tool_->onDrag(*ed_, in, e.button);
        return true;
    }

    if (!in.down[0]) tool_->update(*ed_, in);
    return false;
}

void CanvasNode::syncViewport()
{
    if (!ed_ || frame_.empty()) return;
    ed_->view.vpX = (float)frame_.x;
    ed_->view.vpY = (float)frame_.y;
    ed_->view.vpW = (float)frame_.w;
    ed_->view.vpH = (float)frame_.h;
}

void CanvasNode::onPlaced(const Rect& oldFrame, const Rect& oldClip)
{
    if (oldFrame != frame_) markDirty(frame_);
    syncViewport();
}

void CanvasNode::syncDocument()
{
    if (!ed_) return;
    syncViewport();
    const int rev = ed_->doc.revision();
    const int sr = ed_->doc.structureRevision();
    const int dw = ed_->doc.width, dh = ed_->doc.height;
    if (rev != lastDocRev_ || sr != lastDocStructRev_ || dw != lastDocW_ || dh != lastDocH_) {
        const bool geometry = sr != lastDocStructRev_ || dw != lastDocW_ || dh != lastDocH_;
        lastDocRev_ = rev;
        lastDocStructRev_ = sr;
        lastDocW_ = dw;
        lastDocH_ = dh;
        const Rect d = ed_->doc.pendingDirty();
        ed_->doc.clearDirty();
        if (geometry || d.empty() || dw <= 0 || dh <= 0) {
            markDirty(frame_);
        } else {
            const Rect v = ed_->view.rectToScreen(d, dw, dh).intersect(frame_);
            if (v.empty())
                markDirty(frame_);
            else
                markDirty(v);
        }
    }
    if (lastZoom_ != ed_->view.zoom || lastPanX_ != ed_->view.panX || lastPanY_ != ed_->view.panY ||
        lastRot_ != ed_->view.rotationDeg || lastScaleY_ != ed_->view.scaleY ||
        lastSkewX_ != ed_->view.skewX || lastViewport_ != frame_) {
        lastZoom_ = ed_->view.zoom;
        lastPanX_ = ed_->view.panX;
        lastPanY_ = ed_->view.panY;
        lastRot_ = ed_->view.rotationDeg;
        lastScaleY_ = ed_->view.scaleY;
        lastSkewX_ = ed_->view.skewX;
        lastViewport_ = frame_;
        markDirty(frame_);
    }
}

namespace {

sk_sp<SkImage> layerSkImage(const Image& im)
{
    if (im.empty()) return nullptr;
    SkImageInfo info = SkImageInfo::Make(im.width(), im.height(), kRGBA_8888_SkColorType,
                                         kUnpremul_SkAlphaType);
    sk_sp<SkData> data = SkData::MakeWithoutCopy(im.data(), (size_t)im.width() * (size_t)im.height() * 4);
    if (!data) return nullptr;
    return SkImages::RasterFromData(info, data, (size_t)im.width() * 4);
}

}

void CanvasNode::toNative(uint32_t* px, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        const uint32_t v = px[i];
        const uint32_t a = v >> 24;
        if (a == 255u) {
            px[i] = ((v & 255u) << 16) | (v & 0x0000FF00u) | ((v >> 16) & 255u) | (a << 24);
            continue;
        }
        if (a == 0u) {
            px[i] = 0u;
            continue;
        }
        const uint32_t r = v & 255u, g = (v >> 8) & 255u, b = (v >> 16) & 255u;
        const uint32_t pr = (r * a + 127u) / 255u, pg = (g * a + 127u) / 255u, pb = (b * a + 127u) / 255u;
        px[i] = pb | (pg << 8) | (pr << 16) | (a << 24);
    }
}

void CanvasNode::paint(SkCanvas& canvas)
{
    const ui::Theme& t = ui::theme();
    syncViewport();
    Rect r = frame_.intersect(clipRect());
    if (r.empty()) return;

    ui::fill(canvas, r, t.canvas);

    SkRect cb = canvas.getLocalClipBounds();
    const int x0 = std::max(r.x, (int)std::floor(cb.left()));
    const int y0 = std::max(r.y, (int)std::floor(cb.top()));
    const int x1 = std::min(r.right(), (int)std::ceil(cb.right()));
    const int y1 = std::min(r.bottom(), (int)std::ceil(cb.bottom()));
    const int cell = 16;
    float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
    const bool hasDoc = ed_ && ed_->doc.valid();
    if (hasDoc) {
        const int iw = ed_->doc.width, ih = ed_->doc.height;
        const float xs[4] = {0.0f, (float)iw, (float)iw, 0.0f};
        const float ys[4] = {0.0f, 0.0f, (float)ih, (float)ih};
        for (int i = 0; i < 4; ++i) {
            const Vec2f q = ed_->view.toScreen(xs[i], ys[i], iw, ih);
            bx0 = std::min(bx0, q.x);
            by0 = std::min(by0, q.y);
            bx1 = std::max(bx1, q.x);
            by1 = std::max(by1, q.y);
        }
    }
    const int px0 = hasDoc ? std::max(x0, (int)std::floor(bx0)) : x1;
    const int py0 = hasDoc ? std::max(y0, (int)std::floor(by0)) : y1;
    const int px1 = hasDoc ? std::min(x1, (int)std::ceil(bx1)) : x0;
    const int py1 = hasDoc ? std::min(y1, (int)std::ceil(by1)) : y0;
    for (int gy = (py0 / cell) * cell; gy < py1; gy += cell) {
        const int cy0 = std::max(gy, py0);
        const int cy1 = std::min(gy + cell, py1);
        if (cy0 >= cy1) continue;
        for (int gx = (px0 / cell) * cell; gx < px1; gx += cell) {
            const int cx0 = std::max(gx, px0);
            const int cx1 = std::min(gx + cell, px1);
            if (cx0 >= cx1) continue;
            const bool odd = (((gx / cell) + (gy / cell)) & 1) != 0;
            ui::fill(canvas, Rect{cx0, cy0, cx1 - cx0, cy1 - cy0}, odd ? t.checkA : t.checkB);
        }
    }

    if (hasDoc && !ed_->doc.layers.empty()) {
        const int iw = ed_->doc.width, ih = ed_->doc.height;
        const SkMatrix ctm = ed_->view.matrix(iw, ih);
        if (!pictureValid()) rebuildPicture();
        if (pic_) {
            canvas.save();
            canvas.concat(ctm);
            canvas.drawPicture(pic_.get());
            canvas.restore();
        }
    }

    replayOverlay(canvas, ovShown_);

    if (hasDoc) {
        const int iw = ed_->doc.width, ih = ed_->doc.height;
        const float gx = ed_->snapGuideX();
        const float gy = ed_->snapGuideY();
        if (gx >= 0.0f || gy >= 0.0f) {
            canvas.save();
            canvas.concat(ed_->view.matrix(iw, ih));
            SkPaint line;
            line.setAntiAlias(false);
            line.setStyle(SkPaint::kStroke_Style);
            line.setStrokeWidth(0.0f);
            line.setColor(IE_COL32(178, 71, 255, 255));
            if (gx >= 0.0f) canvas.drawLine(gx, 0.0f, gx, (float)ih, line);
            if (gy >= 0.0f) canvas.drawLine(0.0f, gy, (float)iw, gy, line);
            canvas.restore();
        }
    }
}

bool CanvasNode::pictureValid() const
{
    if (!ed_ || !pic_) return false;
    return picRev_ == ed_->doc.revision() && picStructRev_ == ed_->doc.structureRevision() &&
           picW_ == ed_->doc.width && picH_ == ed_->doc.height && picZoom_ == ed_->view.zoom &&
           picPanX_ == ed_->view.panX && picPanY_ == ed_->view.panY && picRot_ == ed_->view.rotationDeg &&
           picScaleY_ == ed_->view.scaleY && picSkewX_ == ed_->view.skewX;
}

void CanvasNode::rebuildPicture()
{
    pic_.reset();
    if (!ed_ || !ed_->doc.valid()) return;
    SkPictureRecorder rec;
    SkCanvas* c = rec.beginRecording(SkRect::MakeWH((float)ed_->doc.width, (float)ed_->doc.height));
    if (!c) return;
    drawDocumentLayers(*c);
    pic_ = rec.finishRecordingAsPicture();
    picRev_ = ed_->doc.revision();
    picStructRev_ = ed_->doc.structureRevision();
    picW_ = ed_->doc.width;
    picH_ = ed_->doc.height;
    picZoom_ = ed_->view.zoom;
    picPanX_ = ed_->view.panX;
    picPanY_ = ed_->view.panY;
    picRot_ = ed_->view.rotationDeg;
    picScaleY_ = ed_->view.scaleY;
    picSkewX_ = ed_->view.skewX;
}

void CanvasNode::drawDocumentLayers(SkCanvas& canvas)
{
    if (!ed_ || !ed_->doc.valid()) return;
    const int iw = ed_->doc.width, ih = ed_->doc.height;
    const SkMatrix ctm = ed_->view.matrix(iw, ih);
    const bool minify = ed_->view.zoom < 1.0f;
    const SkSamplingOptions samp = minify
                                       ? SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear)
                                       : SkSamplingOptions(SkFilterMode::kNearest);
    for (int i = 0; i < (int)ed_->doc.layers.size(); ++i) {
        const Layer& l = ed_->doc.layers[(size_t)i];
        if (!l.visible || l.opacity <= 0.0f) continue;
        if (ed_->doc.textLayerHasOutline(i)) {
            ed_->doc.drawTextLayers(canvas, ctm, iw, ih, i);
            continue;
        }
        int sox = 0, soy = 0;
        const Image& pix = styledPixels(l, sox, soy);
        if (pix.empty()) continue;
        if (texDoc_ != (const void*)ed_) {
            texDoc_ = (const void*)ed_;
            tex_.clear();
        }
        TexCacheEntry* e = nullptr;
        for (auto& t : tex_)
            if (t.id == l.id) { e = &t; break; }
        if (!e) {
            TexCacheEntry ne;
            ne.id = l.id;
            tex_.push_back(ne);
            e = &tex_.back();
        }
        const void* pdata = (const void*)pix.data();
        if (e->data != pdata || e->w != pix.width() || e->h != pix.height() || e->rev != l.pixRev) {
            e->data = pdata;
            e->w = pix.width();
            e->h = pix.height();
            e->rev = l.pixRev;
            e->img = layerSkImage(pix);
            e->minified = nullptr;
        }
        if (!e->img) continue;
        const SkImage* tex = e->img.get();
        if (minify) {
            if (!e->minified) e->minified = e->img->withDefaultMipmaps();
            if (e->minified) tex = e->minified.get();
        }
        SkPaint paint;
        paint.setAlphaf(std::clamp(l.opacity, 0.0f, 1.0f));
        paint.setBlendMode(blend::skBlend(l.blend));
        const SkRect src = SkRect::MakeWH((float)pix.width(), (float)pix.height());
        const SkRect dst = SkRect::MakeXYWH((float)(l.x + sox), (float)(l.y + soy),
                                            (float)pix.width(), (float)pix.height());
        if (ed_->localWarpActive() && ed_->localWarpLayerIndex() == i) {
            int px = 0, py = 0;
            const Image& ov = ed_->localWarpPreviewImage(px, py);
            sk_sp<SkImage> oimg = ov.empty() ? nullptr : layerSkImage(ov);
            if (oimg) {
                const SkRect osrc = SkRect::MakeWH((float)ov.width(), (float)ov.height());
                const SkRect odst = SkRect::MakeXYWH((float)px, (float)py, (float)ov.width(),
                                                     (float)ov.height());
                SkRect group = dst;
                group.join(odst);
                canvas.saveLayer(&group, &paint);
                SkPaint basePaint;
                basePaint.setBlendMode(SkBlendMode::kSrcOver);
                canvas.drawImageRect(tex, src, dst, samp, &basePaint,
                                     SkCanvas::kFast_SrcRectConstraint);
                SkPaint opaint;
                opaint.setBlendMode(SkBlendMode::kSrc);
                canvas.drawImageRect(oimg.get(), osrc, odst, samp, &opaint,
                                     SkCanvas::kFast_SrcRectConstraint);
                canvas.restore();
                continue;
            }
        }
        canvas.drawImageRect(tex, src, dst, samp, &paint, SkCanvas::kFast_SrcRectConstraint);

        if (!l.uMask.empty() && l.uMaskW == l.img.width() && l.uMaskH == l.img.height()) {
            sk_sp<SkImage> mk = maskOverlayImage(l, i);
            if (mk) {
                SkPaint mp;
                mp.setBlendMode(SkBlendMode::kSrcOver);
                const SkRect msrc = SkRect::MakeWH((float)l.uMaskW, (float)l.uMaskH);
                const SkRect mdst = SkRect::MakeXYWH((float)l.x, (float)l.y, (float)l.uMaskW,
                                                     (float)l.uMaskH);
                canvas.drawImageRect(mk.get(), msrc, mdst, samp, &mp, SkCanvas::kFast_SrcRectConstraint);
            }
        }
    }
}

sk_sp<SkImage> CanvasNode::maskOverlayImage(const Layer& l, int index)
{
    MaskTexEntry* e = nullptr;
    for (auto& t : mtex_)
        if (t.id == l.id) { e = &t; break; }
    if (!e) {
        MaskTexEntry ne;
        ne.id = l.id;
        mtex_.push_back(ne);
        e = &mtex_.back();
    }
    if (e->img && e->rev == l.uMaskRev && e->w == l.uMaskW && e->h == l.uMaskH) return e->img;

    const int w = l.uMaskW, h = l.uMaskH;
    if (w <= 0 || h <= 0 || l.uMask.size() != (size_t)w * (size_t)h) return nullptr;
    SkImageInfo info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    sk_sp<SkData> data = SkData::MakeUninitialized((size_t)w * (size_t)h * 4);
    if (!data) return nullptr;
    uint8_t* px = reinterpret_cast<uint8_t*>(data->writable_data());
        for (int y = 0; y < h; ++y) {
        const uint8_t* m = l.uMask.data() + (size_t)y * (size_t)w;
        uint8_t* row = px + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; ++x) {
            const int keep = (int)m[x];
            row[x * 4 + 0] = 0;
            row[x * 4 + 1] = 230;
            row[x * 4 + 2] = 255;
            row[x * 4 + 3] = (uint8_t)((255 - keep) * 102 / 255);
        }
    }
    e->rev = l.uMaskRev;
    e->w = w;
    e->h = h;
    e->img = SkImages::RasterFromData(info, data, (size_t)w * 4);
    return e->img;
}

void CanvasNode::recordOverlay(Overlay* ov) const
{
    recordCanvasBorder(ov);
    if (ed_ && ed_->composeLines) guides::drawCompose(ov, ed_->view, ed_->doc.width, ed_->doc.height);
    if (!ed_ || !tool_ || !inputSrc_) return;
    tool_->overlay(ov, *ed_, ed_->view, *inputSrc_);
    drawAnts(ov);
}

void CanvasNode::recordCanvasBorder(Overlay* ov) const
{
    if (!ed_ || !ed_->doc.valid()) return;
    const int iw = ed_->doc.width, ih = ed_->doc.height;
    const float xs[4] = {0.0f, (float)iw, (float)iw, 0.0f};
    const float ys[4] = {0.0f, 0.0f, (float)ih, (float)ih};
    Vec2f q[5];
    for (int i = 0; i < 4; ++i) q[i] = ed_->view.toScreen(xs[i], ys[i], iw, ih);
    q[4] = q[0];
    ov->polyline(q, 5, IE_COL32(255, 255, 255, 220), 1.0f, false);
    ov->polyline(q, 5, IE_COL32(0, 0, 0, 210), 1.0f, false);
}

void CanvasNode::autoScrollDrag(const InputFrame& in)
{
    if (dragBtn_ < 0 || !in.down[dragBtn_] || !ed_ || frame_.empty()) return;
    const float x0 = (float)frame_.x, y0 = (float)frame_.y;
    const float x1 = (float)frame_.right(), y1 = (float)frame_.bottom();
    float ox = 0.0f, oy = 0.0f;
    if (in.mouse.x >= x1) ox = in.mouse.x - x1 + 1.0f;
    else if (in.mouse.x < x0) ox = in.mouse.x - x0 - 1.0f;
    if (in.mouse.y >= y1) oy = in.mouse.y - y1 + 1.0f;
    else if (in.mouse.y < y0) oy = in.mouse.y - y0 - 1.0f;
    if (ox == 0.0f && oy == 0.0f) return;

    const float kCap = 24.0f;
    const float dx = -std::max(-kCap, std::min(kCap, ox * 0.8f));
    const float dy = -std::max(-kCap, std::min(kCap, oy * 0.8f));
    ed_->view.panBy(dx, dy);
    markDirty(frame_);
    if (tool_ && tool_->acceptsButton(dragBtn_)) tool_->onDrag(*ed_, in, dragBtn_);
}

void CanvasNode::syncOverlay()
{
    ovNext_.reset();
    {
        OverlayRecorder rec(&ovNext_);
        recordOverlay(&rec);
    }

    const Rect area = frame_.intersect(clipRect());
    const size_t shown = ovShown_.ops.size();
    const size_t next = ovNext_.ops.size();
    for (size_t i = 0; i < std::max(shown, next); ++i) {
        if (i < shown && i < next && overlayOpSame(ovShown_, i, ovNext_, i)) continue;
        {
            if (i < shown) markDirty(ovShown_.ops[i].bounds.intersect(area));
            if (i < next) markDirty(ovNext_.ops[i].bounds.intersect(area));
        }
    }

    ovShown_.ops.swap(ovNext_.ops);
    ovShown_.pts.swap(ovNext_.pts);
    ovShown_.text.swap(ovNext_.text);
}

void CanvasNode::drawAnts(Overlay* ov) const
{
    if (!ed_ || !ed_->sel.active) return;
    const float dash = 4.0f, gap = 4.0f;
    const int iw = ed_->doc.width, ih = ed_->doc.height;
    const float phase = 0.0f;
    const Color32 white = IE_COL32(255, 255, 255, 255);

    for (const auto& s : ed_->sel.segments()) {
        const Vec2f a = ed_->view.toScreen(s.x0, s.y0, iw, ih);
        const Vec2f b = ed_->view.toScreen(s.x1, s.y1, iw, ih);
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.3f) continue;
        const float ux = dx / len, uy = dy / len;
        float t = -phase;
        bool on = true;
        while (t < len) {
            const float t0 = std::max(0.0f, t);
            const float t1 = std::min(len, t + (on ? dash : gap));
            if (t1 > t0 && on)
                ov->line(a.x + ux * t0, a.y + uy * t0, a.x + ux * t1, a.y + uy * t1, white, 1.0f, false);
            t += (on ? dash : gap);
            on = !on;
        }
    }
}

}
