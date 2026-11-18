#pragma once

#include "core/Clipboard.h"
#include "core/Keymap.h"
#include "core/Utf8.h"
#include "host/Input.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ie {
namespace ui {

struct TextSnapshot {
    std::string text;
    int caret = 0;
    int anchor = -1;
};

struct TextSel {
    int caret = 0;
    int anchor = -1;
    bool dragging = false;

    std::vector<TextSnapshot> undoStack;
    std::vector<TextSnapshot> redoStack;
    bool typing = false;

    bool hasSel() const { return anchor >= 0 && anchor != caret; }
    int lo() const { return hasSel() ? std::min(anchor, caret) : caret; }
    int hi() const { return hasSel() ? std::max(anchor, caret) : caret; }
    void clear() { anchor = -1; dragging = false; }
    void reset()
    {
        caret = 0;
        anchor = -1;
        dragging = false;
        undoStack.clear();
        redoStack.clear();
        typing = false;
    }
    void selectAll(int n) { caret = n; anchor = (n > 0 ? 0 : -1); dragging = false; }
    void clamp(int n)
    {
        caret = std::clamp(caret, 0, n);
        if (anchor >= 0) anchor = std::clamp(anchor, 0, n);
    }

    void remember(const std::string& s)
    {
        if (!typing) {
            undoStack.push_back(TextSnapshot{s, caret, anchor});
            if (undoStack.size() > 128) undoStack.erase(undoStack.begin());
        }
        redoStack.clear();
    }
    bool undo(std::string& s)
    {
        if (undoStack.empty()) return false;
        redoStack.push_back(TextSnapshot{s, caret, anchor});
        const TextSnapshot& snap = undoStack.back();
        s = snap.text;
        caret = std::clamp(snap.caret, 0, (int)s.size());
        anchor = (snap.anchor >= 0 ? std::clamp(snap.anchor, 0, (int)s.size()) : -1);
        undoStack.pop_back();
        typing = false;
        return true;
    }
    bool redo(std::string& s)
    {
        if (redoStack.empty()) return false;
        undoStack.push_back(TextSnapshot{s, caret, anchor});
        const TextSnapshot& snap = redoStack.back();
        s = snap.text;
        caret = std::clamp(snap.caret, 0, (int)s.size());
        anchor = (snap.anchor >= 0 ? std::clamp(snap.anchor, 0, (int)s.size()) : -1);
        redoStack.pop_back();
        typing = false;
        return true;
    }
};

struct ByteStep {
    int prev(const std::string&, int i) const { return i > 0 ? i - 1 : 0; }
    int next(const std::string& s, int i) const { return i < (int)s.size() ? i + 1 : (int)s.size(); }
};

struct Utf8Step {
    int prev(const std::string& s, int i) const { return utf8::prev(s, i); }
    int next(const std::string& s, int i) const { return utf8::next(s, i); }
};

enum class PasteRule {
    Text,
    Number,
};

inline bool numberFromText(const std::string& in, std::string& out)
{
    auto space = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
    size_t b = 0, e = in.size();
    while (b < e && space(in[b])) ++b;
    while (e > b && space(in[e - 1])) --e;
    if (b >= e) return false;
    const std::string s = in.substr(b, e - b);
    size_t i = 0;
    if (s[0] == '-' || s[0] == '+') ++i;
    bool digit = false, dot = false;
    for (; i < s.size(); ++i) {
        const char ch = s[i];
        if (ch >= '0' && ch <= '9') {
            digit = true;
            continue;
        }
        if (ch == '.' && !dot) {
            dot = true;
            continue;
        }
        return false;
    }
    if (!digit) return false;
    out = (s[0] == '+') ? s.substr(1) : s;
    return true;
}

inline std::string clipText(const TextSel& sel, const std::string& s)
{
    if (sel.hasSel()) return s.substr((size_t)sel.lo(), (size_t)(sel.hi() - sel.lo()));
    return s;
}

inline void eraseSel(TextSel& sel, std::string& s)
{
    if (!sel.hasSel()) return;
    const int a = sel.lo(), b = sel.hi();
    s.erase(s.begin() + a, s.begin() + b);
    sel.caret = a;
    sel.anchor = -1;
}

inline void insertAt(TextSel& sel, std::string& s, const std::string& ins)
{
    s.insert((size_t)sel.caret, ins);
    sel.caret += (int)ins.size();
}

inline void pasteInto(TextSel& sel, std::string& s, const std::string& in, PasteRule rule,
                      bool flattenNewlines)
{
    std::string t = in;
    if (flattenNewlines) {
        for (char& ch : t)
            if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    }
    if (rule == PasteRule::Number) {
        std::string num;
        if (!numberFromText(t, num)) return;
        t = num;
    }
    if (t.empty()) return;
    sel.remember(s);
    eraseSel(sel, s);
    insertAt(sel, s, t);
    sel.anchor = -1;
    sel.typing = false;
}

inline void textSelInsert(TextSel& sel, std::string& s, const std::string& ch)
{
    if (ch.empty()) return;
    sel.remember(s);
    eraseSel(sel, s);
    insertAt(sel, s, ch);
    sel.anchor = -1;
    sel.typing = true;
}


inline unsigned commandMods(const KeyEvent& e) { return keymap::modsOf(e); }

enum class TextCmd {
    None = 0,
    SelectAll,
    Copy,
    Cut,
    Paste,
    Undo,
    Redo,
    Backspace,
    Delete,
    Left,
    Right,
    Home,
    End,
};

struct TextBinding {
    int key;
    unsigned mods;
    TextCmd cmd;
};

inline TextCmd resolveTextCmd(const KeyEvent& e)
{
    static const TextBinding table[] = {
        {KeyA, keymap::Ctrl, TextCmd::SelectAll},
        {KeyC, keymap::Ctrl, TextCmd::Copy},
        {KeyX, keymap::Ctrl, TextCmd::Cut},
        {KeyV, keymap::Ctrl, TextCmd::Paste},
        {KeyZ, keymap::Ctrl, TextCmd::Undo},
        {KeyZ, keymap::Ctrl | keymap::Shift, TextCmd::Redo},
        {KeyY, keymap::Ctrl, TextCmd::Redo},
        {KeyBackspace, keymap::None, TextCmd::Backspace},
        {KeyDelete, keymap::None, TextCmd::Delete},
        {KeyLeft, keymap::None, TextCmd::Left},
        {KeyRight, keymap::None, TextCmd::Right},
        {KeyHome, keymap::None, TextCmd::Home},
        {KeyEnd, keymap::None, TextCmd::End},
    };
    const unsigned m = commandMods(e);
    TextCmd c = (TextCmd)keymap::resolve(table, e.scancode, m);
    if (c == TextCmd::None && (m & keymap::Shift))
        c = (TextCmd)keymap::resolve(table, e.scancode, m & ~(unsigned)keymap::Shift);
    return c;
}

template <class Step>
bool textSelKey(const KeyEvent& e, TextSel& sel, std::string& s, Step step, PasteRule rule,
                bool flattenNewlines = false)
{
    if (!e.pressed) return false;
    const TextCmd cmd = resolveTextCmd(e);
    const bool extend = e.shift;
    switch (cmd) {
        case TextCmd::SelectAll:
            sel.selectAll((int)s.size());
            return true;
        case TextCmd::Copy:
            clipboard::setText(clipText(sel, s));
            return true;
        case TextCmd::Cut:
            clipboard::setText(clipText(sel, s));
            sel.remember(s);
            if (sel.hasSel()) eraseSel(sel, s);
            else {
                s.clear();
                sel.caret = 0;
                sel.anchor = -1;
            }
            sel.typing = false;
            return true;
        case TextCmd::Paste: {
            std::string ins;
            if (clipboard::getText(ins)) pasteInto(sel, s, ins, rule, flattenNewlines);
            return true;
        }
        case TextCmd::Undo:
        case TextCmd::Redo: {
            if (cmd == TextCmd::Undo) sel.undo(s);
            else sel.redo(s);
            return true;
        }
        case TextCmd::Backspace:
            if (!sel.hasSel() && sel.caret <= 0) {
                sel.typing = false;
                return true;
            }
            sel.remember(s);
            if (sel.hasSel()) eraseSel(sel, s);
            else {
                const int p = step.prev(s, sel.caret);
                s.erase((size_t)p, (size_t)(sel.caret - p));
                sel.caret = p;
            }
            sel.typing = false;
            return true;
        case TextCmd::Delete:
            if (!sel.hasSel() && sel.caret >= (int)s.size()) {
                sel.typing = false;
                return true;
            }
            sel.remember(s);
            if (sel.hasSel()) eraseSel(sel, s);
            else {
                const int q = step.next(s, sel.caret);
                s.erase((size_t)sel.caret, (size_t)(q - sel.caret));
            }
            sel.typing = false;
            return true;
        case TextCmd::Left:
            sel.typing = false;
            if (sel.hasSel() && !extend) {
                sel.caret = sel.lo();
                sel.anchor = -1;
                return true;
            }
            if (extend && sel.anchor < 0) sel.anchor = sel.caret;
            sel.caret = step.prev(s, sel.caret);
            if (!extend) sel.anchor = -1;
            return true;
        case TextCmd::Right:
            if (sel.hasSel() && !extend) {
                sel.caret = sel.hi();
                sel.anchor = -1;
                return true;
            }
            if (extend && sel.anchor < 0) sel.anchor = sel.caret;
            sel.caret = step.next(s, sel.caret);
            if (!extend) sel.anchor = -1;
            return true;
        case TextCmd::Home:
            if (extend && sel.anchor < 0) sel.anchor = sel.caret;
            sel.caret = 0;
            if (!extend) sel.anchor = -1;
            return true;
        case TextCmd::End:
            if (extend && sel.anchor < 0) sel.anchor = sel.caret;
            sel.caret = (int)s.size();
            if (!extend) sel.anchor = -1;
            return true;
        case TextCmd::None:
        default:
            return false;
    }
}

}
}
