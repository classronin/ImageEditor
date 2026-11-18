#include "nodes/Panels.h"
#include "nodes/TextSel.h"

#include "app/Editor.h"
#include "app/SavePath.h"
#include "core/ImageIo.h"
#include "nodes/AboutImg.h"
#include "core/LayerStyle.h"
#include "core/ShadowLift.h"
#include "host/Host.h"
#include "host/Shortcuts.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkVertices.h"
#include "include/effects/SkGradientShader.h"
#include "nodes/Style.h"
#include "nodes/Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ie {
namespace ui {

namespace {

const int kRowH = 30;
const int kHeaderH = 44;
const int kFooterH = 48;

std::string elideTo(const std::string& s, float maxW)
{
    if (maxW <= 0.0f) return {};
    if (textWidth(s) <= maxW) return s;
    static const std::string ell = "...";
    const float ew = textWidth(ell);
    std::string out;
    float acc = 0.0f;
    for (size_t i = 0; i < s.size();) {
        const unsigned char ch = (unsigned char)s[i];
        size_t len = 1;
        if (ch >= 0xF0) len = 4;
        else if (ch >= 0xE0) len = 3;
        else if (ch >= 0xC0) len = 2;
        const std::string cp = s.substr(i, len);
        const float cw = textWidth(cp);
        if (acc + cw + ew > maxW) break;
        out += cp;
        acc += cw;
        i += len;
    }
    return out + ell;
}


inline float caretPixelX(const std::string& s, float baseX, int byteOff)
{
    float x = baseX;
    const int n = (int)s.size();
    int i = 0;
    while (i < n && i < byteOff) {
        const int j = utf8::next(s, i);
        x += textWidth(s.substr(i, (size_t)(j - i)));
        i = j;
    }
    return x;
}
inline int caretAtPixel(const std::string& s, float baseX, float px)
{
    const int n = (int)s.size();
    float x = baseX;
    int i = 0;
    while (i < n) {
        const int j = utf8::next(s, i);
        const float w = textWidth(s.substr(i, (size_t)(j - i)));
        if (x + w >= px) return (px - x < w * 0.5f) ? i : j;
        x += w;
        i = j;
    }
    return n;
}

constexpr float kPi = 3.14159265358979f;

inline void hsvToRgb(float h, float s, float v, float& r, float& g, float& b)
{
    float nr = std::fabs(h * 6.0f - 3.0f) - 1.0f;
    float ng = 2.0f - std::fabs(h * 6.0f - 2.0f);
    float nb = 2.0f - std::fabs(h * 6.0f - 4.0f);
    nr = std::clamp(nr, 0.0f, 1.0f);
    ng = std::clamp(ng, 0.0f, 1.0f);
    nb = std::clamp(nb, 0.0f, 1.0f);
    r = ((nr - 1.0f) * s + 1.0f) * v;
    g = ((ng - 1.0f) * s + 1.0f) * v;
    b = ((nb - 1.0f) * s + 1.0f) * v;
}

inline void rgbToHsv(float r, float g, float b, float& h, float& s, float& v)
{
    float k = 0.0f;
    if (g < b) { std::swap(g, b); k = -1.0f; }
    float minGb = b;
    if (r < g) { std::swap(r, g); k = -2.0f / 6.0f - k; minGb = std::min(g, b); }
    const float chroma = r - minGb;
    h = std::fabs(k + (g - b) / (6.0f * chroma + 1e-20f));
    s = chroma / (r + 1e-20f);
    v = r;
}

inline void hsvCircleValsFromPos(float cx, float cy, float radius, float mx, float my, float& h, float& s)
{
    const float dx = mx - cx, dy = my - cy;
    const float distSq = dx * dx + dy * dy;
    s = (distSq < radius * radius) ? std::sqrt(distSq) / radius : 1.0f;
    h = std::atan2(dx, dy) / (2.0f * kPi) + 0.5f;
}

inline void hsvCirclePosFromVals(float cx, float cy, float radius, const float hsv[3], float& x, float& y)
{
    const float ang = 2.0f * kPi * hsv[0] + kPi / 2.0f;
    const float rad = std::clamp(hsv[1], 0.0f, 1.0f) * radius;
    x = cx + std::cos(-ang) * rad;
    y = cy + std::sin(-ang) * rad;
}

inline SkColor skCol(Color32 c)
{
    return SkColorSetARGB((c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255);
}

inline SkColor rgb8(float r, float g, float b)
{
    return SkColorSetRGB((uint8_t)(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f),
                         (uint8_t)(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f),
                         (uint8_t)(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f));
}

class Modal : public Node {
public:
    Modal(Host& host, std::string title, int width) : host_(host), title_(std::move(title)), width_(width) {}

    Size requestSize() const override { return Size{width_, panel().h}; }
    bool focusable() const override { return true; }
    bool swallowsOutsidePress() const override { return true; }

    void paint(SkCanvas& c) override
    {
        const Theme& t = theme();
        const Rect p = panel();
        for (int i = 6; i >= 1; --i) {
            const Rect sp{p.x - (float)i, p.y - (float)i + 3.0f, p.w + 2.0f * i, p.h + 2.0f * i};
            fill(c, sp, (Color32)(((uint32_t)(0x07 * i)) << 24), 5.0f + (float)i);
        }

        fill(c, p, t.panel, 5.0f);
        stroke(c, p, t.border, 1.0f, 5.0f);
        if (!title_.empty()) {
            drawText(c, (float)(p.x + 16), (float)(p.y + 14), t.text, title_);
            fill(c, Rect{p.x + 12, p.y + 40, p.w - 24, 1}, t.border);
        }
        if (insetL_ > 0) paintLeft(c);

        for (size_t i = 0; i < rows_.size(); ++i) {
            const Row& row = rows_[i];
            const Rect rr = rowRect((int)i);
            if (row.kind == RLabel) {
                drawText(c, (float)(rr.x + 4), (float)(rr.y + (rr.h - (int)lineHeight()) / 2), t.dim,
                         elideTo(row.s ? *row.s : row.label, (float)rr.w - 8));
                continue;
            }
            if (row.kind == RPicker) {
                paintPicker(c, row, rr);
                continue;
            }
            if (row.kind != RButton)
                drawText(c, (float)(rr.x + 4), (float)(rr.y + (rr.h - (int)lineHeight()) / 2), t.text,
                         row.label);

            if (row.kind == RColor) {
                const Rect box = ctrlRect(rr);
                fill(c, box, t.panelHi, 2.0f);
                if (row.col) {
                    const Color32 col = rgba((int)(row.col[0] * 255), (int)(row.col[1] * 255),
                                             (int)(row.col[2] * 255), 255);
                    fill(c, Rect{box.x + 2, box.y + 2, box.w - 4, box.h - 4}, col, 2.0f);
                }
                stroke(c, box, hover_ == (int)i ? t.accent : t.border, 1.0f, 2.0f);
                continue;
            }

            const Rect box = ctrlRect(rr);
            if (row.kind == RInt || row.kind == RNum) {
                bindRowNum(row, (int)i);
                num_.paint(c, box, sess_.index == (int)i && num_.editing);
            } else if (row.kind == RCheck) {
                Rect cb{box.x, box.y + 3, 16, 16};
                stroke(c, cb, t.border, 1.0f, 2.0f);
                if (row.b && *row.b) fill(c, Rect{cb.x + 4, cb.y + 4, 8, 8}, t.accent, 2.0f);
                drawText(c, (float)(cb.right() + 8), (float)(box.y + (box.h - (int)lineHeight()) / 2), t.dim,
                         row.b && *row.b ? "开" : "关");
            } else if (row.kind == RText) {
                const std::string* sp = row.s;
                fill(c, box, t.surface, 2.0f);
                stroke(c, box, editing_ == (int)i ? t.accent : t.border, 1.0f, 2.0f);
                const std::string txt = sp ? *sp : std::string();
                const float baseX = (float)(box.x + 6);
                const float ty = (float)(box.y + (box.h - (int)lineHeight()) / 2);
                if (editing_ == (int)i && sp && sel.hasSel()) {
                    const int a = sel.lo(), b = sel.hi();
                    const float sx = caretPixelX(txt, baseX, a);
                    const float ex = caretPixelX(txt, baseX, b);
                    fill(c, Rect{(int)sx, box.y + 2, std::max(0, (int)(ex - sx)), box.h - 4},
                         (Color32)0x504a90e2, 0.0f);
                }
                drawText(c, baseX, ty, t.text, elideTo(txt, (float)box.w - 12));
                if (editing_ == (int)i && sp) {
                    float cx = caretPixelX(txt, baseX, utf8::snap(txt, sel.caret));
                    if (cx > (float)box.right() - 2) cx = (float)box.right() - 2;
                    if (cx < baseX) cx = baseX;
                    fill(c, Rect{(int)cx, box.y + 3, 1, box.h - 6}, t.accent, 0.0f);
                }
            } else if (row.kind == RButton) {
                fill(c, box, hover_ == (int)i ? t.pressed : t.panelHi, 2.0f);
                stroke(c, box, t.border, 1.0f, 2.0f);
                drawText(c, (float)(box.x + (box.w - (int)textWidth(row.label)) / 2),
                         (float)(box.y + (box.h - (int)lineHeight()) / 2), t.text, row.label);
            } else if (row.kind == RSelect) {
                fill(c, box, t.panelHi, 2.0f);
                stroke(c, box, t.border, 1.0f, 2.0f);
                const int sel = row.i ? std::clamp(*row.i, 0, (int)row.items.size() - 1) : 0;
                const std::string cur =
                    (sel >= 0 && sel < (int)row.items.size()) ? row.items[(size_t)sel] : std::string();
                drawText(c, (float)(box.x + 6), (float)(box.y + (box.h - (int)lineHeight()) / 2), t.text,
                         elideTo(cur, (float)box.w - 12));
            }
        }

        if (extraPaint_) {
            int y = rowsTop();
            for (const Row& r : rows_) y += r.h;
            extraPaint_(c, Rect{p.x + 16, y, p.w - 32, extraH_});
        }

        if (!footerBtns_.empty()) {
            const int n = (int)footerBtns_.size();
            for (int i = 0; i < n; ++i) {
                const Rect b = footerBtnRect(i);
                const bool primary = i == 0;
                fill(c, b, primary ? (hoverFooter_ == i ? t.pressed : t.accent)
                                   : (hoverFooter_ == i ? t.hover : t.panelHi), 3.0f);
                if (!primary) stroke(c, b, t.border, 1.0f, 3.0f);
                drawText(c, (float)(b.x + (b.w - (int)textWidth(footerBtns_[(size_t)i].first)) / 2),
                         (float)(b.y + (b.h - (int)lineHeight()) / 2),
                         primary ? t.text : t.text, footerBtns_[(size_t)i].first);
            }
        }

        if (!noFooter_) {
            const Rect ok = okRect();
            const Rect ca = cancelRect();
            fill(c, ok, hoverOk_ ? t.pressed : t.accent, 3.0f);
            drawText(c, (float)(ok.x + (ok.w - (int)textWidth(okLabel_)) / 2),
                     (float)(ok.y + (ok.h - (int)lineHeight()) / 2), t.text, okLabel_);
            if (showReset_) {
                const Rect rs = resetRect();
                fill(c, rs, hoverReset_ ? t.hover : t.panelHi, 3.0f);
                stroke(c, rs, t.border, 1.0f, 3.0f);
                drawText(c, (float)(rs.x + (rs.w - (int)textWidth(resetLabel_)) / 2),
                         (float)(rs.y + (rs.h - (int)lineHeight()) / 2), t.text, resetLabel_);
            }
            if (showCancel_) {
                fill(c, ca, hoverCancel_ ? t.hover : t.panelHi, 3.0f);
                stroke(c, ca, t.border, 1.0f, 3.0f);
                drawText(c, (float)(ca.x + (ca.w - (int)textWidth(cancelLabel_)) / 2),
                         (float)(ca.y + (ca.h - (int)lineHeight()) / 2), t.text, cancelLabel_);
            }
        }
    }

    bool onPointer(const PointerEvent& e) override
    {
        const Rect p = panel();
        const bool inside = p.contains((int)e.pos.x, (int)e.pos.y);

        if (sess_.busy() && sess_.index >= 0 && sess_.index < (int)rows_.size()) {
            Row& r = rows_[(size_t)sess_.index];
            if (r.kind == RInt || r.kind == RNum) {
                bindRowNum(r, sess_.index);
                const bool handled = sess_.route(e, ctrlRect(rowRect(sess_.index)));
                if (handled) {
                    changed();
                    markDirtyArea();
                    return true;
                }
            }
        }

        if (e.pressed && !inside) {
            requestClose();
            return true;
        }
        if (!inside) return true;

        if (!footerBtns_.empty()) {
            const int fb = footerBtnAt((int)e.pos.x, (int)e.pos.y);
            if (e.released) {
                if (pressedFooter_ >= 0) {
                    const int k = pressedFooter_;
                    pressedFooter_ = -1;
                    if (k == fb && footerBtns_[(size_t)k].second) footerBtns_[(size_t)k].second();
                    return true;
                }
            }
            if (!e.down || e.pressed) {
                hoverFooter_ = fb;
                pressedFooter_ = (e.pressed && fb >= 0) ? fb : pressedFooter_;
                markDirtyArea();
                return true;
            }
        }

        if (e.pressed && e.pos.y < (float)(p.y + kHeaderH)) {
            moving_ = true;
            movePressX_ = e.pos.x;
            movePressY_ = e.pos.y;
            moveStartX_ = moveX_;
            moveStartY_ = moveY_;
            markDirty(frame_);
            return true;
        }
        if (moving_) {
            if (e.down && !e.pressed) {
                const Rect cp = centeredPanel();
                const int loX = frame_.x + 8 - cp.x, hiX = frame_.right() - 8 - cp.w - cp.x;
                const int loY = frame_.y + 8 - cp.y, hiY = frame_.bottom() - kHeaderH - 8 - cp.y;
                int nx = moveStartX_ + (int)(e.pos.x - movePressX_);
                int ny = moveStartY_ + (int)(e.pos.y - movePressY_);
                if (loX <= hiX) nx = std::clamp(nx, loX, hiX);
                if (loY <= hiY) ny = std::clamp(ny, loY, hiY);
                if (nx != moveX_ || ny != moveY_) {
                    moveX_ = nx;
                    moveY_ = ny;
                    markDirty(frame_);
                }
                return true;
            }
            if (e.released) {
                moving_ = false;
                return true;
            }
        }

        const bool okHover = !noFooter_ && okRect().contains((int)e.pos.x, (int)e.pos.y);
        const bool caHover = !noFooter_ && showCancel_ && cancelRect().contains((int)e.pos.x, (int)e.pos.y);
        const bool rsHover = !noFooter_ && showReset_ && resetRect().contains((int)e.pos.x, (int)e.pos.y);
        if (okHover || caHover || rsHover || hover_ >= 0 || drag_ >= 0) {
            markDirtyArea();
            hoverOk_ = okHover;
            hoverCancel_ = caHover;
            hoverReset_ = rsHover;
        }

        if (e.released) {
            sel.dragging = false;
            if (drag_ >= 0) {
                drag_ = -1;
                pickerGrab_ = 0;
                return true;
            }
            if (pressedOk_ && okHover) {
                pressedOk_ = false;
                if (num_.editing) num_.commit();
                commit();
                if (!okKeepsOpen_) requestClose();
                return true;
            }
            if (pressedOk_ && caHover) {
                pressedOk_ = false;
                cancel();
                requestClose();
                return true;
            }
            if (pressedRes_ && rsHover) {
                pressedRes_ = false;
                if (num_.editing) num_.commit();
                doReset();
                markDirtyArea();
                return true;
            }
            pressedOk_ = false;
            pressedRes_ = false;
            return true;
        }

        if (!e.down) {
            const int hov = rowAt((int)e.pos.x, (int)e.pos.y);
            if (hov != hover_) {
                hover_ = hov;
                markDirtyArea();
            }
            return true;
        }

        if (e.pressed) {
            hoverOk_ = okHover;
            hoverCancel_ = caHover;
            hoverReset_ = rsHover;
            if (rsHover) {
                pressedRes_ = true;
                return true;
            }
            if (okHover || caHover) {
                pressedOk_ = true;
                return true;
            }
            const int idx = rowAt((int)e.pos.x, (int)e.pos.y);
            if (idx < 0) {
                if (insetL_ > 0 && e.pos.x < (float)(panel().x + 16 + insetL_) && onLeftClick(e))
                    return true;
                editing_ = -1;
                return true;
            }
            Row& row = rows_[(size_t)idx];
            const Rect rr = rowRect(idx);
            const Rect box = ctrlRect(rr);
            if (row.kind == RCheck && row.b) {
                *row.b = !*row.b;
                changed();
            } else if (row.kind == RInt || row.kind == RNum) {
                beginRowNum(row, idx);
                num_.pointer(e, box);
                return true;
            } else if (row.kind == RText && row.s) {
                editing_ = idx;
                sel.reset();
                sel.caret = caretAtPixel(*row.s, (float)(box.x + 6), e.pos.x);
                sel.anchor = sel.caret;
                sel.dragging = true;
                return true;
            } else if (row.kind == RButton && row.action) {
                row.action();
            } else if (row.kind == RColor && row.col) {
                float* col = row.col;
                host_.pushDialogNode(makeColorPopup(host_, col, [this, col](const float rgba[4]) {
                    for (int k = 0; k < 4; ++k) col[k] = rgba[k];
                    changed();
                    markDirtyArea();
                }));
            } else if (row.kind == RPicker && row.hsv) {
                const Rect pw = pickerWheel(rr), ps = pickerStrip(rr);
                const bool inWheel = pw.contains((int)e.pos.x, (int)e.pos.y);
                const bool inStrip = ps.contains((int)e.pos.x, (int)e.pos.y);
                if (inWheel || inStrip) {
                    drag_ = idx;
                    pickerGrab_ = inStrip ? 2 : 1;
                    applyPicker(idx, e.pos.x, e.pos.y);
                }
            } else if (row.kind == RSelect && row.i) {
                host_.openListPopup(row.items, *row.i, (float)box.x, (float)box.bottom(),
                                    [this, idx](int pick) {
                                        Row& rw = rows_[(size_t)idx];
                                        if (rw.i) *rw.i = pick;
                                        if (rw.onPick) rw.onPick(pick);
                                        changed();
                                        markDirtyArea();
                                    });
            }
            (void)box;
            markDirtyArea();
        } else if (sel.dragging) {
            if (editing_ >= 0 && editing_ < (int)rows_.size()) {
                Row& r = rows_[(size_t)editing_];
                if (r.kind == RText && r.s) {
                    const Rect bx = ctrlRect(rowRect(editing_));
                    sel.caret = caretAtPixel(*r.s, (float)(bx.x + 6), e.pos.x);
                    markDirtyArea();
                }
            }
        } else if (drag_ >= 0) {
            Row& row = rows_[(size_t)drag_];
            if (row.kind == RPicker) applyPicker(drag_, e.pos.x, e.pos.y);
            markDirtyArea();
        }
        return true;
    }

    bool onKey(const KeyEvent& e) override
    {
        if (!e.pressed) return true;
        if (num_.editing) {
            if (sess_.index >= 0 && sess_.index < (int)rows_.size()) bindRowNum(rows_[(size_t)sess_.index], sess_.index);
            num_.key(e);
            if (!num_.editing) sess_.clear();
            changed();
            markDirtyArea();
            return true;
        }
        if (e.scancode == KeyEsc) {
            cancel();
            requestClose();
            return true;
        }
        if (editing_ >= 0 && editing_ < (int)rows_.size()) {
            Row& row = rows_[(size_t)editing_];
            if (row.kind == RText && editStr(row)) {
                std::string& s = *editStr(row);
                sel.clamp((int)s.size());
                if (ui::textSelKey(e, sel, s, ui::Utf8Step{}, ui::PasteRule::Text)) {
                    sel.clamp((int)s.size());
                    changed();
                    markDirtyArea();
                    return true;
                }
                if (e.scancode == KeyEnter) {
                    sel.clear();
                    editing_ = -1;
                    changed();
                    markDirtyArea();
                    return true;
                }
                if (!e.ctrl && !e.alt && e.unicode >= 32 && e.unicode != 0x7F) {
                    ui::textSelInsert(sel, s, utf8::encode(e.unicode));
                    changed();
                    markDirtyArea();
                    return true;
                }
                markDirtyArea();
                return true;
            }
        }
        if (e.scancode == KeyEnter) {
            commit();
            if (!okKeepsOpen_) requestClose();
            return true;
        }
        return true;
    }

    void markDirtyArea() { Node::markDirty(panel()); }

protected:
    enum { RLabel = 0, RText, RInt, RCheck, RColor, RButton, RSelect, RNum, RPicker };

    int insetL_ = 0;
    virtual void paintLeft(SkCanvas&) {}
    virtual bool onLeftClick(const PointerEvent&) { return false; }

    bool noFooter_ = false;
    std::string okLabel_ = "确定";
    std::string cancelLabel_ = "取消";
    std::string resetLabel_ = "重置";
    bool showReset_ = false;
    bool okKeepsOpen_ = false;
    std::vector<std::pair<std::string, std::function<void()>>> footerBtns_;
    int hoverFooter_ = -1;
    int pressedFooter_ = -1;
    bool showCancel_ = true;
    int extraH_ = 0;
    std::function<void(SkCanvas&, const Rect&)> extraPaint_;

    struct Row {
        int kind = RLabel;
        std::string label;
        std::string* s = nullptr;
        int* i = nullptr;
        bool* b = nullptr;
        float* col = nullptr;
        float* hsv = nullptr;
        std::function<void()> action;
        std::function<void(int)> onPick;
        std::vector<std::string> items;
        double* num = nullptr;
        double nmin = 0, nmax = 100, nstep = 1;
        std::string nfmt{"%.0f"};
        int h = kRowH;
    };

    static double rowNumGet(const void* ud)
    {
        const Row* r = (const Row*)ud;
        if (!r) return 0.0;
        if (r->num) return *r->num;
        return r->i ? (double)*r->i : 0.0;
    }
    static void rowNumSet(void* ud, double v)
    {
        Row* r = (Row*)ud;
        if (!r) return;
        if (r->num) {
            *r->num = v;
            return;
        }
        if (r->i) *r->i = (int)std::llround(v);
    }

    void bindRowNum(const Row& r, int idx)
    {
        num_.get = rowNumGet;
        num_.readUd = (const void*)&rows_[(size_t)idx];
        num_.set = rowNumSet;
        num_.writeUd = (void*)&rows_[(size_t)idx];
        num_.lo = r.nmin;
        num_.hi = r.nmax;
        num_.step = r.nstep;
        num_.fmt = r.nfmt.c_str();
    }
    void beginRowNum(const Row& r, int idx)
    {
        sess_.take(idx, &num_);
        bindRowNum(r, idx);
    }

    std::string* editStr(Row& r) const
    {
        return (r.kind == RText) ? r.s : nullptr;
    }


    void addLabel(std::string text)
    {
        Row r;
        r.kind = RLabel;
        r.label = std::move(text);
        rows_.push_back(std::move(r));
    }

    void addText(std::string label, std::string* s)
    {
        Row r;
        r.kind = RText;
        r.label = std::move(label);
        r.s = s;
        rows_.push_back(std::move(r));
    }

    void addInt(std::string label, int* v, int lo, int hi)
    {
        Row r;
        r.kind = RInt;
        r.label = std::move(label);
        r.i = v;
        r.nmin = (double)lo;
        r.nmax = (double)hi;
        r.nstep = 1.0;
        r.nfmt = "%.0f";
        rows_.push_back(std::move(r));
    }

    void addSelect(std::string label, int* sel, std::vector<std::string> items,
                   std::function<void(int)> onPick = nullptr)
    {
        Row r;
        r.kind = RSelect;
        r.label = std::move(label);
        r.i = sel;
        r.items = std::move(items);
        r.onPick = std::move(onPick);
        rows_.push_back(std::move(r));
    }

    void addCheck(std::string label, bool* b)
    {
        Row r;
        r.kind = RCheck;
        r.label = std::move(label);
        r.b = b;
        rows_.push_back(std::move(r));
    }

    void addNum(std::string label, double* v, double mn, double mx, double step, int prec)
    {
        Row r;
        r.kind = RNum;
        r.label = std::move(label);
        r.num = v;
        r.nmin = mn;
        r.nmax = mx;
        r.nstep = prec > 0 ? 0.0 : step;
        char fmt[8];
        std::snprintf(fmt, sizeof(fmt), "%%.%df", prec > 0 ? prec : 0);
        r.nfmt = fmt;
        rows_.push_back(std::move(r));
    }

    void clearRows()
    {
        rows_.clear();
        sess_.clear();
        num_.drag = false;
        num_.dragged = false;
        num_.editing = false;
        num_.buf.clear();
    }

    void addColor(std::string label, float* rgba)
    {
        Row r;
        r.kind = RColor;
        r.label = std::move(label);
        r.col = rgba;
        rows_.push_back(std::move(r));
    }

    void addPicker(float* hsv)
    {
        Row r;
        r.kind = RPicker;
        r.hsv = hsv;
        r.h = 216;
        rows_.push_back(std::move(r));
    }

    void addLabelRef(std::string* s)
    {
        Row r;
        r.kind = RLabel;
        r.s = s;
        rows_.push_back(std::move(r));
    }

    void addButton(std::string label, std::function<void()> fn)
    {
        Row r;
        r.kind = RButton;
        r.label = std::move(label);
        r.action = std::move(fn);
        rows_.push_back(std::move(r));
    }

    void addFooterButton(std::string label, std::function<void()> fn)
    {
        footerBtns_.emplace_back(std::move(label), std::move(fn));
    }

    Rect footerBtnRect(int i) const
    {
        const Rect p = panel();
        const int n = (int)footerBtns_.size();
        const int w = 84, gap = 8;
        const int x = p.right() - 16 - w - (n - 1 - i) * (w + gap);
        return Rect{x, p.bottom() - 12 - 30, w, 30};
    }

    int footerBtnAt(int x, int y) const
    {
        for (int i = 0; i < (int)footerBtns_.size(); ++i)
            if (footerBtnRect(i).contains(x, y)) return i;
        return -1;
    }

    virtual void commit() {}
    virtual void cancel() {}
    virtual void doReset() {}
    virtual void changed() {}

    void requestClose()
    {
        Host* hp = &host_;
        host_.post([hp]() { hp->closePopup(); });
    }

    Host& host_;

    int moveX_ = 0, moveY_ = 0;
    bool moving_ = false;
    float movePressX_ = 0, movePressY_ = 0;
    int moveStartX_ = 0, moveStartY_ = 0;

protected:
    Rect centeredPanel() const
    {
        int h = (title_.empty() ? 12 : kHeaderH + 8) + (noFooter_ ? 0 : kFooterH) + extraH_;
        for (const Row& r : rows_) h += r.h;
        const int w = width_;
        const int x = frame_.x + (frame_.w - w) / 2;
        const int y = frame_.y + (frame_.h - h) / 2;
        return Rect{x, y, w, h};
    }

    Rect panel() const
    {
        const Rect p = centeredPanel();
        int x = p.x + moveX_;
        int y = p.y + moveY_;
        const int minX = frame_.x + 8, maxX = frame_.right() - p.w - 8;
        const int minY = frame_.y + 8, maxY = frame_.bottom() - kHeaderH - 8;
        if (maxX >= minX) x = std::clamp(x, minX, maxX);
        if (maxY >= minY) y = std::clamp(y, minY, maxY);
        return Rect{x, y, p.w, p.h};
    }

    int rowsTop() const { return panel().y + (title_.empty() ? 12 : kHeaderH + 8); }

    Rect rowRect(int idx) const
    {
        int y = rowsTop();
        for (int i = 0; i < idx && i < (int)rows_.size(); ++i) y += rows_[(size_t)i].h;
        const Rect p = panel();
        const int h = (idx >= 0 && idx < (int)rows_.size()) ? rows_[(size_t)idx].h : kRowH;
        return Rect{p.x + 16 + insetL_, y, p.w - 32 - insetL_, h};
    }

    Rect ctrlRect(const Rect& rr) const
    {
        const int x = rr.x + 120;
        return Rect{x, rr.y + 4, std::max(0, rr.right() - x - 4), 22};
    }

    Rect pickerWheel(const Rect& rr) const
    {
        const int d = rr.h - 16;
        const int x0 = rr.x + (rr.w - (d + 10 + 18)) / 2;
        return Rect{x0, rr.y + 8, d, d};
    }

    Rect pickerStrip(const Rect& rr) const
    {
        const Rect w = pickerWheel(rr);
        return Rect{w.right() + 10, w.y, 18, w.h};
    }

    void paintPicker(SkCanvas& c, const Row& row, const Rect& rr) const
    {
        if (!row.hsv) return;
        const Theme& t = theme();
        const Rect w = pickerWheel(rr);
        const float cx = (float)w.x + (float)w.w * 0.5f;
        const float cy = (float)w.y + (float)w.h * 0.5f;
        const float radius = (float)std::min(w.w, w.h) * 0.5f;
        const float v = row.hsv[2];

        const int tot = 64;
        SkPoint pts[66];
        SkColor cols[66];
        pts[0] = SkPoint::Make(cx, cy);
        cols[0] = rgb8(v, v, v);
        for (int a = 0; a <= tot; ++a) {
            const float ang = 2.0f * kPi * (float)a / (float)tot;
            const float co = std::cos(ang), si = std::sin(ang);
            float hh = 0, ss = 0;
            hsvCircleValsFromPos(cx, cy, radius, cx + co * radius, cy + si * radius, hh, ss);
            float r = 0, g = 0, b = 0;
            hsvToRgb(hh, 1.0f, v, r, g, b);
            pts[a + 1] = SkPoint::Make(cx + co * radius, cy + si * radius);
            cols[a + 1] = rgb8(r, g, b);
        }
        if (sk_sp<SkVertices> verts =
                SkVertices::MakeCopy(SkVertices::kTriangleFan_VertexMode, tot + 2, pts, nullptr, cols)) {
            SkPaint p;
            p.setColor(SK_ColorWHITE);
            c.drawVertices(verts.get(), SkBlendMode::kModulate, p);
        }
        SkPaint ol;
        ol.setAntiAlias(true);
        ol.setStyle(SkPaint::kStroke_Style);
        ol.setStrokeWidth(1.0f);
        ol.setColor(skCol(t.border));
        c.drawCircle(cx, cy, radius, ol);

        float px = 0, py = 0;
        hsvCirclePosFromVals(cx, cy, radius, row.hsv, px, py);
        float cr = 0, cg = 0, cb = 0;
        hsvToRgb(row.hsv[0], row.hsv[1], v, cr, cg, cb);
        SkPaint cp;
        cp.setAntiAlias(true);
        cp.setColor(rgb8(cr, cg, cb));
        c.drawCircle(px, py, 7.0f, cp);
        cp.setStyle(SkPaint::kStroke_Style);
        cp.setStrokeWidth(1.0f);
        cp.setColor(SkColorSetARGB((int)(std::min(1.0f - v + 0.2f, 0.8f) * 255.0f), 255, 255, 255));
        c.drawCircle(px, py, 7.0f, cp);
        cp.setColor(SkColorSetARGB((int)(v * 0.5f * 255.0f), 0, 0, 0));
        c.drawCircle(px, py, 8.0f, cp);

        const Rect s = pickerStrip(rr);
        const SkPoint gpts[2] = {SkPoint::Make((float)s.x, (float)s.y),
                                 SkPoint::Make((float)s.x, (float)s.bottom())};
        const SkColor gcols[2] = {SK_ColorWHITE, SK_ColorBLACK};
        SkPaint gp;
        gp.setShader(SkGradientShader::MakeLinear(gpts, gcols, nullptr, 2, SkTileMode::kClamp));
        c.drawRect(SkRect::MakeLTRB((float)s.x, (float)s.y, (float)s.right(), (float)s.bottom()), gp);
        stroke(c, s, t.border, 1.0f);
        float iy = (float)s.y + (1.0f - v) * (float)s.h;
        iy = std::clamp(iy, (float)s.y + 2.0f, (float)s.bottom() - 2.0f);
        const float ch = std::max((float)s.w * 0.35f, 1.0f);
        const Rect ind{s.x, (int)(iy - ch), s.w, (int)(ch * 2.0f)};
        fill(c, ind, rgba(0, 0, 0), 1.0f);
        const int gv = (int)(v * 255.0f);
        fill(c, Rect{ind.x + 1, ind.y + 1, ind.w - 2, ind.h - 2}, rgba(gv, gv, gv), 1.0f);
    }

    Rect okRect() const
    {
        const Rect p = panel();
        return Rect{p.right() - 16 - 84, p.bottom() - 12 - 30, 84, 30};
    }

    Rect resetRect() const
    {
        const Rect p = panel();
        return Rect{p.right() - 16 - 84 - 8 - 84, p.bottom() - 12 - 30, 84, 30};
    }

    Rect cancelRect() const
    {
        const Rect p = panel();
        const int x = showReset_ ? p.right() - 16 - 84 - 8 - 84 - 8 - 84 : p.right() - 16 - 84 - 8 - 84;
        return Rect{x, p.bottom() - 12 - 30, 84, 30};
    }

    int rowAt(int x, int y) const
    {
        for (int i = 0; i < (int)rows_.size(); ++i)
            if (rowRect(i).contains(x, y)) return i;
        return -1;
    }

    void applyPicker(int idx, float x, float y)
    {
        Row& row = rows_[(size_t)idx];
        if (!row.hsv) return;
        const Rect rr = rowRect(idx);
        if (pickerGrab_ == 2) {
            const Rect s = pickerStrip(rr);
            row.hsv[2] = std::clamp(1.0f - (y - (float)s.y) / (float)std::max(1, s.h), 0.0f, 1.0f);
        } else {
            const Rect w = pickerWheel(rr);
            hsvCircleValsFromPos((float)w.x + (float)w.w * 0.5f, (float)w.y + (float)w.h * 0.5f,
                                 (float)std::min(w.w, w.h) * 0.5f, x, y, row.hsv[0], row.hsv[1]);
        }
        changed();
    }

    std::string title_;
    int width_;
    std::vector<Row> rows_;
    int hover_ = -1;
    int editing_ = -1;
    int drag_ = -1;
    int pickerGrab_ = 0;
    bool hoverOk_ = false, hoverCancel_ = false, pressedOk_ = false;
    bool hoverReset_ = false, pressedRes_ = false;

    ui::TextSel sel;

    NumField num_;
    NumSession sess_;
};


class NewDialog : public Modal {
public:
    explicit NewDialog(Host& h) : Modal(h, "新建图像", 380)
    {
        addNum("宽度", &w_, 1, 8192, 1, 0);
        addNum("高度", &h_, 1, 8192, 1, 0);
        addLabel("像素；在新标签页打开并自动适应窗口。");
    }
    void commit() override { host_.newTabCanvas((int)w_, (int)h_); }

private:
    double w_ = 1920, h_ = 1080;
};

class CanvasSizeDialog : public Modal {
public:
    explicit CanvasSizeDialog(Host& h) : Modal(h, "画布大小", 380)
    {
        w_ = h.ed().doc.width;
        h_ = h.ed().doc.height;
        addNum("宽度", &w_, 1, 8192, 1, 0);
        addNum("高度", &h_, 1, 8192, 1, 0);
        extraH_ = 10 + 84 + 6;
        extraPaint_ = [this](SkCanvas& c, const Rect& r) { paintGrid(c, r); };
    }
    void commit() override { host_.applyCanvasSize((int)std::lround(w_), (int)std::lround(h_), ax_, ay_); }

    bool onPointer(const PointerEvent& e) override
    {
        if (e.pressed) {
            const Rect g = gridRect();
            for (int i = 0; i < 9; ++i) {
                if (cellRect(g, i).contains((int)e.pos.x, (int)e.pos.y)) {
                    ax_ = i % 3;
                    ay_ = i / 3;
                    markDirtyArea();
                    return true;
                }
            }
        }
        return Modal::onPointer(e);
    }

private:
    static constexpr int kCell = 24, kGap = 6;

    Rect gridRect() const
    {
        const Rect p = panel();
        int y = rowsTop();
        for (const Row& r : rows_) y += r.h;
        const int gw = 3 * kCell + 2 * kGap;
        return Rect{p.x + (p.w - gw) / 2, y + 6, gw, gw};
    }
    Rect cellRect(const Rect& g, int i) const
    {
        const int col = i % 3, row = i / 3;
        return Rect{g.x + col * (kCell + kGap), g.y + row * (kCell + kGap), kCell, kCell};
    }
    void paintGrid(SkCanvas& c, const Rect& r) const
    {
        const Theme& t = theme();
        (void)r;
        const Rect g = gridRect();
        for (int i = 0; i < 9; ++i) {
            const Rect cr = cellRect(g, i);
            const bool on = (i % 3 == ax_ && i / 3 == ay_);
            fill(c, cr, on ? t.accent : t.panelHi, 3.0f);
            controlLine(c, cr, 3.0f);
        }
    }

    double w_ = 0, h_ = 0;
    int ax_ = 1, ay_ = 1;
};

class ImageSizeDialog : public Modal {
public:
    explicit ImageSizeDialog(Host& h) : Modal(h, "图像大小", 380)
    {
        w_ = baseW_ = h.ed().doc.width;
        h_ = baseH_ = h.ed().doc.height;
        lastW_ = baseW_;
        lastH_ = baseH_;
        addNum("宽度", &w_, 1, 8192, 1, 0);
        addNum("高度", &h_, 1, 8192, 1, 0);
        addCheck("保持比例", &keep_);
    }
    void changed() override
    {
        if (!keep_ || baseW_ <= 0 || baseH_ <= 0) return;
        const int W = (int)std::lround(w_), H = (int)std::lround(h_);
        if (W != lastW_) {
            lastW_ = W;
            h_ = std::max(1.0, std::round((double)W * baseH_ / baseW_));
            lastH_ = (int)std::lround(h_);
        } else if (H != lastH_) {
            lastH_ = H;
            w_ = std::max(1.0, std::round((double)H * baseW_ / baseH_));
            lastW_ = (int)std::lround(w_);
        }
    }
    void commit() override
    {
        int W = (int)std::lround(w_), H = (int)std::lround(h_);
        if (keep_ && baseW_ > 0 && baseH_ > 0) {
            if (W != baseW_) H = std::max(1, (int)(W * (double)baseH_ / baseW_ + 0.5));
            else if (H != baseH_) W = std::max(1, (int)(H * (double)baseW_ / baseH_ + 0.5));
        }
        host_.applyImageSize(W, H);
    }

private:
    double w_ = 0, h_ = 0;
    int baseW_ = 0, baseH_ = 0;
    int lastW_ = 0, lastH_ = 0;
    bool keep_ = true;
};

class ThresholdDialog : public Modal {
public:
    explicit ThresholdDialog(Host& h) : Modal(h, "阈值", 320)
    {
        okLabel_ = "应用";
        cancelLabel_ = "关闭";
        okKeepsOpen_ = true;
        showReset_ = true;
        addNum("阈值", &v_, 1, 254, 1, 0);
    }
    void commit() override { host_.ed().applyThreshold((int)std::lround(v_)); }
    void doReset() override { v_ = 128; }

private:
    double v_ = 128;
};

class BrightContrastDialog : public Modal {
public:
    explicit BrightContrastDialog(Host& h) : Modal(h, "亮/对比", 320)
    {
        okLabel_ = "应用";
        cancelLabel_ = "关闭";
        okKeepsOpen_ = true;
        showReset_ = true;
        addNum("亮度", &b_, -100, 100, 1, 0);
        addNum("对比度", &c_, -100, 100, 1, 0);
    }
    void commit() override
    {
        host_.ed().applyBrightnessContrast((float)b_, (float)c_);
    }
    void doReset() override
    {
        b_ = 0;
        c_ = 0;
    }

private:
    double b_ = 0, c_ = 0;
};

class HueDialog : public Modal {
public:
    explicit HueDialog(Host& h) : Modal(h, "色相", 320)
    {
        okLabel_ = "应用";
        cancelLabel_ = "关闭";
        okKeepsOpen_ = true;
        showReset_ = true;
        addNum("色相", &deg_, -180, 180, 1, 0);
    }
    void commit() override { host_.ed().applyHueShift((int)std::lround(deg_)); }
    void doReset() override { deg_ = 0; }

private:
    double deg_ = 0;
};

class SaveDialog : public Modal {
public:
    explicit SaveDialog(Host& h, int closeTabAfter = -1, const std::string& initialPath = {})
        : Modal(h, "保存图像", 520), closeTabAfter_(closeTabAfter)
    {
        const imageio::SaveOptions& last = h.ed().saveOpts;
        pngComp_ = last.compression;
        jpgQ_ = last.quality;
        webpQ_ = last.quality;
        webpLoss_ = last.lossless;
        webpEff_ = last.effort;

        if (initialPath.empty()) {
            dir_ = h.defaultSaveDir();
            name_ = h.defaultSaveName();
        } else {
            dir_ = ie::dirNameOf(initialPath);
            const size_t sep = initialPath.find_last_of("\\/");
            name_ = (sep == std::string::npos) ? initialPath : initialPath.substr(sep + 1);
            type_ = typeForExt(initialPath);
        }
        buildRows();
    }

    void commit() override
    {
        refreshPath();
        imageio::SaveOptions o;
        if (type_ == 1) {
            o.quality = (int)std::lround(jpgQ_);
        } else if (type_ == 2) {
            o.quality = (int)std::lround(webpQ_);
            o.lossless = webpLoss_;
            o.effort = (int)std::lround(webpEff_);
        } else if (type_ == 0) {
            o.compression = (int)std::lround(pngComp_);
        }
        host_.ed().saveOpts = o;
        if (host_.saveTo(path_, o)) {
            if (closeTabAfter_ == -2)
                host_.continueQuitAfterSave();
            else if (closeTabAfter_ >= 0)
                host_.requestCloseTab(closeTabAfter_);
        }
    }
    void changed() override { refreshPath(); }

private:
    static int typeForExt(const std::string& path)
    {
        const size_t dot = path.find_last_of('.');
        std::string e = (dot == std::string::npos) ? std::string() : path.substr(dot + 1);
        for (auto& c : e) c = (char)std::tolower((unsigned char)c);
        if (e == "jpg" || e == "jpeg") return 1;
        if (e == "webp") return 2;
        if (e == "ora") return 3;
        return 0;
    }

    void buildRows()
    {
        clearRows();
        addText("目录", &dir_);
        addButton("浏览…", [this]() {
            host_.browseSaveDir(dir_);
            refreshPath();
            markDirtyArea();
        });
        addText("文件名", &name_);
        addSelect("格式", &type_, {"PNG", "JPG", "WebP", "ORA"},
                  [this](int) {
                      syncNameExt();
                      buildRows();
                      markDirtyArea();
                  });

        if (type_ == 0) {
            addNum("压缩级别", &pngComp_, 0, 9, 1, 0);
        } else if (type_ == 1) {
            addNum("质量", &jpgQ_, 1, 100, 1, 0);
        } else if (type_ == 2) {
            addNum("质量", &webpQ_, 1, 100, 1, 0);
            addCheck("无损", &webpLoss_);
            addNum("压缩力度", &webpEff_, 0, 6, 1, 0);
        }
        addLabelRef(&pathText_);
        refreshPath();
    }

    void refreshPath()
    {
        path_ = ie::uniquePathName(dir_, name_, ie::saveExtFor(type_), ie::imageio::exists);
        pathText_ = "将保存到：" + path_;
    }

    void syncNameExt()
    {
        const size_t sep = name_.find_last_of("\\/");
        const size_t dot = name_.find_last_of('.');
        if (dot == std::string::npos) return;
        if (sep != std::string::npos && dot < sep) return;
        name_ = name_.substr(0, dot) + ie::saveExtFor(type_);
    }

    std::string dir_, name_, path_, pathText_;
    int type_ = 0;
    int closeTabAfter_ = -1;
    double pngComp_ = 6.0, jpgQ_ = 92.0, webpQ_ = 92.0, webpEff_ = 4.0;
    bool webpLoss_ = false;
};

class CloseDialog : public Modal {
public:
    CloseDialog(Host& h, int tab) : Modal(h, "关闭文档", 360), tab_(tab)
    {
        noFooter_ = true;
        extraH_ = 42;
        addLabel("当前图像有未保存的修改，保存后再关闭？");
        addFooterButton("保存", [this]() {
            Host* hp = &host_;
            const int t = tab_;
            requestClose();
            hp->post([hp, t]() { hp->saveAndCloseTab(t); });
        });
        addFooterButton("不保存", [this]() {
            Host* hp = &host_;
            const int t = tab_;
            requestClose();
            hp->post([hp, t]() { hp->closeTabNoSave(t); });
        });
        addFooterButton("取消", [this]() { requestClose(); });
    }

private:
    int tab_;
};

class QuitDialog : public Modal {
public:
    explicit QuitDialog(Host& h) : Modal(h, "退出", 430)
    {
        noFooter_ = true;
        extraH_ = 42;
        const int n = h.unsavedTabCount();
        if (n > 1) {
            line_ = "有 " + std::to_string(n) + " 个图像未保存，退出前要保存吗？";
        } else {
            line_ = "当前图像有未保存的修改，退出前要保存吗？";
        }
        addLabel(line_.c_str());
        addFooterButton("保存", [this]() {
            Host* hp = &host_;
            requestClose();
            hp->post([hp]() { hp->quitSaveAll(); });
        });
        addFooterButton("不保存", [this]() {
            Host* hp = &host_;
            requestClose();
            hp->post([hp]() { hp->quitNoSave(); });
        });
        addFooterButton("取消", [this]() { requestClose(); });
    }

private:
    std::string line_;
};

class AboutDialog : public Modal {
public:
    explicit AboutDialog(Host& h) : Modal(h, "", 232)
    {
        noFooter_ = true;
        showCancel_ = false;
        extraH_ = 22 + 22 + 10 + 200 + 10;
        loadImage();
        extraPaint_ = [this](SkCanvas& c, const Rect& r) { paintContent(c, r); };
    }

private:
    static constexpr int kImgSide = 200;

    sk_sp<SkImage> img_;

    static bool b64decode(const std::string& in, std::vector<uint8_t>& out)
    {
        auto val = [](char ch) -> int {
            if (ch >= 'A' && ch <= 'Z') return ch - 'A';
            if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
            if (ch >= '0' && ch <= '9') return ch - '0' + 52;
            if (ch == '+') return 62;
            if (ch == '/') return 63;
            return -1;
        };
        out.clear();
        out.reserve(in.size() * 3 / 4 + 4);
        int acc = 0, bits = 0;
        for (char ch : in) {
            if (ch == '\n' || ch == '\r') continue;
            if (ch == '=') break;
            const int v = val(ch);
            if (v < 0) return false;
            acc = (acc << 6) | v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back((uint8_t)((acc >> bits) & 0xFF));
            }
        }
        return !out.empty();
    }

    void loadImage()
    {
        std::vector<uint8_t> bytes;
        if (!b64decode(aboutimg::base64(), bytes)) return;
        ie::Image img;
        if (!ie::imageio::decode(bytes, img) || img.empty()) return;
        const size_t n = (size_t)img.width() * (size_t)img.height() * 4;
        auto data = SkData::MakeWithCopy(img.data(), n);
        img_ = SkImages::RasterFromData(
            SkImageInfo::Make(img.width(), img.height(), kRGBA_8888_SkColorType, kUnpremul_SkAlphaType), data,
            (size_t)img.width() * 4);
    }

    void paintContent(SkCanvas& c, const Rect& r) const
    {
        const Theme& t = theme();
        const char* brand = "ClassRonin";
        drawText(c, (float)(r.x + (r.w - (int)textWidth(brand)) / 2), (float)(r.y + 2), t.accent, brand);
        const char* line = "请我喝杯咖啡";
        drawText(c, (float)(r.x + (r.w - (int)textWidth(line)) / 2), (float)(r.y + 24), t.text, line);
        const int x = r.x + (r.w - kImgSide) / 2, y = r.y + 22 + 22 + 10;
        const SkRect dst = SkRect::MakeXYWH((float)x, (float)y, (float)kImgSide, (float)kImgSide);
        if (img_) {
            c.drawImageRect(img_.get(), SkRect::MakeWH((float)img_->width(), (float)img_->height()), dst,
                            SkSamplingOptions(SkFilterMode::kLinear), nullptr,
                            SkCanvas::kFast_SrcRectConstraint);
        } else {
            fill(c, Rect{x, y, kImgSide, kImgSide}, t.surface, 3.0f);
            stroke(c, Rect{x, y, kImgSide, kImgSide}, t.border, 1.0f, 3.0f);
            drawText(c, (float)(x + (kImgSide - (int)textWidth("img/img.png")) / 2), (float)(y + 90), t.dim,
                     "img/img.png");
        }
    }
};

class ColorPopup : public Modal {
public:
    ColorPopup(Host& h, const float* rgba, std::function<void(const float[4])> onChange)
        : Modal(h, "颜色", 380), onChange_(std::move(onChange))
    {
        rgbToHsv(rgba ? rgba[0] : 0.0f, rgba ? rgba[1] : 0.0f, rgba ? rgba[2] : 0.0f,
                 hsv_[0], hsv_[1], hsv_[2]);
        alpha_ = rgba ? (int)(rgba[3] * 255.0f + 0.5f) : 0;
        addPicker(hsv_);
        addInt("不透明度", &alpha_, 0, 255);
        addButton("吸管（点画布取色）", [this]() {
            Host* hp = &host_;
            auto cb = onChange_;
            requestClose();
            hp->beginColorPick([cb](float r, float g, float b, float a) {
                float c[4] = {r, g, b, a};
                if (cb) cb(c);
            });
        });
    }
    void commit() override { emit(); }
    void changed() override { emit(); }

private:
    void emit()
    {
        float out[4];
        hsvToRgb(hsv_[0], hsv_[1], hsv_[2], out[0], out[1], out[2]);
        out[3] = (float)alpha_ / 255.0f;
        if (onChange_) onChange_(out);
    }

    std::function<void(const float[4])> onChange_;
    float hsv_[3] = {0.0f, 0.0f, 1.0f};
    int alpha_ = 255;
};

class ShortcutsDialog : public Node {
public:
    explicit ShortcutsDialog(Host& h) : host_(h), draft_(shortcuts::rows())
    {
        buildGroups();
    }

    Size requestSize() const override { return Size{560, 408}; }
    bool focusable() const override { return true; }
    bool swallowsOutsidePress() const override { return true; }

    void paint(SkCanvas& c) override
    {
        const Theme& t = theme();
        const Rect p = panel();
        for (int i = 6; i >= 1; --i) {
            const Rect sp{p.x - (float)i, p.y - (float)i + 3.0f, p.w + 2.0f * i, p.h + 2.0f * i};
            fill(c, sp, (Color32)(((uint32_t)(0x07 * i)) << 24), 5.0f + (float)i);
        }
        fill(c, p, t.panel, 5.0f);
        stroke(c, p, t.border, 1.0f, 5.0f);
        drawText(c, (float)(p.x + 16), (float)(p.y + 14), t.text, "快捷键");
        fill(c, Rect{p.x + 12, p.y + 40, p.w - 24, 1}, t.border);

        for (int i = 0; i < (int)groups_.size(); ++i) {
            const Rect r = rowRect(i);
            if (i == armed_ || i == hover_) fill(c, r, t.hover, 3.0f);
            const float ty = (float)(r.y + (r.h - (int)lineHeight()) / 2);
            drawText(c, (float)(r.x + 8), ty, t.text, shortcuts::actionName(groupAct(i)));
            const std::string combo = armed_ == i ? std::string("…") : groupCombo(i);
            drawText(c, (float)(r.right() - 8 - (int)textWidth(combo)), ty,
                     armed_ == i ? t.accent : t.text, combo);
        }

        for (int i = 0; i < 3; ++i) {
            const Rect b = buttonRect(i);
            const bool hot = hoverBtn_ == i;
            fill(c, b, hot ? t.pressed : t.panelHi, 3.0f);
            stroke(c, b, t.border, 1.0f, 3.0f);
            const char* label = i == 0 ? "应用" : (i == 1 ? "恢复" : "取消");
            drawText(c, (float)(b.x + (b.w - (int)textWidth(label)) / 2),
                     (float)(b.y + (b.h - (int)lineHeight()) / 2), t.text, label);
        }
    }

    bool onPointer(const PointerEvent& e) override
    {
        if (e.pressed && !frame_.contains((int)e.pos.x, (int)e.pos.y)) {
            close();
            return true;
        }
        int idx = -1;
        for (int i = 0; i < (int)groups_.size(); ++i)
            if (rowRect(i).contains((int)e.pos.x, (int)e.pos.y)) idx = i;
        int btn = -1;
        for (int i = 0; i < 3; ++i)
            if (buttonRect(i).contains((int)e.pos.x, (int)e.pos.y)) btn = i;

        if (e.pressed) {
            pressedBtn_ = btn;
            armed_ = btn < 0 ? idx : -1;
            markDirty(frame_);
            return true;
        }
        if (e.released) {
            if (pressedBtn_ >= 0 && pressedBtn_ == btn) {
                if (btn == 0) applyDraft();
                else if (btn == 1) draft_ = shortcuts::defaults();
                else close();
            }
            pressedBtn_ = -1;
            markDirty(frame_);
            return true;
        }
        if (!e.down && (idx != hover_ || btn != hoverBtn_)) {
            hover_ = idx;
            hoverBtn_ = btn;
            markDirty(frame_);
        }
        return true;
    }

    bool onKey(const KeyEvent& e) override
    {
        if (!e.pressed) return true;
        if (armed_ >= 0) {
            if (e.scancode == KeyEsc) {
                armed_ = -1;
                markDirty(frame_);
                return true;
            }
            if (isModifier(e.scancode)) return true;
            if (!shortcuts::keyHasName(e.scancode)) return true;
            const std::vector<int>& slots = groups_[(size_t)armed_];
            for (size_t k = 0; k < slots.size(); ++k)
                shortcuts::bindRowIn(draft_, slots[k], k == 0 ? e.scancode : 0,
                                     k == 0 ? keymap::modsOf(e) : 0u);
            armed_ = -1;
            markDirty(frame_);
            return true;
        }
        if (e.scancode == KeyEsc) close();
        return true;
    }

private:
    static bool isModifier(int sc) { return sc >= 114 && sc <= 126; }

    void buildGroups()
    {
        groups_.clear();
        for (int i = 0; i < (int)draft_.size(); ++i) {
            const KeyAct a = draft_[(size_t)i].act;
            bool found = false;
            for (size_t g = 0; g < groups_.size() && !found; ++g)
                if (groupAct((int)g) == a) {
                    groups_[g].push_back(i);
                    found = true;
                }
            if (!found) groups_.push_back(std::vector<int>{i});
        }
    }

    KeyAct groupAct(int g) const { return draft_[(size_t)groups_[(size_t)g][0]].act; }

    std::string groupCombo(int g) const
    {
        std::string out;
        for (int r : groups_[(size_t)g]) {
            const std::string c = shortcuts::rowCombo(draft_[(size_t)r]);
            if (c.empty()) continue;
            if (!out.empty()) out += " / ";
            out += c;
        }
        return out;
    }

    void applyDraft()
    {
        shortcuts::applyRows(draft_);
        host_.persistSettings();
        markDirty(frame_);
    }

    Rect panel() const
    {
        const int w = 560;
        const int h = 44 + rowsHeight() + 44;
        return Rect{frame_.x + (frame_.w - w) / 2, frame_.y + (frame_.h - h) / 2, w, h};
    }

    int rowsHeight() const
    {
        const int n = (int)groups_.size();
        return ((n + 1) / 2) * 24 + 8;
    }

    int perCol() const { return ((int)groups_.size() + 1) / 2; }

    Rect rowRect(int i) const
    {
        const Rect p = panel();
        const int colW = (p.w - 24 - 12) / 2;
        const int col = i / perCol();
        const int row = i % perCol();
        return Rect{p.x + 12 + col * (colW + 12), p.y + 48 + row * 24, colW, 24};
    }

    Rect buttonRect(int i) const
    {
        const Rect p = panel();
        const int bw = 72, bh = 26, gap = 8;
        const int x = p.right() - 16 - (3 - i) * (bw + gap) + gap;
        return Rect{x, p.bottom() - 12 - bh, bw, bh};
    }

    void close()
    {
        Host* hp = &host_;
        host_.post([hp]() { hp->closePopup(); });
    }

    Host& host_;
    std::vector<shortcuts::Row> draft_;
    std::vector<std::vector<int>> groups_;
    int armed_ = -1;
    int hover_ = -1;
    int hoverBtn_ = -1;
    int pressedBtn_ = -1;
};

}

std::unique_ptr<Node> makeNewDialog(Host& host) { return std::make_unique<NewDialog>(host); }
std::unique_ptr<Node> makeCanvasSizeDialog(Host& host) { return std::make_unique<CanvasSizeDialog>(host); }
std::unique_ptr<Node> makeImageSizeDialog(Host& host) { return std::make_unique<ImageSizeDialog>(host); }
std::unique_ptr<Node> makeThresholdDialog(Host& host) { return std::make_unique<ThresholdDialog>(host); }
std::unique_ptr<Node> makeBrightContrastDialog(Host& host) { return std::make_unique<BrightContrastDialog>(host); }
std::unique_ptr<Node> makeHueDialog(Host& host) { return std::make_unique<HueDialog>(host); }
std::unique_ptr<Node> makeSaveDialog(Host& host, int closeTabAfter, const std::string& initialPath) { return std::make_unique<SaveDialog>(host, closeTabAfter, initialPath); }
std::unique_ptr<Node> makeCloseDialog(Host& host, int tab) { return std::make_unique<CloseDialog>(host, tab); }

std::unique_ptr<Node> makeQuitDialog(Host& host) { return std::make_unique<QuitDialog>(host); }
std::unique_ptr<Node> makeAboutDialog(Host& host) { return std::make_unique<AboutDialog>(host); }

std::unique_ptr<Node> makeColorPopup(Host& host, const float* rgba,
                                     std::function<void(const float[4])> onChange)
{
    return std::make_unique<ColorPopup>(host, rgba, std::move(onChange));
}

std::unique_ptr<Node> makeShortcutsDialog(Host& host)
{
    return std::make_unique<ShortcutsDialog>(host);
}

}
}
