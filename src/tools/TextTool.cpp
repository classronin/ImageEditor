#include "core/TextRender.h"
#include "core/Clipboard.h"
#include "tools/Tools.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ie {

namespace {

constexpr float kBlinkPeriod = 1.06f;

class TextTool final : public Tool {
public:
    ToolId id() const override { return ToolId::Text; }
    const char* name() const override { return "文字"; }

    CursorShape cursor(const InputFrame&) const override
    {
        return (editing_ && (moveBox_ || hoverFrame_)) ? CursorShape::Move : CursorShape::IBeam;
    }

    void onEnter(Editor& e) override
    {
        for (int i = 0; i < 4; ++i) {
            tint_[i] = e.palette.fg[i];
            lastFg_[i] = e.palette.fg[i];
        }
    }

    void syncTint(Editor& e)
    {
        if (std::memcmp(lastFg_, e.palette.fg, sizeof(lastFg_)) == 0) return;
        for (int i = 0; i < 4; ++i) {
            tint_[i] = e.palette.fg[i];
            lastFg_[i] = e.palette.fg[i];
        }
        const int idx = targetTextLayer(e);
        if (idx >= 0) {
            const TextData& s = e.doc.layers[(size_t)idx].text;
            float col[4] = {tint_[0], tint_[1], tint_[2], tint_[3]};
            e.applyTextStyle(idx, s.size, s.lineSpacing, s.opacity, col);
        }
    }

    bool acceptsButton(int b) const override { return b == 0 || (b == 1 && editing_); }

    void onPress(Editor& e, const InputFrame& in, int btn) override
    {
        if (btn == 1) {
            if (editing_) cancel(e);
            return;
        }
        if (btn != 0 || !e.canvasHas(in.mouse.x, in.mouse.y)) return;
        if (editing_) {
            const TextData* t = editingText(e);
            const Vec2f at = e.canvasImage(in.mouse.x, in.mouse.y);
            const Vec2f local = toLocal(at);
            if (t && onBoxFrame(*t, local)) {
                const int idx = targetTextLayer(e);
                if (idx >= 0) {
                    moveBox_ = true;
                    movePressLocal_ = local;
                    moveX0_ = e.doc.layers[(size_t)idx].text.x;
                    moveY0_ = e.doc.layers[(size_t)idx].text.y;
                    dragSelect_ = false;
                    anchor_ = -1;
                    blink_ = 0.0f;
                    return;
                }
            }
            if (t && insideTextBox(*t, local)) {
                caret_ = textOffsetAt(*t, local.x, local.y);
                anchor_ = caret_;
                dragSelect_ = true;
                blink_ = 0.0f;
                return;
            }
            commit(e);
            return;
        }
        const Vec2f at = e.canvasImage(in.mouse.x, in.mouse.y);
        if (beginEditAt(e, at)) return;
        if (!textFontReady()) {
            return;
        }
        for (int i = 0; i < 4; ++i) {
            tint_[i] = e.palette.fg[i];
            lastFg_[i] = e.palette.fg[i];
        }

        TextData t;
        t.x = at.x;
        t.y = at.y;
        t.size = size_;
        t.lineSpacing = lineSpacing_;
        t.opacity = opacity_;
        t.color = tintColor();
        t.font = font_;
        const int idx = e.beginNewTextSession(t);
        if (idx < 0) return;
        editLayerId_ = e.doc.layers[(size_t)idx].id;
        text_[0] = 0;
        caret_ = 0;
        anchor_ = -1;
        blink_ = 0.0f;
        editing_ = true;
    }

    bool beginEditAt(Editor& e, const Vec2f& at) override
    {
        const int hit = hitTextLayer(e, at);
        if (hit < 0) return false;
        if (editing_) commit(e);
        const Layer& probe = e.doc.layers[(size_t)hit];
        editXf_ = probe.textXf;
        e.doc.active = hit;
        e.doc.selIds.clear();
        e.doc.touchStructure();
        if (!e.beginTextSession(hit)) {
            editXf_ = SkMatrix::I();
            return false;
        }
        const Layer& l = e.doc.layers[(size_t)hit];
        editLayerId_ = l.id;
        setText(l.text.text);
        font_ = l.text.font;
        const Vec2f local = toLocal(at);
        caret_ = textOffsetAt(l.text, local.x, local.y);
        anchor_ = -1;
        blink_ = 0.0f;
        editing_ = true;
        return true;
    }

    void onDrag(Editor& e, const InputFrame& in, int btn) override
    {
        if (!editing_) return;
        if (moveBox_) {
            if (btn != 0) return;
            const int idx = targetTextLayer(e);
            if (idx < 0) return;
            const Vec2f local = toLocal(e.canvasImage(in.mouse.x, in.mouse.y));
            TextData& td = e.doc.layers[(size_t)idx].text;
            td.x = moveX0_ + (local.x - movePressLocal_.x);
            td.y = moveY0_ + (local.y - movePressLocal_.y);
            const Rect before = e.doc.layerVisualBounds(idx);
            e.doc.refreshTextLayer(idx);
            e.doc.markDirty(before.unite(e.doc.layerVisualBounds(idx)));
            e.modified = true;
            blink_ = 0.0f;
            return;
        }
        if (!dragSelect_ || btn != 0) return;
        const TextData* t = editingText(e);
        if (!t) return;
        const Vec2f at = toLocal(e.canvasImage(in.mouse.x, in.mouse.y));
        caret_ = textOffsetAt(*t, at.x, at.y);
        blink_ = 0.0f;
    }

    void onRelease(Editor& e, const InputFrame& in, int btn) override
    {
        (void)e;
        (void)in;
        if (btn != 0) return;
        moveBox_ = false;
        dragSelect_ = false;
        if (anchor_ == caret_) anchor_ = -1;
    }

    void onLeave(Editor& e) override
    {
        if (editing_) commit(e);
    }

    void update(Editor& e, const InputFrame& in) override
    {
        syncTint(e);
        hoverFrame_ = false;
        if (editing_) {
            if (const TextData* t = editingText(e)) {
                const Vec2f local = toLocal(e.canvasImage(in.mouse.x, in.mouse.y));
                hoverFrame_ = onBoxFrame(*t, local);
            }
        }
        if (!editing_) {
            e.takeTextCommit();
            e.takeTextCancel();
            return;
        }
        if (!e.textSessionActive()) {
            resetSession();
            e.takeTextCommit();
            e.takeTextCancel();
            return;
        }
        if (e.takeTextCancel()) {
            cancel(e);
            return;
        }
        blink_ += in.dt;
        if (blink_ >= kBlinkPeriod * 4.0f) blink_ = std::fmod(blink_, kBlinkPeriod);
        syncBuffer();
        e.updateTextEditing(text_);
        if (e.takeTextCommit()) commit(e);
    }

    bool animating() const override { return editing_; }

    bool canvasTextActive() const override { return editing_; }
    char* canvasTextBuffer() override { return text_; }
    int canvasTextCapacity() const override { return (int)sizeof(text_); }

    bool canvasTextKey(Editor& e, const KeyEvent& ev) override
    {
        if (!editing_) return false;
        const int n = (int)std::strlen(text_);
        const bool hasSel = hasSelection();

        if (ev.ctrl && !ev.alt && ev.scancode == KeyA) {
            anchor_ = 0;
            caret_ = n;
            blink_ = 0.0f;
            return true;
        }
        if (ev.ctrl && !ev.alt && ev.scancode == KeyZ) {
            commit(e);
            e.undo();
            return true;
        }
        if (ev.ctrl && !ev.alt && (ev.scancode == KeyC || ev.scancode == KeyX)) {
            if (hasSel) {
                const int a = selA(), b = selB();
                clipboard::setText(std::string(text_ + a, (size_t)(b - a)));
                if (ev.scancode == KeyX) eraseSelection();
            }
            return true;
        }
        if (ev.ctrl && !ev.alt && ev.scancode == KeyV) {
            std::string ins;
            if (clipboard::getText(ins) && !ins.empty()) {
                for (char& ch : ins)
                    if (ch == '\t') ch = ' ';
                replaceSelectionWith(ins.data(), (int)ins.size());
            }
            return true;
        }

        if (ev.scancode == KeyEsc) {
            e.requestTextCancel();
            return true;
        }
        if (ev.scancode == KeyEnter) {
            if (ev.ctrl) {
                e.requestTextCommit();
                return true;
            }
            replaceSelectionWith("\n", 1);
            return true;
        }
        if (ev.scancode == KeyBackspace) {
            if (hasSel) {
                eraseSelection();
            } else if (caret_ > 0) {
                const int from = prevCodepoint(caret_);
                eraseRange(from, caret_);
                caret_ = from;
            }
            anchor_ = -1;
            blink_ = 0.0f;
            return true;
        }
        if (ev.scancode == KeyDelete) {
            if (hasSel) {
                eraseSelection();
            } else if (caret_ < n) {
                const int to = nextCodepoint(caret_);
                eraseRange(caret_, to);
            }
            anchor_ = -1;
            blink_ = 0.0f;
            return true;
        }
        if (ev.scancode == KeyLeft || ev.scancode == KeyRight || ev.scancode == KeyHome ||
            ev.scancode == KeyEnd || ev.scancode == KeyUp || ev.scancode == KeyDown) {
            moveCaret(e, ev, n, hasSel);
            return true;
        }
        if (ev.unicode >= 32 && ev.unicode <= 0x10FFFF &&
            (ev.unicode < 0xD800 || ev.unicode > 0xDFFF)) {
            char tmp[8];
            const int k = utf8FromCodepoint((unsigned)ev.unicode, tmp);
            replaceSelectionWith(tmp, k);
            return true;
        }
        if (ev.scancode == KeySpace) {
            replaceSelectionWith(" ", 1);
            return true;
        }
        return false;
    }

    void props(Props& p, Editor& e) override
    {
        ed_ = &e;
        p.header("文字");

        buildFontList(e);
        if (!fontNames_.empty()) {
            fontShown_ = std::clamp(fontShown_, 0, (int)fontNames_.size() - 1);
            p.combo("字体", &fontShown_, fontNames_.data(), (int)fontNames_.size(), &TextTool::sFont, this);
        }

        const int idx = targetTextLayer(e);
        if (idx >= 0) {
            const TextData& s = e.doc.layers[(size_t)idx].text;
            szStage_ = s.size;
            lnStage_ = s.lineSpacing;
            opStage_ = s.opacity;
            tint_[0] = s.color.r / 255.0f;
            tint_[1] = s.color.g / 255.0f;
            tint_[2] = s.color.b / 255.0f;
            tint_[3] = s.color.a / 255.0f;
            fontShown_ = std::max(0, textFontIndex(s.font));
            p.sliderI("字号", &szStage_, 6, 400, &TextTool::sSizeV, this);
            p.sliderF("行距", &lnStage_, -20.0f, 80.0f, &TextTool::sLineV, this, "%.0f px");
            p.sliderF("不透明度", &opStage_, 0.0f, 1.0f, &TextTool::sOpacityV, this, "%.2f");
        } else {
            fontShown_ = std::max(0, textFontIndex(font_));
            p.sliderI("字号", &size_, 6, 400, &TextTool::sSizeV, this);
            p.sliderF("行距", &lineSpacing_, -20.0f, 80.0f, &TextTool::sLineV, this, "%.0f px");
            p.sliderF("不透明度", &opacity_, 0.0f, 1.0f, &TextTool::sOpacityV, this, "%.2f");
        }

        if (!textFontReady()) {
            p.separator();
            p.label("未找到系统中文字体，无法绘制。");
        }
    }

    void overlay(Overlay* dl, const Editor& e, const CanvasViewState& v, const InputFrame&) override
    {
        if (!editing_ || !dl) return;
        const TextData* t = editingText(e);
        if (!t) return;
        const TextMetrics m = measureText(*t);
        if (!m.ok) return;

        auto sp = [&](float lx, float ly) {
            const Vec2f d = toDoc(Vec2f{lx, ly});
            return v.toScreen(d.x, d.y, e.doc.width, e.doc.height);
        };
        auto quad = [&](float x0, float y0, float x1, float y1, Vec2f* out) {
            out[0] = sp(x0, y0);
            out[1] = sp(x1, y0);
            out[2] = sp(x1, y1);
            out[3] = sp(x0, y1);
        };

        {
            const Rect box = textBoxRect(*t);
            if (!box.empty()) {
                Vec2f q[4];
                quad((float)box.x, (float)box.y, (float)(box.x + box.w), (float)(box.y + box.h), q);
                dl->polyline(q, 4, rgba(70, 140, 240, 170), 1.0f, true);
            }
        }

        if (hasSelection()) {
            const int a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
            Rect boxes[64];
            const int nb = textSelectionRects(*t, a, b, boxes, 64);
            for (int i = 0; i < nb; ++i) {
                const Rect& r = boxes[i];
                Vec2f q[4];
                quad((float)r.x, (float)r.y, (float)(r.x + r.w), (float)(r.y + r.h), q);
                dl->polygonFilled(q, 4, rgba(60, 130, 240, 90));
            }
        }

        const bool visible = dragSelect_ || std::fmod(blink_, kBlinkPeriod) < kBlinkPeriod * 0.5f;
        if (!visible) return;
        const TextCaret c = textCaretAt(*t, caret_);
        const Vec2f o = sp(c.x, c.y);
        const Vec2f o2 = sp(c.x, c.y + c.h);
        dl->line(o.x, o.y, o2.x, o2.y, rgba(20, 20, 20, 255), 2.0f);
    }

    bool caretImageRect(Editor& e, Rect& out) override
    {
        if (!editing_) return false;
        const TextData* t = editingText(e);
        if (!t) return false;
        const TextCaret c = textCaretAt(*t, caret_);
        SkPoint q[2];
        q[0] = editXf_.mapXY(c.x, c.y);
        q[1] = editXf_.mapXY(c.x, c.y + c.h);
        const float x0 = std::min(q[0].x(), q[1].x());
        const float y0 = std::min(q[0].y(), q[1].y());
        const float x1 = std::max(q[0].x(), q[1].x());
        const float y1 = std::max(q[0].y(), q[1].y());
        out = Rect{(int)std::floor((double)x0), (int)std::floor((double)y0),
                   std::max(1, (int)std::ceil((double)(x1 - x0))), std::max(1, (int)std::ceil((double)(y1 - y0)))};
        return true;
    }

private:
    int len() const { return (int)std::strlen(text_); }    bool hasSelection() const { return anchor_ >= 0 && anchor_ != caret_; }
    int selA() const { return std::min(caret_, anchor_); }
    int selB() const { return std::max(caret_, anchor_); }

    static int prevCodepoint(int i, const char* s)
    {
        int p = i - 1;
        while (p > 0 && ((unsigned char)s[p] & 0xC0) == 0x80) --p;
        return p < 0 ? 0 : p;
    }
    int prevCodepoint(int i) const { return prevCodepoint(i, text_); }
    int nextCodepoint(int i) const
    {
        const int n = len();
        if (i >= n) return n;
        int p = i + 1;
        while (p < n && ((unsigned char)text_[p] & 0xC0) == 0x80) ++p;
        return p;
    }
    static int utf8FromCodepoint(unsigned cp, char* out)
    {
        if (cp < 0x80) {
            out[0] = (char)cp;
            return 1;
        }
        if (cp < 0x800) {
            out[0] = (char)(0xC0 | (cp >> 6));
            out[1] = (char)(0x80 | (cp & 0x3F));
            return 2;
        }
        if (cp < 0x10000) {
            out[0] = (char)(0xE0 | (cp >> 12));
            out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[2] = (char)(0x80 | (cp & 0x3F));
            return 3;
        }
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }

    void eraseRange(int from, int to)
    {
        const int n = len();
        from = std::clamp(from, 0, n);
        to = std::clamp(to, from, n);
        if (from == to) return;
        std::memmove(text_ + from, text_ + to, (size_t)(n - to) + 1);
    }
    void eraseSelection()
    {
        eraseRange(selA(), selB());
        caret_ = selA();
        anchor_ = -1;
    }
    void insertAt(int at, const char* s, int k)
    {
        const int n = len();
        const int cap = (int)sizeof(text_);
        if (k <= 0 || n + k >= cap) return;
        at = std::clamp(at, 0, n);
        std::memmove(text_ + at + k, text_ + at, (size_t)(n - at) + 1);
        std::memcpy(text_ + at, s, (size_t)k);
    }
    void replaceSelectionWith(const char* s, int k)
    {
        const int at = hasSelection() ? selA() : caret_;
        if (hasSelection()) eraseSelection();
        insertAt(at, s, k);
        caret_ = at + k;
        anchor_ = -1;
        blink_ = 0.0f;
    }

    void moveCaret(Editor& e, const KeyEvent& ev, int n, bool /*hasSel*/)
    {
        const TextData* t = editingText(e);
        const int before = caret_;
        int next = caret_;
        if (ev.scancode == KeyLeft) {
            next = hasSelection() && !ev.shift ? selA() : prevCodepoint(caret_);
        } else if (ev.scancode == KeyRight) {
            next = hasSelection() && !ev.shift ? selB() : nextCodepoint(caret_);
        } else if (ev.scancode == KeyHome) {
            next = 0;
            for (int i = caret_ - 1; i >= 0; --i)
                if (text_[i] == '\n') { next = i + 1; break; }
        } else if (ev.scancode == KeyEnd) {
            next = n;
            for (int i = caret_; i < n; ++i)
                if (text_[i] == '\n') { next = i; break; }
        } else if (t && (ev.scancode == KeyUp || ev.scancode == KeyDown)) {
            const TextCaret c = textCaretAt(*t, caret_);
            const TextMetrics m = measureText(*t);
            const float dy = ev.scancode == KeyUp ? -m.lineH : m.lineH;
            next = textOffsetAt(*t, c.x, c.y + dy + (ev.scancode == KeyDown ? m.lineH * 0.5f : 0.0f));
        }
        caret_ = std::clamp(next, 0, n);
        if (ev.shift) {
            if (anchor_ < 0) anchor_ = before;
        } else {
            anchor_ = -1;
        }
        blink_ = 0.0f;
    }

    void syncBuffer() { const int n = len(); caret_ = std::clamp(caret_, 0, n); }

    void setText(const std::string& s)
    {
        const size_t n = std::min(s.size(), sizeof(text_) - 1);
        std::memcpy(text_, s.data(), n);
        text_[n] = 0;
    }

    bool onBoxFrame(const TextData& t, Vec2f p) const
    {
        const Rect box = textBoxRect(t);
        if (box.empty()) return false;
        const float pad = 6.0f;
        const float ringW = std::min(pad, std::min((float)box.w * 0.25f, (float)box.h * 0.25f));
        if (p.x < (float)box.x - pad || p.x > (float)(box.x + box.w) + pad) return false;
        if (p.y < (float)box.y - pad || p.y > (float)(box.y + box.h) + pad) return false;
        const bool deepX = p.x > (float)box.x + ringW && p.x < (float)(box.x + box.w) - ringW;
        const bool deepY = p.y > (float)box.y + ringW && p.y < (float)(box.y + box.h) - ringW;
        return !(deepX && deepY);
    }

    bool insideTextBox(const TextData& t, Vec2f p) const
    {
        const float pad = 6.0f;
        const Rect box = textBoxRect(t);
        if (box.empty()) return false;
        return p.x >= (float)box.x - pad && p.x <= (float)(box.x + box.w) + pad &&
               p.y >= (float)box.y - pad && p.y <= (float)(box.y + box.h) + pad;
    }

    void resetSession()
    {
        editing_ = false;
        dragSelect_ = false;
        moveBox_ = false;
        hoverFrame_ = false;
        editLayerId_ = -1;
        editXf_ = SkMatrix::I();
        text_[0] = 0;
        caret_ = 0;
        anchor_ = -1;
        blink_ = 0.0f;
    }

    Vec2f toLocal(const Vec2f& doc) const
    {
        SkMatrix inv;
        if (editXf_.invert(&inv)) {
            const SkPoint q = inv.mapXY(doc.x, doc.y);
            return Vec2f{q.x(), q.y()};
        }
        return doc;
    }

    Vec2f toDoc(const Vec2f& local) const
    {
        const SkPoint q = editXf_.mapXY(local.x, local.y);
        return Vec2f{q.x(), q.y()};
    }

    void buildFontList(Editor&)
    {
        if (fontNames_.size() == (size_t)textFontCount()) return;
        fontNames_.clear();
        for (int i = 0; i < textFontCount(); ++i) fontNames_.push_back(textFontName(i));
    }
    void setFontIndex(int index)
    {
        const char* fam = textFontName(index);
        font_ = fam ? fam : "";
        fontShown_ = std::max(0, index);
        const int idx = ed_ ? targetTextLayer(*ed_) : -1;
        if (idx >= 0) ed_->applyTextFont(idx, font_);
    }
    static void sFont(double v, void* ud) { static_cast<TextTool*>(ud)->setFontIndex((int)v); }

    static void rgbaFromColor(const Color& c, float out[4])
    {
        out[0] = c.r / 255.0f; out[1] = c.g / 255.0f; out[2] = c.b / 255.0f; out[3] = c.a / 255.0f;
    }
    void setSizeV(double v)
    {
        const int idx = ed_ ? targetTextLayer(*ed_) : -1;
        if (idx >= 0) {
            const TextData& s = ed_->doc.layers[(size_t)idx].text;
            float col[4]; rgbaFromColor(s.color, col);
            ed_->applyTextStyle(idx, (int)v, s.lineSpacing, s.opacity, col);
            szStage_ = (int)v;
        } else size_ = (int)v;
    }
    void setLineSpacingV(double v)
    {
        const int idx = ed_ ? targetTextLayer(*ed_) : -1;
        if (idx >= 0) {
            const TextData& s = ed_->doc.layers[(size_t)idx].text;
            float col[4]; rgbaFromColor(s.color, col);
            ed_->applyTextStyle(idx, s.size, (float)v, s.opacity, col);
            lnStage_ = (float)v;
        } else lineSpacing_ = (float)v;
    }
    void setOpacityV(double v)
    {
        const int idx = ed_ ? targetTextLayer(*ed_) : -1;
        if (idx >= 0) {
            const TextData& s = ed_->doc.layers[(size_t)idx].text;
            float col[4]; rgbaFromColor(s.color, col);
            ed_->applyTextStyle(idx, s.size, s.lineSpacing, (float)v, col);
            opStage_ = (float)v;
        } else opacity_ = (float)v;
    }
    void setTintColor(const float* c)
    {
        const int idx = ed_ ? targetTextLayer(*ed_) : -1;
        if (idx >= 0) {
            const TextData& s = ed_->doc.layers[(size_t)idx].text;
            ed_->applyTextStyle(idx, s.size, s.lineSpacing, s.opacity, c);
        }
        for (int k = 0; k < 4; ++k) tint_[k] = c[k];
    }
    static void sSizeV(double v, void* ud) { static_cast<TextTool*>(ud)->setSizeV(v); }
    static void sLineV(double v, void* ud) { static_cast<TextTool*>(ud)->setLineSpacingV(v); }
    static void sOpacityV(double v, void* ud) { static_cast<TextTool*>(ud)->setOpacityV(v); }
    static void sTint(const float* c, void* ud) { static_cast<TextTool*>(ud)->setTintColor(c); }

    Color tintColor() const
    {
        return Color((uint8_t)std::lround(std::clamp(tint_[0], 0.0f, 1.0f) * 255.0f),
                     (uint8_t)std::lround(std::clamp(tint_[1], 0.0f, 1.0f) * 255.0f),
                     (uint8_t)std::lround(std::clamp(tint_[2], 0.0f, 1.0f) * 255.0f),
                     (uint8_t)std::lround(std::clamp(tint_[3], 0.0f, 1.0f) * 255.0f));
    }

    int targetTextLayer(const Editor& e) const
    {
        if (editLayerId_ >= 0) {
            const int i = e.doc.indexOfId(editLayerId_);
            if (i >= 0 && e.doc.layers[(size_t)i].kind == LayerKind::Text) return i;
        }
        const Layer* l = e.doc.activeLayer();
        if (l && l->kind == LayerKind::Text) return e.doc.active;
        return -1;
    }

    const TextData* editingText(const Editor& e) const
    {
        if (editLayerId_ < 0) return nullptr;
        const int i = e.doc.indexOfId(editLayerId_);
        if (i < 0 || e.doc.layers[(size_t)i].kind != LayerKind::Text) return nullptr;
        return &e.doc.layers[(size_t)i].text;
    }

    int hitTextLayer(const Editor& e, Vec2f p) const
    {
        const float pad = 4.0f;
        for (int i = (int)e.doc.layers.size() - 1; i >= 0; --i) {
            const Layer& l = e.doc.layers[(size_t)i];
            if (l.kind != LayerKind::Text || !l.visible) continue;
            const Rect box = textBoxRect(l.text);
            if (box.empty()) continue;
            if (p.x >= (float)box.x - pad && p.x <= (float)(box.x + box.w) + pad &&
                p.y >= (float)box.y - pad && p.y <= (float)(box.y + box.h) + pad)
                return i;
        }
        return -1;
    }

    void cancel(Editor& e)
    {
        if (!editing_) return;
        resetSession();
        e.cancelTextSession();
    }

    void commit(Editor& e)
    {
        if (!editing_) return;
        const char* keep = text_;
        editing_ = false;
        editLayerId_ = -1;
        e.updateTextEditing(keep);
        e.commitTextSession();
        resetSession();
    }

    Editor* ed_ = nullptr;
    bool editing_ = false;
    bool dragSelect_ = false;
    bool moveBox_ = false;
    bool hoverFrame_ = false;
    Vec2f movePressLocal_{};
    float moveX0_ = 0.0f, moveY0_ = 0.0f;
    int editLayerId_ = -1;
    SkMatrix editXf_ = SkMatrix::I();
    char text_[512] = "";
    int caret_ = 0;
    int anchor_ = -1;
    float blink_ = 0.0f;
    std::string font_;
    std::vector<const char*> fontNames_;
    int fontShown_ = 0;
    int size_ = 64;
    float lineSpacing_ = 6.0f;
    float opacity_ = 1.0f;
    float tint_[4] = {0.09f, 0.09f, 0.10f, 1.0f};
    float lastFg_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int   szStage_ = 64;
    float lnStage_ = 6.0f;
    float opStage_ = 1.0f;
};

}

std::unique_ptr<Tool> makeTextTool() { return std::make_unique<TextTool>(); }

}
