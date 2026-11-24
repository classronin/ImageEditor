#include "nodes/Widgets.h"


#include "core/Clipboard.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRRect.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkTypeface_win.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace ie {

namespace {

SkFont gFont;
bool gFontReady = false;

struct TextEntry {
    sk_sp<SkTextBlob> blob;
    float w = 0.0f;
};

std::unordered_map<std::string, TextEntry> gTextCache;

const TextEntry& textEntry(const std::string& s)
{
    auto it = gTextCache.find(s);
    if (it != gTextCache.end()) return it->second;
    if (gTextCache.size() > 2048) gTextCache.clear();
    TextEntry e;
    e.w = gFont.measureText(s.c_str(), s.size(), SkTextEncoding::kUTF8);
    e.blob = SkTextBlob::MakeFromString(s.c_str(), gFont, SkTextEncoding::kUTF8);
    return gTextCache.emplace(s, std::move(e)).first->second;
}

SkColor sk(Color32 c)
{
    return SkColorSetARGB((c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255);
}

SkPaint fillPaint(Color32 c)
{
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(sk(c));
    return p;
}

std::string fmtValue(const char* fmt, double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt ? fmt : "%.0f", v);
    return buf;
}

float ascender()
{
    SkFontMetrics m{};
    gFont.getMetrics(&m);
    return -m.fAscent;
}

}

namespace ui {

const Theme& theme()
{
    static Theme t;
    return t;
}

bool initFonts()
{
    if (gFontReady) return true;
    const char* candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyhbd.ttc",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/segoeui.ttf",
    };
    sk_sp<SkFontMgr> mgr = SkFontMgr_New_DirectWrite();
    sk_sp<SkTypeface> face;
    for (const char* p : candidates) {
        face = mgr ? mgr->makeFromFile(p) : nullptr;
        if (face) break;
    }
    if (!face && mgr) face = mgr->legacyMakeTypeface("", SkFontStyle());
    if (!face) return false;
    gFont = SkFont(face, (SkScalar)theme().fontPx);
    gFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
    gFont.setSubpixel(true);
    gFontReady = true;
    return true;
}

float lineHeight()
{
    return (float)theme().fontPx + 7.0f;
}

float textWidth(const std::string& s)
{
    if (s.empty() || !gFontReady) return 0.0f;
    return textEntry(s).w;
}

void drawText(SkCanvas& c, float x, float yTop, Color32 col, const std::string& s)
{
    if (s.empty() || !gFontReady) return;
    SkPaint p = fillPaint(col);
    const float baseline = yTop + ascender();
    const sk_sp<SkTextBlob>& blob = textEntry(s).blob;
    if (blob) c.drawTextBlob(blob.get(), x, baseline, p);
}

void fill(SkCanvas& c, const Rect& r, Color32 col, float radius)
{
    if (r.empty()) return;
    SkPaint p = fillPaint(col);
    const SkRect rc = SkRect::MakeLTRB((float)r.x, (float)r.y, (float)r.right(), (float)r.bottom());
    if (radius > 0.0f)
        c.drawRRect(SkRRect::MakeRectXY(rc, radius, radius), p);
    else
        c.drawRect(rc, p);
}

void stroke(SkCanvas& c, const Rect& r, Color32 col, float width, float radius)
{
    if (r.empty()) return;
    SkPaint p = fillPaint(col);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(width);
    const SkRect rc = SkRect::MakeLTRB((float)r.x, (float)r.y, (float)r.right(), (float)r.bottom());
    if (radius > 0.0f)
        c.drawRRect(SkRRect::MakeRectXY(rc, radius, radius), p);
    else
        c.drawRect(rc, p);
}

void controlLine(SkCanvas& c, const Rect& r, float radius)
{
    stroke(c, r, theme().boxLine, 1.0f, radius);
}

}

Button::Button(std::string text, std::function<void()> action) : text_(std::move(text)), action_(std::move(action))
{
}

Size Button::requestSize() const
{
    const int w = w_ > 0 ? w_ : (int)ui::textWidth(text_) + 20;
    const int h = h_ > 0 ? h_ : ui::theme().rowH;
    return Size{w, h};
}

void Button::paint(SkCanvas& c)
{
    const ui::Theme& t = ui::theme();
    Rect r = frame_;
    Color32 bg = pressed_ ? t.accentPress : (hover_ ? t.hover : t.panelHi);
    ui::fill(c, r, bg, (float)t.radius);
    ui::stroke(c, r, t.border, 1.0f, (float)t.radius);
    const int tw = (int)ui::textWidth(text_);
    ui::drawText(c, (float)(r.x + (r.w - tw) / 2), (float)(r.y + (r.h - (int)ui::lineHeight()) / 2), t.text, text_);
}

bool Button::onPointer(const PointerEvent& e)
{
    if (!frame_.contains((int)e.pos.x, (int)e.pos.y)) {
        if (hover_ || pressed_) {
            hover_ = false;
            pressed_ = false;
            markDirty(frame_);
        }
        return false;
    }
    if (e.pressed) {
        pressed_ = true;
        markDirty(frame_);
        return true;
    }
    if (e.released) {
        const bool fire = pressed_;
        pressed_ = false;
        hover_ = true;
        markDirty(frame_);
        if (fire && action_) action_();
        return true;
    }
    if (!hover_) {
        hover_ = true;
        markDirty(frame_);
    }
    return true;
}

Label::Label(std::string text, Color32 col) : text_(std::move(text)), col_(col) {}

Size Label::requestSize() const
{
    return Size{(int)ui::textWidth(text_) + 2, (int)ui::lineHeight()};
}

void Label::paint(SkCanvas& c)
{
    ui::drawText(c, (float)frame_.x, (float)frame_.y, col_, text_);
}

Checkbox::Checkbox(std::string text, const bool* value, std::function<void(bool)> onSet)
    : text_(std::move(text)), value_(value), onSet_(std::move(onSet))
{
}

Size Checkbox::requestSize() const
{
    return Size{16 + 6 + (int)ui::textWidth(text_), ui::theme().rowH};
}

void Checkbox::paint(SkCanvas& c)
{
    const ui::Theme& t = ui::theme();
    const Rect box = ui::checkBoxIn(frame_);
    ui::fill(c, box, hover_ ? t.hover : t.panelHi, 3.0f);
    ui::controlLine(c, box, 3.0f);
    if (value_ && *value_) {
        const Rect in{box.x + 3, box.y + 3, box.w - 6, box.h - 6};
        ui::fill(c, in, t.accent, 2.0f);
    }
    ui::drawText(c, (float)(box.right() + 6), (float)(frame_.y + (frame_.h - (int)ui::lineHeight()) / 2),
                 hover_ ? t.text : t.text, text_);
}

bool Checkbox::onPointer(const PointerEvent& e)
{
    if (!frame_.contains((int)e.pos.x, (int)e.pos.y)) {
        if (hover_) {
            hover_ = false;
            markDirty(frame_);
        }
        return false;
    }
    if (e.pressed && value_) {
        if (onSet_) onSet_(!*value_);
        markDirty(frame_);
        return true;
    }
    if (!hover_) {
        hover_ = true;
        markDirty(frame_);
    }
    return true;
}

namespace {
int decimalsFor(double step)
{
    if (step >= 1.0) return 0;
    if (step >= 0.1) return 1;
    if (step >= 0.01) return 2;
    return 3;
}

bool parseNumText(const std::string& s, double& out)
{
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || (end && *end != '\0')) return false;
    out = v;
    return true;
}
}

std::string NumField::text(bool editingRow) const
{
    if (editingRow && editing) return buf;
    char tmp[64];
    const int dec = fmt ? 0 : decimalsFor(step);
    if (fmt)
        std::snprintf(tmp, sizeof(tmp), fmt, value());
    else
        std::snprintf(tmp, sizeof(tmp), "%.*f", dec, value());
    return tmp;
}

void NumField::setValue(double v) const
{
    if (!set) return;
    if (step > 0) v = std::round(v / step) * step;
    v = std::clamp(v, lo, hi);
    set(writeUd, v);
}

void NumField::beginEdit()
{
    editing = true;
    char tmp[64];
    std::snprintf(tmp, sizeof(tmp), "%.*f", decimalsFor(step), value());
    buf = tmp;
    sel.reset();
    sel.selectAll((int)buf.size());
}

void NumField::commit()
{
    double v = 0;
    if (parseNumText(buf, v)) setValue(v);
    editing = false;
    buf.clear();
    sel.reset();
}

void NumField::cancelEdit()
{
    editing = false;
    buf.clear();
    sel.reset();
}

void NumField::eraseSel() { ui::eraseSel(sel, buf); }

std::string NumField::clipText() const { return ui::clipText(sel, buf); }

bool NumField::pasteText(const std::string& utf8)
{
    std::string num;
    if (!ui::numberFromText(utf8, num)) return false;
    ui::eraseSel(sel, buf);
    ui::insertAt(sel, buf, num);
    sel.anchor = -1;
    return true;
}

int NumField::caretAtX(float x, const Rect& r) const
{
    const float tx = (float)(r.x + 6);
    int best = 0;
    float bestD = std::abs(x - tx);
    const int n = (int)buf.size();
    for (int i = 1; i <= n; ++i) {
        const float px = tx + ui::textWidth(buf.substr(0, (size_t)i));
        const float d = std::abs(x - px);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void NumField::paint(SkCanvas& c, const Rect& r, bool editingRow) const
{
    const ui::Theme& t = ui::theme();
    ui::fill(c, r, t.surface, 2.0f);
    ui::stroke(c, r, (editingRow && editing) ? t.accent : t.border, 1.0f, 2.0f);
    if (slide && !(editingRow && editing)) {
        const double span = hi - lo;
        double f = span > 0 ? (value() - lo) / span : 0.0;
        f = std::clamp(f, 0.0, 1.0);
        const int w = (int)std::lround((double)(r.w - 2) * f);
        if (w > 0) ui::fill(c, Rect{r.x + 1, r.y + 1, w, r.h - 2}, t.accent, 1.0f);
    }
    const bool editNow = editingRow && editing;
    const std::string txt = text(editNow);
    const float ty = (float)(r.y + (r.h - (int)ui::lineHeight()) / 2);
    if (editNow) {
        const float tx = (float)(r.x + 6);
        if (hasSel()) {
            const int a = selBegin(), b = selEnd();
            const float x0 = tx + ui::textWidth(txt.substr(0, (size_t)a));
            const float x1 = tx + ui::textWidth(txt.substr(0, (size_t)b));
            ui::fill(c, Rect{(int)x0, r.y + 3, (int)std::max(1.0f, x1 - x0), r.h - 6}, t.accent, 1.0f);
        }
        ui::drawText(c, tx, ty, t.text, txt);
        if (!hasSel()) {
            const float cx = tx + ui::textWidth(txt.substr(0, (size_t)std::max(0, sel.caret)));
            ui::fill(c, Rect{(int)cx, r.y + 3, 1, r.h - 6}, t.accent, 0.0f);
        }
    } else {
        const int tw = (int)ui::textWidth(txt);
        ui::drawText(c, (float)(r.x + (r.w - tw) / 2), ty, t.text, txt);
    }
}

bool NumField::pointer(const PointerEvent& e, const Rect& r)
{
    if (editing) {
        if (e.pressed) {
            if (r.contains((int)e.pos.x, (int)e.pos.y)) {
                sel.caret = caretAtX(e.pos.x, r);
                sel.anchor = sel.caret;
                sel.dragging = true;
                return true;
            }
        } else if (e.released && sel.dragging) {
            sel.dragging = false;
            if (sel.anchor == sel.caret) sel.anchor = -1;
            return true;
        } else if (e.down && sel.dragging) {
            sel.caret = caretAtX(e.pos.x, r);
            return true;
        }
    }

    if (drag && e.pressed && !r.contains((int)e.pos.x, (int)e.pos.y)) {
        drag = false;
        dragged = false;
        return false;
    }

    if (drag && (e.down || e.released)) {
        if (e.released) {
            const bool wasClick = !dragged;
            drag = false;
            dragged = false;
            if (wasClick) {
                const float f = std::clamp((pressX - (float)r.x) / (float)std::max(1, r.w), 0.0f, 1.0f);
                setValue(lo + (double)f * (hi - lo));
            }
            return true;
        }
        const float dx = e.pos.x - pressX;
        if (!dragged && std::abs(dx) + std::abs(e.pos.y - pressY) > 3.0f) dragged = true;
        if (dragged) {
            const float f = std::clamp((e.pos.x - (float)r.x) / (float)std::max(1, r.w), 0.0f, 1.0f);
            setValue(lo + (double)f * (hi - lo));
        }
        return true;
    }

    const bool inside = r.contains((int)e.pos.x, (int)e.pos.y);
    if (!inside) {
        if (e.pressed && editing) {
            cancelEdit();
            return false;
        }
        return false;
    }
    if (e.pressed) {
        if (editing) {
            sel.caret = (int)buf.size();
            return true;
        }
        if (!slide || e.doubleClick) {
            beginEdit();
            return true;
        }
        drag = true;
        dragged = false;
        pressX = e.pos.x;
        pressY = e.pos.y;
        return true;
    }
    return false;
}

bool NumField::key(const KeyEvent& e)
{
    if (!editing) return false;
    if (!e.pressed) return true;
    if (ui::textSelKey(e, sel, buf, ui::ByteStep{}, ui::PasteRule::Number)) return true;
    switch (e.scancode) {
        case KeyEnter: commit(); return true;
        case KeyEsc: cancelEdit(); return true;
        default: break;
    }
    const int u = e.unicode;
    if ((u >= '0' && u <= '9') || u == '.' || u == '-' || u == '+' || u == ',') {
        ui::textSelInsert(sel, buf, std::string(1, (char)(u == ',' ? '.' : u)));
        return true;
    }
    return true;
}

Slider::Slider(std::string label, const float* value, float lo, float hi, const char* fmt,
               std::function<void(float)> onSet)
    : label_(std::move(label)), value_(value), lo_(lo), hi_(hi), fmt_(fmt ? fmt : "%.0f"), onSet_(std::move(onSet))
{
}

Size Slider::requestSize() const
{
    return Size{170, ui::theme().rowH};
}

void Slider::apply(float x)
{
    const int labelW = (int)ui::textWidth(label_) + 8;
    const int barX = frame_.x + labelW;
    const int barW = std::max(1, frame_.w - labelW - 4);
    float f = (x - (float)barX) / (float)barW;
    f = std::clamp(f, 0.0f, 1.0f);
    const float nv = lo_ + f * (hi_ - lo_);
    if (onSet_) onSet_(nv);
    markDirty(frame_);
}

void Slider::paint(SkCanvas& c)
{
    const ui::Theme& t = ui::theme();
    const int labelW = (int)ui::textWidth(label_) + 8;
    ui::drawText(c, (float)frame_.x, (float)(frame_.y + (frame_.h - (int)ui::lineHeight()) / 2), t.dim, label_);
    const Rect bar{frame_.x + labelW, frame_.y + frame_.h / 2 - 2, std::max(1, frame_.w - labelW - 52), 4};
    ui::fill(c, bar, t.panelHi, 2.0f);
    float f = hi_ > lo_ && value_ ? (*value_ - lo_) / (hi_ - lo_) : 0.0f;
    f = std::clamp(f, 0.0f, 1.0f);
    Rect on = bar;
    on.w = (int)(bar.w * f);
    if (on.w > 0) ui::fill(c, on, t.accent, 2.0f);
    const std::string v = fmtValue(fmt_.c_str(), value_ ? *value_ : 0.0);
    ui::drawText(c, (float)(frame_.right() - (int)ui::textWidth(v) - 2),
                 (float)(frame_.y + (frame_.h - (int)ui::lineHeight()) / 2), t.text, v);
}

bool Slider::onPointer(const PointerEvent& e)
{
    if (!frame_.contains((int)e.pos.x, (int)e.pos.y)) return false;
    if (e.pressed) {
        dragging_ = true;
        apply(e.pos.x);
        return true;
    }
    if (e.released) {
        if (dragging_) apply(e.pos.x);
        dragging_ = false;
        return true;
    }
    if (dragging_ && e.down) {
        apply(e.pos.x);
        return true;
    }
    return true;
}

bool Slider::onWheel(const WheelEvent& e)
{
    if (!value_ || !frame_.contains((int)e.pos.x, (int)e.pos.y)) return false;
    float nv = *value_ + e.dy * (hi_ - lo_) / 20.0f;
    nv = std::clamp(nv, lo_, hi_);
    if (onSet_) onSet_(nv);
    markDirty(frame_);
    return true;
}

SliderI::SliderI(std::string label, const int* value, int lo, int hi, std::function<void(int)> onSet)
    : label_(std::move(label)), value_(value), lo_(lo), hi_(hi), onSet_(std::move(onSet))
{
}

Size SliderI::requestSize() const
{
    return Size{170, ui::theme().rowH};
}

void SliderI::apply(float x)
{
    const int labelW = (int)ui::textWidth(label_) + 8;
    const int barX = frame_.x + labelW;
    const int barW = std::max(1, frame_.w - labelW - 4);
    float f = std::clamp((x - (float)barX) / (float)barW, 0.0f, 1.0f);
    const int nv = (int)(lo_ + f * (hi_ - lo_) + 0.5f);
    if (onSet_) onSet_(nv);
    markDirty(frame_);
}

void SliderI::paint(SkCanvas& c)
{
    const ui::Theme& t = ui::theme();
    const int labelW = (int)ui::textWidth(label_) + 8;
    ui::drawText(c, (float)frame_.x, (float)(frame_.y + (frame_.h - (int)ui::lineHeight()) / 2), t.dim, label_);
    const Rect bar{frame_.x + labelW, frame_.y + frame_.h / 2 - 2, std::max(1, frame_.w - labelW - 52), 4};
    ui::fill(c, bar, t.panelHi, 2.0f);
    float f = hi_ > lo_ && value_ ? (float)(*value_ - lo_) / (float)(hi_ - lo_) : 0.0f;
    f = std::clamp(f, 0.0f, 1.0f);
    Rect on = bar;
    on.w = (int)(bar.w * f);
    if (on.w > 0) ui::fill(c, on, t.accent, 2.0f);
    const std::string v = std::to_string(value_ ? *value_ : 0);
    ui::drawText(c, (float)(frame_.right() - (int)ui::textWidth(v) - 2),
                 (float)(frame_.y + (frame_.h - (int)ui::lineHeight()) / 2), t.text, v);
}

bool SliderI::onPointer(const PointerEvent& e)
{
    if (!frame_.contains((int)e.pos.x, (int)e.pos.y)) return false;
    if (e.pressed) {
        dragging_ = true;
        apply(e.pos.x);
        return true;
    }
    if (e.released) {
        if (dragging_) apply(e.pos.x);
        dragging_ = false;
        return true;
    }
    if (dragging_ && e.down) {
        apply(e.pos.x);
        return true;
    }
    return true;
}

bool SliderI::onWheel(const WheelEvent& e)
{
    if (!value_ || !frame_.contains((int)e.pos.x, (int)e.pos.y)) return false;
    const int nv = std::clamp(*value_ + (int)(e.dy > 0 ? 1 : -1), lo_, hi_);
    if (onSet_) onSet_(nv);
    markDirty(frame_);
    return true;
}

Combo::Combo(std::string label, const int* index, std::vector<std::string> items, std::function<void()> onOpen,
             std::function<void(int)> onChange)
    : label_(std::move(label))
    , index_(index)
    , items_(std::move(items))
    , onOpen_(std::move(onOpen))
    , onChange_(std::move(onChange))
{
}

Size Combo::requestSize() const
{
    return Size{170, ui::theme().rowH};
}

void Combo::paint(SkCanvas& c)
{
    const ui::Theme& t = ui::theme();
    const int labelW = (int)ui::textWidth(label_) + 8;
    ui::drawText(c, (float)frame_.x, (float)(frame_.y + (frame_.h - (int)ui::lineHeight()) / 2), t.dim, label_);
    Rect box{frame_.x + labelW, frame_.y, std::max(1, frame_.w - labelW), frame_.h};
    ui::fill(c, box, hover_ ? t.hover : t.panelHi, (float)t.radius);
    ui::stroke(c, box, t.border, 1.0f, (float)t.radius);
    const int i = index_ ? *index_ : -1;
    const std::string cur = (i >= 0 && i < (int)items_.size()) ? items_[(size_t)i] : std::string();
    ui::drawText(c, (float)(box.x + 6), (float)(box.y + (box.h - (int)ui::lineHeight()) / 2), t.text, cur);
    ui::drawText(c, (float)(box.right() - 10), (float)(box.y + (box.h - (int)ui::lineHeight()) / 2), t.dim, "v");
}

bool Combo::onPointer(const PointerEvent& e)
{
    if (!frame_.contains((int)e.pos.x, (int)e.pos.y)) {
        if (hover_) {
            hover_ = false;
            markDirty(frame_);
        }
        return false;
    }
    if (!hover_) {
        hover_ = true;
        markDirty(frame_);
    }
    if (e.pressed && onOpen_) onOpen_();
    if (e.pressed && onChange_ && index_) onChange_(*index_);
    return true;
}

namespace {
TextField* gFocusField = nullptr;
}

TextField::TextField(std::string* text, std::function<void()> submit, bool password)
    : text_(text), submit_(std::move(submit)), password_(password)
{
}

Size TextField::requestSize() const
{
    return Size{w_, ui::theme().rowH};
}

void TextField::setFocusHost(TextField* f)
{
    if (gFocusField && gFocusField != f) gFocusField->setFocus(false);
    gFocusField = f;
}

void TextField::setFocus(bool on)
{
    if (focused_ == on) return;
    focused_ = on;
    if (on) {
        sel.reset();
        sel.caret = text_ ? (int)text_->size() : 0;
    }
    if (on)
        setFocusHost(this);
    else if (gFocusField == this)
        gFocusField = nullptr;
    markDirty(frame_);
}

void TextField::paint(SkCanvas& c)
{
    const ui::Theme& t = ui::theme();
    ui::fill(c, frame_, t.panelHi, (float)t.radius);
    ui::stroke(c, frame_, focused_ ? t.accent : t.border, 1.0f, (float)t.radius);
    std::string shown = text_ ? *text_ : std::string();
    if (password_) shown.assign(shown.size(), '*');
    const int tx = frame_.x + 6;
    const int ty = frame_.y + (frame_.h - (int)ui::lineHeight()) / 2;
    ui::drawText(c, (float)tx, (float)ty, t.text, shown);
    if (focused_) {
        if (sel.hasSel()) {
            const int lo = sel.lo(), hi = sel.hi();
            const float x0 = (float)tx + ui::textWidth(shown.substr(0, (size_t)lo));
            const float x1 = (float)tx + ui::textWidth(shown.substr(0, (size_t)hi));
            ui::fill(c, Rect{(int)x0, frame_.y + 4, std::max(2, (int)(x1 - x0)), frame_.h - 8}, t.accent);
        }
        const float cx =
            (float)tx + ui::textWidth(shown.substr(0, (size_t)std::clamp(sel.caret, 0, (int)shown.size())));
        SkPaint p;
        p.setColor(sk(t.accent));
        c.drawRect(SkRect::MakeLTRB(cx, (float)(frame_.y + 4), cx + 1.0f, (float)(frame_.bottom() - 4)), p);
    }
}

bool TextField::onPointer(const PointerEvent& e)
{
    if (!frame_.contains((int)e.pos.x, (int)e.pos.y)) return false;
    if (e.pressed) setFocus(true);
    return true;
}

bool TextField::onKey(const KeyEvent& e)
{
    if (!focused_ || !text_) return false;
    if (!e.pressed) return false;

    if (ui::textSelKey(e, sel, *text_, ui::Utf8Step{}, ui::PasteRule::Text, true)) {
        sel.clamp((int)text_->size());
        markDirty(frame_);
        return true;
    }
    if (e.scancode == KeyEnter) {
        if (submit_) submit_();
        return true;
    }
    if (e.scancode == KeyEsc) {
        setFocus(false);
        return true;
    }
    if (e.unicode >= 32) {
        ui::textSelInsert(sel, *text_, utf8::encode(e.unicode));
        markDirty(frame_);
        return true;
    }
    return false;
}

ColorSwatch::ColorSwatch(float* rgba, std::function<void()> onOpen) : rgba_(rgba), onOpen_(std::move(onOpen)) {}

Size ColorSwatch::requestSize() const
{
    return Size{44, ui::theme().rowH};
}

void ColorSwatch::paint(SkCanvas& c)
{
    const ui::Theme& t = ui::theme();
    ui::fill(c, frame_, t.bg, 3.0f);
    if (rgba_) {
        Color32 col = rgba((int)(rgba_[0] * 255), (int)(rgba_[1] * 255), (int)(rgba_[2] * 255), 255);
        Rect in = frame_;
        in.x += 2;
        in.y += 2;
        in.w -= 4;
        in.h -= 4;
        ui::fill(c, in, col, 2.0f);
    }
    ui::stroke(c, frame_, t.border, 1.0f, 3.0f);
}

bool ColorSwatch::onPointer(const PointerEvent& e)
{
    if (!frame_.contains((int)e.pos.x, (int)e.pos.y)) return false;
    if (e.pressed && onOpen_) onOpen_();
    return true;
}

Row::Row(int gap, int pad) : gap_(gap), pad_(pad)
{
}

void Row::arrange()
{
    int x = frame_.x + pad_;
    const int h = frame_.h - pad_ * 2;
    for (auto& c : children_) {
        const Size s = c->requestSize();
        int w = s.w;
        if (w <= 0) w = std::max(0, frame_.right() - pad_ - x);
        placeChildAbs(c.get(), Rect{x, frame_.y + pad_, w, h > 0 ? h : s.h});
        x += w + gap_;
    }
}

Column::Column(int gap, int pad) : gap_(gap), pad_(pad)
{
}

void Column::arrange()
{
    int y = frame_.y + pad_;
    const int w = frame_.w - pad_ * 2;
    for (auto& c : children_) {
        const Size s = c->requestSize();
        int h = s.h;
        if (h <= 0) h = std::max(0, frame_.bottom() - pad_ - y);
        placeChildAbs(c.get(), Rect{frame_.x + pad_, y, w > 0 ? w : s.w, h});
        y += h + gap_;
    }
}

}
