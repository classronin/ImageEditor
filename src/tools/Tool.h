#pragma once

#include "app/Editor.h"
#include "host/Input.h"
#include "nodes/Overlay.h"
#include "ui/CursorShape.h"

#include <string>
#include <vector>

namespace ie {

struct InputFrame {
    Vec2f mouse{0, 0};
    bool down[3] = {false, false, false};
    bool pressed[3] = {false, false, false};
    bool released[3] = {false, false, false};
    bool doubleClick = false;
    float wheel = 0.0f;
    bool ctrl = false, shift = false, alt = false;
    bool space = false;
    float dt = 0.0f;

    int key = 0;
    bool keyPressed = false;
    bool keyRepeat = false;

    bool dragging(int b) const { return down[b] && !pressed[b]; }
};

enum class ToolId { Move, Select, Lasso, Wand, Crop, Brush, Bucket, Text, Shape, Eraser, Gradient, Mask };


enum class PropKind { Header, Label, Separator, Float, Int, Bool, Combo, Color, Button, Seg };

enum class SegIcon : uint8_t { Text, New, Add, Sub, Inter };

extern const char* const kSelModeItems[4];
extern const SegIcon kSelModeIcons[4];

struct Prop {
    PropKind kind = PropKind::Label;
    const char* label = "";
    const char* hint = "";
    float* f = nullptr;
    float lo = 0.0f, hi = 1.0f, step = 0.01f;
    const char* fmt = "%.0f";
    int* i = nullptr;
    int ilo = 0, ihi = 100, istep = 1;
    bool* b = nullptr;
    float* col = nullptr;
    int* sel = nullptr;
    const char* const* items = nullptr;
    int itemCount = 0;
    const SegIcon* icons = nullptr;
    const char* right = "";
    bool wide = false;
    int comboBoxW = 0;
    void (*action)(void*) = nullptr;
    void* ud = nullptr;
    void (*setV)(double, void*) = nullptr;
    void (*setColor)(const float*, void*) = nullptr;
    void* setUd = nullptr;
};

class Props : public std::vector<Prop> {
public:
    void header(const char* label)
    {
        Prop p;
        p.kind = PropKind::Header;
        p.label = label;
        push_back(p);
    }
    void label(const char* text)
    {
        Prop p;
        p.kind = PropKind::Label;
        p.label = text;
        push_back(p);
    }
    void hint(const char* text, const char* sub = "")
    {
        Prop p;
        p.kind = PropKind::Label;
        p.label = text;
        p.hint = sub;
        push_back(p);
    }
    void separator()
    {
        Prop p;
        p.kind = PropKind::Separator;
        push_back(p);
    }
    void sliderF(const char* label, float* v, float lo, float hi,
                 void (*setfn)(double, void*) = nullptr, void* sud = nullptr, const char* fmt = "%.0f")
    {
        Prop p;
        p.kind = PropKind::Float;
        p.label = label;
        p.f = v;
        p.lo = lo;
        p.hi = hi;
        p.fmt = fmt;
        p.setV = setfn;
        p.setUd = sud;
        push_back(p);
    }
    void sliderI(const char* label, int* v, int lo, int hi,
                 void (*setfn)(double, void*) = nullptr, void* sud = nullptr)
    {
        Prop p;
        p.kind = PropKind::Int;
        p.label = label;
        p.i = v;
        p.ilo = lo;
        p.ihi = hi;
        p.setV = setfn;
        p.setUd = sud;
        push_back(p);
    }
    void check(const char* label, bool* v, void (*setfn)(double, void*) = nullptr, void* sud = nullptr)
    {
        Prop p;
        p.kind = PropKind::Bool;
        p.label = label;
        p.b = v;
        p.setV = setfn;
        p.setUd = sud;
        push_back(p);
    }
    void combo(const char* label, int* sel, const char* const* items, int n,
               void (*setfn)(double, void*) = nullptr, void* sud = nullptr, int boxW = 0)
    {
        Prop p;
        p.kind = PropKind::Combo;
        p.label = label;
        p.sel = sel;
        p.items = items;
        p.itemCount = n;
        p.setV = setfn;
        p.setUd = sud;
        p.comboBoxW = boxW;
        push_back(p);
    }
    void seg(int* sel, const char* const* items, int n, void (*setfn)(double, void*) = nullptr,
             void* sud = nullptr, const SegIcon* icons = nullptr)
    {
        Prop p;
        p.kind = PropKind::Seg;
        p.sel = sel;
        p.items = items;
        p.itemCount = n;
        p.icons = icons;
        p.setV = setfn;
        p.setUd = sud;
        push_back(p);
    }
    void color(const char* label, float* rgba, void (*set)(const float*, void*) = nullptr, void* sud = nullptr)
    {
        Prop p;
        p.kind = PropKind::Color;
        p.label = label;
        p.col = rgba;
        p.setColor = set;
        p.setUd = sud;
        push_back(p);
    }
    void button(const char* label, void (*fn)(void*), void* ud, const char* right = nullptr)
    {
        Prop p;
        p.kind = PropKind::Button;
        p.label = label;
        p.action = fn;
        p.ud = ud;
        p.right = right ? right : "";
        p.wide = right != nullptr;
        push_back(p);
    }
};


class Tool {
public:
    virtual ~Tool() = default;

    virtual ToolId id() const = 0;
    virtual const char* name() const = 0;

    virtual void onEnter(Editor&) {}
    virtual void onLeave(Editor&) {}

    virtual void onPress(Editor&, const InputFrame&, int) {}
    virtual void onDrag(Editor&, const InputFrame&, int) {}
    virtual void onRelease(Editor&, const InputFrame&, int) {}
    virtual void onKey(Editor&, const InputFrame&) {}
    virtual void update(Editor&, const InputFrame&) {}

    virtual bool onWheel(Editor&, const InputFrame&) { return false; }

    virtual bool consumesKey(int scancode) const { (void)scancode; return false; }

    virtual bool busy() const { return false; }
    virtual bool animating() const { return false; }

    virtual bool acceptsButton(int b) const { return b == 0; }
    virtual CursorShape cursor(const InputFrame&) const { return CursorShape::Arrow; }

    virtual void props(Props&, Editor&) = 0;
    virtual bool liveSize(int& w, int& h)
    {
        (void)w;
        (void)h;
        return false;
    }
    virtual void overlay(Overlay*, const Editor&, const CanvasViewState&, const InputFrame&) {}

    virtual bool canvasTextActive() const { return false; }
    virtual char* canvasTextBuffer() { return nullptr; }
    virtual int canvasTextCapacity() const { return 0; }
    virtual bool canvasTextKey(Editor&, const KeyEvent&) { return false; }

    virtual bool caretImageRect(Editor&, Rect& out)
    {
        (void)out;
        return false;
    }

    virtual bool beginEditAt(Editor&, const Vec2f&) { return false; }
};

}
