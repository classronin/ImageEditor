#pragma once

#include "app/Editor.h"
#include "core/LayerStyle.h"
#include "core/ShadowLift.h"
#include "host/Host.h"
#include "nodes/Panels.h"
#include "nodes/Style.h"
#include "nodes/Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ie {
namespace ui {

enum class Page { Tri, Stroke, Shadow, Overlay, Lift };
enum class RowKind { Num, Toggle, Combo, Button, Text, Color };

struct RowDef {
    RowKind kind;
    const char* label;
    int key;
};

struct PageDef {
    const char* title;
    Page page;
    std::vector<RowDef> rows;
};

class ImagePanelNode : public Node {
public:
    explicit ImagePanelNode(Host& h) : host_(h)
    {
        for (int i = 0; i < kNumPool; ++i) {
            pool_[i].owner = this;
            bind(pool_[i]);
        }
    }

    Size requestSize() const override
    {
        const int w = std::clamp(host_.settings.triW, kMinW, kMaxW);
        const int minH = contentH();
        const int h = host_.settings.triH > 0 ? std::clamp(host_.settings.triH, minH, kMaxH) : minH;
        return Size{w, h};
    }
    int contentH() const { return kHeadH + kTabH + 6 + (int)rows().size() * kRowH + 8; }

    static const std::vector<PageDef>& pages()
    {
        static const std::vector<PageDef> p = {
            {"三色", Page::Tri,
             {{RowKind::Num, "红 (%)", 0},
              {RowKind::Num, "绿 (%)", 1},
              {RowKind::Num, "蓝 (%)", 2},
              {RowKind::Toggle, "保持亮度", 3},
              {RowKind::Button, "重置", 4},
              {RowKind::Text, "", 5}}},
            {"衡影", Page::Lift,
             {{RowKind::Num, "强度 (%)", 40},
              {RowKind::Num, "半径 (px)", 41},
              {RowKind::Num, "灵敏度 (%)", 43},
              {RowKind::Num, "柔化 (px)", 44},
              {RowKind::Num, "上限 (倍)", 45},
              {RowKind::Button, "重置", 42}}},
            {"描边", Page::Stroke,
             {{RowKind::Toggle, "启用", 10},
              {RowKind::Num, "宽度 (像素)", 11},
              {RowKind::Combo, "位置", 12},
              {RowKind::Num, "不透明度 (%)", 13},
              {RowKind::Color, "颜色", 14}}},
            {"投影", Page::Shadow,
             {{RowKind::Toggle, "启用", 20},
              {RowKind::Num, "距离 (像素)", 21},
              {RowKind::Num, "模糊 (像素)", 22},
              {RowKind::Num, "角度 (度)", 23},
              {RowKind::Num, "不透明度 (%)", 24},
              {RowKind::Color, "颜色", 25}}},
            {"颜叠", Page::Overlay,
             {{RowKind::Toggle, "启用", 30},
              {RowKind::Num, "不透明度 (%)", 31},
              {RowKind::Color, "颜色", 32}}},
        };
        return p;
    }
    const PageDef& page() const { return pages()[(size_t)page_]; }
    const std::vector<RowDef>& rows() const { return page().rows; }

    void paint(SkCanvas& c) override
    {
        const Theme& t = theme();
        const Rect p = frame_;
        for (int i = 5; i >= 1; --i) {
            const Rect sp{p.x - i, p.y - i + 3, p.w + 2 * i, p.h + 2 * i};
            fill(c, sp, rgba(0, 0, 0, 6 * i), 4.0f + (float)i);
        }
        fill(c, p, t.panel, (float)t.radius);
        stroke(c, p, t.border, 1.0f, (float)t.radius);
        drawText(c, (float)(p.x + 10), (float)(p.y + (kHeadH - (int)lineHeight()) / 2), t.text, "图像");
        if (const LayerChild* ch = tri()) {
            char idbuf[24];
            std::snprintf(idbuf, sizeof(idbuf), "#%d", ch->id);
            const float x0 = (float)(p.x + 10) + ui::textWidth("图像");
            drawText(c, x0 + 8.0f, (float)(p.y + (kHeadH - (int)lineHeight()) / 2), t.dim, idbuf);
        }
        paintTabs(c);
        fill(c, Rect{p.x, p.y + kTabH, p.w, 1}, t.border);
        paintRows(c);
    }

    bool onPointer(const PointerEvent& e) override
    {
        if (!frame_.contains((int)e.pos.x, (int)e.pos.y) && hoverBtn_ >= 0 && !sess_.busy()) {
            hoverBtn_ = -1;
            markDirty(frame_);
        }
        if (!e.pressed && !e.released && !e.down) {
            const int hit = rowAt(e.pos.y);
            int hb = -1;
            if (hit >= 0 && rows()[(size_t)hit].kind == RowKind::Button &&
                buttonRect(hit).contains((int)e.pos.x, (int)e.pos.y))
                hb = hit;
            if (hb != hoverBtn_) {
                hoverBtn_ = hb;
                markDirty(frame_);
            }
        }
        if (sess_.busy()) {
            const bool used = sess_.route(e, numBox(sess_.index));
            if (e.released) {
                host_.releasePointer(this);
                if (!sess_.editing()) {
                    endFragment();
                }
            }
            markDirty(frame_);
            if (used) return true;
        }

        if (e.pressed && tabRect().contains((int)e.pos.x, (int)e.pos.y)) {
            const int i = tabAt(e.pos.x);
            if (i >= 0 && i != (int)page_) switchPage(i);
            return true;
        }

        if (e.pressed) {
            const int hit = rowAt(e.pos.y);
            if (hit >= 0) {
                const RowDef& rd = rows()[(size_t)hit];
                switch (rd.kind) {
                    case RowKind::Num: {
                        NumRow* nr = numAt(hit);
                        if (!nr) break;
                        if (!ctrlBox(hit).contains((int)e.pos.x, (int)e.pos.y)) break;
                        sess_.take(hit, &nr->nf);
                        const bool used = nr->nf.pointer(e, ctrlBox(hit));
                        host_.capturePointer(this);
                        markDirty(frame_);
                        return used;
                    }
                    case RowKind::Toggle:
                        if (toggleBox(hit).contains((int)e.pos.x, (int)e.pos.y)) {
                            toggleKey(rd.key);
                            return true;
                        }
                        break;
                    case RowKind::Combo:
                        if (ctrlBox(hit).contains((int)e.pos.x, (int)e.pos.y)) {
                            openCombo(rd.key);
                            return true;
                        }
                        break;
                    case RowKind::Color:
                        if (colorSwatch(hit).contains((int)e.pos.x, (int)e.pos.y)) {
                            openColor(rd.key);
                            return true;
                        }
                        break;
                    case RowKind::Button:
                        if (buttonRect(hit).contains((int)e.pos.x, (int)e.pos.y)) {
                            pressKey(rd.key);
                            return true;
                        }
                        break;
                    case RowKind::Text:
                        break;
                }
                return true;
            }
            return true;
        }
        return true;
    }

    bool onKey(const KeyEvent& e) override
    {
        if (!sess_.editing()) return false;
        NumRow* nr = numAt(sess_.index);
        if (!nr) return false;
        NumField& f = nr->nf;
        f.key(e);
        markDirty(frame_);
        if (!f.editing) {
            sess_.clear();
            endFragment();
        }
        return true;
    }

    void cancelTransientEdit(float px, float py) override
    {
        if (sess_.index >= 0 && sess_.cancelUnless(px, py, numBox(sess_.index))) markDirty(frame_);
        if (frame_.contains((int)px, (int)py)) return;
        endFragment();
    }

    bool cursorAt(float x, float y, CursorShape& out) override
    {
        if (tabRect().contains((int)x, (int)y)) {
            out = CursorShape::Hand;
            return true;
        }
        const int hit = rowAt(y);
        if (hit >= 0) {
            const RowDef& rd = rows()[(size_t)hit];
            if (rd.kind == RowKind::Num) {
                NumRow* nr = numAt(hit);
                if (nr && sess_.index == hit && nr->nf.editing) {
                    out = CursorShape::IBeam;
                    return true;
                }
                return false;
            }
            if (rd.kind == RowKind::Toggle) {
                if (toggleBox(hit).contains((int)x, (int)y)) {
                    out = CursorShape::Hand;
                    return true;
                }
                return false;
            }
            if (rd.kind == RowKind::Combo) {
                if (ctrlBox(hit).contains((int)x, (int)y)) {
                    out = CursorShape::Hand;
                    return true;
                }
                return false;
            }
            if (rd.kind == RowKind::Color) {
                if (colorSwatch(hit).contains((int)x, (int)y)) {
                    out = CursorShape::Hand;
                    return true;
                }
                return false;
            }
            if (rd.kind == RowKind::Button) {
                if (buttonRect(hit).contains((int)x, (int)y)) {
                    out = CursorShape::Hand;
                    return true;
                }
                return false;
            }
        }
        return false;
    }

    bool focusable() const override { return true; }

    void takeDirty(std::vector<Rect>& out) override
    {
        Node::takeDirty(out);
        const std::string now = fingerprint();
        if (now != seen_) {
            seen_ = now;
            out.push_back(frame_);
        }
    }

    double valueAt(int i) const
    {
        const auto& rs = rows();
        if (i < 0 || i >= (int)rs.size()) return 0.0;
        return readKey(rs[(size_t)i].key);
    }

    bool preferredPos(int& x, int& y) const override
    {
        if (host_.settings.triX < 0 || host_.settings.triY < 0) return false;
        x = host_.settings.triX;
        y = host_.settings.triY;
        return true;
    }

    Rect probeCtrl(int row) const { return ctrlBox(row); }
    Rect probeToggle(int row) const { return toggleBox(row); }
    Rect probeRow(int row) const { return rowRect(row); }
    int probeLabelW() const { return labelW(); }

private:
    static constexpr int kMinW = 220;
    static constexpr int kMaxW = 460;
    static constexpr int kMaxH = 720;
    static constexpr int kHeadH = 30;
    static constexpr int kTabH = 28;
    static constexpr int kRowH = 30;
    static constexpr int kPadX = 12;
    static constexpr int kLabelW = 64;
    static constexpr int kNumPool = 6;

    int labelW() const
    {
        int w = 0;
        for (const RowDef& rd : rows())
            if (rd.kind != RowKind::Button && rd.label && *rd.label)
                w = std::max(w, (int)textWidth(rd.label));
        const int cap = std::max(kLabelW, frame_.w - 2 * kPadX - 88);
        return std::clamp(w + 10, kLabelW, cap);
    }

    static constexpr const char* const kPosItems[3] = {"外部", "中部", "内部"};

    struct NumRow {
        NumField nf;
        ImagePanelNode* owner = nullptr;
        int key = 0;
    };

    Rect headerRect() const { return Rect{frame_.x, frame_.y, frame_.w, kHeadH}; }
    Rect tabRect() const { return Rect{frame_.x, frame_.y + kHeadH, frame_.w, kTabH}; }
    Rect rowRect(int i) const
    {
        return Rect{frame_.x + kPadX, frame_.y + kHeadH + kTabH + 6 + i * kRowH,
                    std::max(1, frame_.w - 2 * kPadX), kRowH - 6};
    }
    Rect ctrlBox(int i) const
    {
        const Rect rr = rowRect(i);
        const int x = rr.x + labelW();
        return Rect{x, rr.y, std::max(1, rr.right() - x), rr.h};
    }
    Rect numBox(int i) const { return ctrlBox(i); }
    int hitMargin() const override { return 6; }

    int zoneAt(float x, float y) const
    {
        int z = 0;
        if (x <= (float)(frame_.x + 8)) z |= 1;
        else if (x >= (float)(frame_.right() - 8)) z |= 2;
        if (y <= (float)(frame_.y + 8)) z |= 8;
        if (y >= (float)(frame_.bottom() - 8)) z |= 4;
        return z;
    }
    Rect buttonRect(int i) const
    {
        const Rect rr = rowRect(i);
        return Rect{rr.right() - 72, rr.y, 72, rr.h};
    }
    Rect toggleBox(int i) const { return ui::checkBoxIn(ctrlBox(i)); }
    Rect colorSwatch(int i) const
    {
        const Rect cb = ctrlBox(i);
        return Rect{cb.x, cb.y + 3, 40, cb.h - 6};
    }
    int rowAt(float y) const
    {
        const int top = frame_.y + kHeadH + kTabH + 6;
        if (y < (float)top) return -1;
        const int i = ((int)y - top) / kRowH;
        if (i < 0 || i >= (int)rows().size()) return -1;
        return i;
    }
    int tabAt(float x) const
    {
        const int n = (int)pages().size();
        if (n <= 0) return -1;
        const int tw = frame_.w / n;
        const int i = ((int)x - frame_.x) / std::max(1, tw);
        return (i >= 0 && i < n) ? i : -1;
    }
    Rect tabCell(int i) const
    {
        const int n = (int)pages().size();
        const int tw = frame_.w / n;
        return Rect{frame_.x + i * tw, frame_.y + kHeadH, tw, kTabH};
    }

    NumRow* numAt(int rowIndex)
    {
        int n = 0;
        const auto& rs = rows();
        for (int i = 0; i < (int)rs.size(); ++i) {
            if (rs[(size_t)i].kind != RowKind::Num) continue;
            if (i == rowIndex) {
                NumRow& r = pool_[n];
                r.key = rs[(size_t)i].key;
                applyRange(r.nf, r.key);
                return &r;
            }
            ++n;
        }
        return nullptr;
    }
    NumRow* numAt(int rowIndex) const
    {
        return const_cast<ImagePanelNode*>(this)->numAt(rowIndex);
    }

    static double numRead(const void* ud)
    {
        const NumRow* r = (const NumRow*)ud;
        return r->owner->readKey(r->key);
    }
    static void numWrite(void* ud, double v)
    {
        NumRow* r = (NumRow*)ud;
        r->owner->writeKey(r->key, v);
    }
    void bind(NumRow& row)
    {
        NumField& f = row.nf;
        f.get = &numRead;
        f.set = &numWrite;
        f.readUd = &row;
        f.writeUd = &row;
        f.lo = 0.0;
        f.hi = 100.0;
        f.step = 1.0;
        f.fmt = "%.0f";
    }

    static void applyRange(NumField& f, int key)
    {
        f.lo = 0.0;
        f.hi = 100.0;
        f.step = 1.0;
        switch (key) {
            case 0: case 1: case 2: f.lo = 0; f.hi = 200; break;
            case 11: f.lo = 0; f.hi = kStyleStrokeWidthMax; break;
            case 21: f.lo = -kStyleShadowDistMax; f.hi = kStyleShadowDistMax; break;
            case 22: f.lo = 0; f.hi = kStyleShadowBlurMax; break;
            case 45: f.lo = 2.0; f.hi = 4.0; f.step = 0.05; break;
            case 23: f.lo = 0; f.hi = 360; break;
            default: f.lo = 0; f.hi = 100; break;
        }
    }

    int activeIndex() const { return host_.ed().doc.active; }
    const Layer* active() const { return host_.ed().doc.activeLayer(); }
    const LayerStyle* style() const
    {
        const Layer* l = active();
        return l ? &l->style : nullptr;
    }
    const LayerChild* tri() const { return host_.ed().triChild(activeIndex()); }
    const LayerChild* lift() const { return host_.ed().liftChild(activeIndex()); }

    double readKey(int key) const
    {
        const LayerChild* c = tri();
        const LayerChild* l = lift();
        const LayerStyle* s = style();
        switch (key) {
            case 0: return c ? (double)c->r * 100.0 : 100.0;
            case 1: return c ? (double)c->g * 100.0 : 100.0;
            case 2: return c ? (double)c->b * 100.0 : 100.0;
            case 11: return s ? (double)s->strokeWidth : 3.0;
            case 13: return s ? (double)s->strokeOpacity : 100.0;
            case 21: return s ? (double)s->shadowDist : 6.0;
            case 22: return s ? (double)s->shadowSize : 8.0;
            case 23: return s ? (double)s->shadowAngle : 135.0;
            case 24: return s ? (double)s->shadowOpacity : 100.0;
            case 31: return s ? (double)s->overlayOpacity : 100.0;
            case 40: return l ? (double)l->liftStrength * 100.0 : 0.0;
            case 41: return l ? (double)l->liftRadius : 0.0;
            case 43: return l ? (double)((l->liftSensitivity - 0.25f) / 0.5f) * 100.0 : 50.0;
            case 44: return l ? (double)l->liftSoftness : 0.0;
            case 45: return l ? (double)l->liftMaxGain : 3.0;
            default: return 0.0;
        }
    }

    bool boolKey(int key) const
    {
        const LayerStyle* s = style();
        const LayerChild* c = tri();
        switch (key) {
            case 3: return c && c->keepLuma;
            case 10: return s && s->strokeOn;
            case 20: return s && s->shadowOn;
            case 30: return s && s->overlayOn;
            default: return false;
        }
    }

    int comboKey(int key) const
    {
        const LayerStyle* s = style();
        if (key == 12) return s ? (int)s->strokePos : 0;
        return 0;
    }

    const float* colorKey(int key) const
    {
        const LayerStyle* s = style();
        if (!s) return nullptr;
        switch (key) {
            case 14: return s->strokeColor;
            case 25: return s->shadowColor;
            case 32: return s->overlayColor;
            default: return nullptr;
        }
    }

    void writeTri(int ch, double percent)
    {
        const int idx = activeIndex();
        if (idx < 0) return;
        float r = 1.0f, g = 1.0f, b = 1.0f;
        bool kl = false;
        if (const LayerChild* c = tri()) {
            r = c->r; g = c->g; b = c->b; kl = c->keepLuma;
        }
        const float nv = (float)(percent / 100.0);
        float* dst = ch == 0 ? &r : (ch == 1 ? &g : &b);
        if (*dst == nv) return;
        *dst = nv;
        beginFragment();
        host_.ed().applyLayerTri(idx, r, g, b, kl);
        host_.refreshCanvas();
        markDirty(frame_);
    }

    void writeTriLuma(bool on)
    {
        const int idx = activeIndex();
        if (idx < 0) return;
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (const LayerChild* c = tri()) { r = c->r; g = c->g; b = c->b; }
        beginFragment();
        host_.ed().applyLayerTri(idx, r, g, b, on);
        host_.refreshCanvas();
        markDirty(frame_);
    }

    void writeStyle(const std::function<void(LayerStyle&)>& mutate)
    {
        const int idx = activeIndex();
        const Layer* l = active();
        if (idx < 0 || !l) return;
        LayerStyle st = l->style;
        mutate(st);
        beginFragment();
        host_.ed().applyLayerSettings(idx, l->blend, l->opacity, l->lockAlpha, st);
        host_.refreshCanvas();
        markDirty(frame_);
    }

    void writeKey(int key, double v)
    {
        if (key == 0 || key == 1 || key == 2) {
            writeTri(key, v);
            return;
        }
        switch (key) {
            case 11: writeStyle([&](LayerStyle& s) { s.strokeWidth = (float)v; }); break;
            case 13: writeStyle([&](LayerStyle& s) { s.strokeOpacity = (float)v; }); break;
            case 21: writeStyle([&](LayerStyle& s) { s.shadowDist = (float)v; }); break;
            case 22: writeStyle([&](LayerStyle& s) { s.shadowSize = (float)v; }); break;
            case 23: writeStyle([&](LayerStyle& s) { s.shadowAngle = (float)v; }); break;
            case 24: writeStyle([&](LayerStyle& s) { s.shadowOpacity = (float)v; }); break;
            case 31: writeStyle([&](LayerStyle& s) { s.overlayOpacity = (float)v; }); break;
            case 40: writeLift(0, (float)(v / 100.0)); break;
            case 41: writeLift(1, (float)v); break;
            case 43: writeLift(2, (float)(0.25 + v / 100.0 * 0.5)); break;
            case 44: writeLift(3, (float)v); break;
            case 45: writeLift(4, (float)v); break;
            default: break;
        }
    }

    void writeLift(int slot, float v)
    {
        const int idx = activeIndex();
        if (idx < 0) return;
        float p[5] = {0.0f, 0.0f, 0.5f, 0.0f, 3.0f};
        if (const LayerChild* c = lift()) {
            p[0] = c->liftStrength;
            p[1] = c->liftRadius;
            p[2] = c->liftSensitivity;
            p[3] = c->liftSoftness;
            p[4] = c->liftMaxGain;
        }
        p[slot] = v;
        beginFragment();
        host_.ed().applyLayerLift(idx, p[0], p[1], p[2], p[3], p[4]);
        host_.refreshCanvas();
        markDirty(frame_);
    }

    void toggleKey(int key)
    {
        switch (key) {
            case 3: writeTriLuma(!boolKey(3)); break;
            case 10: {
                const bool nv = !boolKey(10);
                writeStyle([&](LayerStyle& s) {
                    s.strokeOn = nv;
                    if (nv) copyPaletteFg(s.strokeColor);
                });
                break;
            }
            case 20: {
                const bool nv = !boolKey(20);
                writeStyle([&](LayerStyle& s) {
                    s.shadowOn = nv;
                    if (nv) copyPaletteFg(s.shadowColor);
                });
                break;
            }
            case 30: {
                const bool nv = !boolKey(30);
                writeStyle([&](LayerStyle& s) {
                    s.overlayOn = nv;
                    if (nv) copyPaletteFg(s.overlayColor);
                });
                break;
            }
            default: break;
        }
    }

    void copyPaletteFg(float* dst) const
    {
        const float* fg = host_.ed().palette.fg;
        for (int i = 0; i < 4; ++i) dst[i] = fg[i];
    }

    void pressKey(int key)
    {
        if (key == 4) {
            const int idx = activeIndex();
            if (idx < 0) return;
            beginFragment();
            host_.ed().applyLayerTri(idx, 1.0f, 1.0f, 1.0f, false);
            markDirty(frame_);
            return;
        }
        if (key == 42) {
            const int idx = activeIndex();
            if (idx < 0) return;
            beginFragment();
            host_.ed().applyLayerLift(idx, 0.0f, 0.0f, 0.5f, 0.0f, 3.0f);
            host_.refreshCanvas();
            markDirty(frame_);
            return;
        }
    }

    void openCombo(int key)
    {
        if (key != 12) return;
        const int cur = comboKey(12);
        std::vector<std::string> items;
        for (int i = 0; i < 3; ++i) items.push_back(kPosItems[i]);
        host_.openListPopup(items, cur, frame_.x + 40, frame_.y + 40, [this](int pick) {
            writeStyle([&](LayerStyle& s) { s.strokePos = (StrokePos)std::clamp(pick, 0, 2); });
        });
    }

    void writeColor(int key, const float rgba[4])
    {
        switch (key) {
            case 14: writeStyle([&](LayerStyle& s) { for (int i = 0; i < 4; ++i) s.strokeColor[i] = rgba[i]; }); break;
            case 25: writeStyle([&](LayerStyle& s) { for (int i = 0; i < 4; ++i) s.shadowColor[i] = rgba[i]; }); break;
            case 32: writeStyle([&](LayerStyle& s) { for (int i = 0; i < 4; ++i) s.overlayColor[i] = rgba[i]; }); break;
            default: break;
        }
    }

    void openColor(int key)
    {
        const float* cur = colorKey(key);
        if (!cur) return;
        endFragment();
        float cpy[4] = {cur[0], cur[1], cur[2], cur[3]};
        Host* hp = &host_;
        hp->openDialogNode(makeColorPopup(*hp, cpy, [this, key](const float rgba[4]) {
            writeColor(key, rgba);
        }));
    }

    void beginFragment()
    {
        if (fragOpen_) return;
        fragOpen_ = true;
        if (page_ == Page::Tri || page_ == Page::Lift)
            host_.ed().beginChildEdit(page_ == Page::Lift ? "衡影" : "三色");
        else
            host_.ed().beginLayerStyleEdit();
    }
    void endFragment()
    {
        if (!fragOpen_) return;
        fragOpen_ = false;
        host_.ed().endEditStep();
    }

    void switchPage(int i)
    {
        if (i == (int)page_) return;
        endFragment();
        page_ = (Page)i;
        sess_.clear();
        for (int k = 0; k < kNumPool; ++k) {
            pool_[k].nf.editing = false;
            pool_[k].nf.drag = false;
            pool_[k].nf.dragged = false;
        }
        markDirty(frame_);
        requestLayout();
    }


    void paintTabs(SkCanvas& c)
    {
        const Theme& t = theme();
        const Rect bar = tabRect();
        fill(c, bar, t.surface);
        for (int i = 0; i < (int)pages().size(); ++i) {
            const Rect cell = tabCell(i);
            const bool on = i == (int)page_;
            if (on) fill(c, cell, t.panelHi);
            const std::string& s = pages()[(size_t)i].title;
            drawText(c, (float)(cell.x + (cell.w - (int)textWidth(s)) / 2),
                     (float)(cell.y + (cell.h - (int)lineHeight()) / 2), on ? t.text : t.dim, s);
        }
        fill(c, Rect{bar.x, bar.bottom() - 1, bar.w, 1}, t.border);
    }

    void paintRows(SkCanvas& c)
    {
        const Theme& t = theme();
        const auto& rs = rows();
        const bool live = active() != nullptr;
        for (int i = 0; i < (int)rs.size(); ++i) {
            const RowDef& rd = rs[(size_t)i];
            const Rect rr = rowRect(i);
            if (rd.kind == RowKind::Text) {
                drawText(c, (float)(rr.x + 2), (float)(rr.y + (rr.h - (int)lineHeight()) / 2), t.dim,
                         textFor(rd.key));
                continue;
            }
            if (rd.kind != RowKind::Button)
                drawText(c, (float)(rr.x + 2), (float)(rr.y + (rr.h - (int)lineHeight()) / 2),
                         live ? t.text : t.dim, rd.label);
            switch (rd.kind) {
                case RowKind::Num: {
                    NumRow* nr = numAt(i);
                    if (!nr) break;
                    nr->nf.paint(c, ctrlBox(i), sess_.index == i && nr->nf.editing);
                    break;
                }
                case RowKind::Toggle: {
                    const Rect box = toggleBox(i);
                    controlLine(c, box, 2.0f);
                    if (boolKey(rd.key)) fill(c, Rect{box.x + 4, box.y + 4, 8, 8}, t.accent, 2.0f);
                    break;
                }
                case RowKind::Combo: {
                    const Rect cb = ctrlBox(i);
                    fill(c, cb, t.surface, 2.0f);
                    stroke(c, cb, t.border, 1.0f, 2.0f);
                    const std::string& s = kPosItems[std::clamp(comboKey(rd.key), 0, 2)];
                    drawText(c, (float)(cb.x + 8), (float)(cb.y + (cb.h - (int)lineHeight()) / 2), t.text, s);
                    break;
                }
                case RowKind::Button: {
                    const Rect bb = buttonRect(i);
                    const bool hot = hoverBtn_ == i;
                    fill(c, bb, hot ? t.hover : t.panelHi, 2.0f);
                    stroke(c, bb, hot ? t.accent : t.border, 1.0f, 2.0f);
                    drawText(c, (float)(bb.x + (bb.w - (int)textWidth(rd.label)) / 2),
                             (float)(bb.y + (bb.h - (int)lineHeight()) / 2), t.text, rd.label);
                    break;
                }
                case RowKind::Color: {
                    const Rect sw = colorSwatch(i);
                    const float* col = colorKey(rd.key);
                    if (col) {
                        Color32 cc = rgba((int)(col[0] * 255.0f), (int)(col[1] * 255.0f),
                                          (int)(col[2] * 255.0f), 255);
                        fill(c, Rect{sw.x + 2, sw.y + 2, sw.w - 4, sw.h - 4}, cc, 2.0f);
                    }
                    controlLine(c, sw, 2.0f);
                    break;
                }
                case RowKind::Text:
                    break;
            }
        }
    }

    std::string textFor(int key) const
    {
        if (key == 5) return active() ? "" : "当前图层没有可处理的像素。";
        return "";
    }

    std::string fingerprint() const
    {
        char buf[512];
        const Layer* l = active();
        const LayerChild* c = tri();
        const LayerChild* lc = lift();
        const LayerStyle* s = style();
        std::snprintf(buf, sizeof(buf),
                      "%d|%d|%d|%.3f,%.3f,%.3f|%d|%d|%.2f,%.2f,%d,%.1f,%.2f|%d|%.2f,%.1f|%d|%.2f|%.1f,%.2f"
                      "|%.3f,%.3f,%.3f,%.3f|%.3f,%.3f,%.3f,%.3f|%.3f,%.3f,%.3f,%.3f",
                      (int)page_, l ? l->id : -1, c ? c->id : 0, c ? c->r : 1.0f, c ? c->g : 1.0f,
                      c ? c->b : 1.0f, c && c->keepLuma, s ? s->strokeOn : 0,
                      s ? (double)s->strokeWidth : 0.0, s ? (double)s->strokeOpacity : 0.0,
                      s ? (int)s->strokePos : 0, s ? (double)s->shadowDist : 0.0,
                      s ? (double)s->shadowSize : 0.0, s ? s->shadowOn : 0, s ? (double)s->shadowAngle : 0.0,
                      s ? (double)s->shadowOpacity : 0.0, s ? s->overlayOn : 0,
                      s ? (double)s->overlayOpacity : 0.0, lc ? lc->liftStrength : 0.0f,
                      lc ? lc->liftRadius : 0.0f,
                      s ? (double)s->strokeColor[0] : 0.0, s ? (double)s->strokeColor[1] : 0.0,
                      s ? (double)s->strokeColor[2] : 0.0, s ? (double)s->strokeColor[3] : 0.0,
                      s ? (double)s->shadowColor[0] : 0.0, s ? (double)s->shadowColor[1] : 0.0,
                      s ? (double)s->shadowColor[2] : 0.0, s ? (double)s->shadowColor[3] : 0.0,
                      s ? (double)s->overlayColor[0] : 0.0, s ? (double)s->overlayColor[1] : 0.0,
                      s ? (double)s->overlayColor[2] : 0.0, s ? (double)s->overlayColor[3] : 0.0);
        return std::string(buf);
    }

    Host& host_;
    Page page_ = Page::Tri;
    bool fragOpen_ = false;
    NumSession sess_;
    int hoverBtn_ = -1;
    NumRow pool_[kNumPool];
    std::string seen_;
};

inline std::unique_ptr<Node> makeImagePanelImpl(Host& host) { return std::make_unique<ImagePanelNode>(host); }

}
}
