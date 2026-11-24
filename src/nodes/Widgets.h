#pragma once

#include "nodes/Container.h"
#include "nodes/Style.h"
#include "nodes/TextSel.h"

#include <functional>
#include <string>
#include <vector>

namespace ie {

class Button : public Node {
public:
    explicit Button(std::string text, std::function<void()> action = {});
    Size requestSize() const override;
    void paint(SkCanvas& c) override;
    bool onPointer(const PointerEvent& e) override;
    void setSize(int w, int h)
    {
        w_ = w;
        h_ = h;
    }
    void setText(std::string t) { text_ = std::move(t); }

private:
    std::string text_;
    std::function<void()> action_;
    int w_ = 0, h_ = 0;
    bool hover_ = false, pressed_ = false;
};

class Label : public Node {
public:
    explicit Label(std::string text, Color32 col = ui::theme().text);
    Size requestSize() const override;
    void paint(SkCanvas& c) override;
    void setText(std::string t) { text_ = std::move(t); }

private:
    std::string text_;
    Color32 col_;
};

class Checkbox : public Node {
public:
    Checkbox(std::string text, const bool* value, std::function<void(bool)> onSet = {});
    Size requestSize() const override;
    void paint(SkCanvas& c) override;
    bool onPointer(const PointerEvent& e) override;

private:
    std::string text_;
    const bool* value_;
    std::function<void(bool)> onSet_;
    bool hover_ = false;
};

struct NumField {
    using GetFn = double (*)(const void* ud);
    using SetFn = void (*)(void* ud, double v);

    GetFn get = nullptr;
    SetFn set = nullptr;
    const void* readUd = nullptr;
    void* writeUd = nullptr;

    double lo = 0.0, hi = 100.0, step = 1.0;
    const char* fmt = nullptr;

    /* slide=true：拖动/单击条内位置都能改值（滑杆形态，带进度条）；
       slide=false：只能点开输入，点一下直接进编辑，不画进度条（尺寸这类不该乱动的量用它）。 */
    bool slide = true;

    bool drag = false, dragged = false;
    float pressX = 0.0f, pressY = 0.0f;
    bool editing = false;
    std::string buf;
    ui::TextSel sel;

    double value() const { return get ? get(readUd) : 0.0; }
    void setValue(double v) const;
    bool hasSel() const { return sel.hasSel(); }
    int selBegin() const { return sel.lo(); }
    int selEnd() const { return sel.hi(); }
    void eraseSel();
    int caretAtX(float x, const Rect& r) const;
    std::string clipText() const;
    bool pasteText(const std::string& utf8);
    std::string text(bool editingRow) const;

    void beginEdit();
    void commit();
    void cancelEdit();

    void paint(SkCanvas& c, const Rect& r, bool editingRow) const;
    bool pointer(const PointerEvent& e, const Rect& r);
    bool key(const KeyEvent& e);
};

struct NumSession {
    NumField* field = nullptr;
    int index = -1;

    bool editing() const { return field && field->editing; }
    bool busy() const { return field && (field->drag || field->editing); }

    NumField* take(int i, NumField* f)
    {
        if (index != i && field) {
            if (field->editing) field->cancelEdit();
            field->drag = false;
            field->dragged = false;
        }
        index = i;
        field = f;
        return field;
    }

    bool route(const PointerEvent& e, const Rect& box)
    {
        if (!busy()) {
            clear();
            return false;
        }
        const bool used = field->pointer(e, box);
        if (!field->drag && !field->editing) clear();
        return used;
    }

    bool cancelUnless(float px, float py, const Rect& box)
    {
        if (!editing()) return false;
        if (box.contains((int)px, (int)py)) return false;
        field->cancelEdit();
        index = -1;
        return true;
    }

    void clear()
    {
        field = nullptr;
        index = -1;
    }
};

class Slider : public Node {
public:
    Slider(std::string label, const float* value, float lo, float hi, const char* fmt = "%.0f",
           std::function<void(float)> onSet = {});
    Size requestSize() const override;
    void paint(SkCanvas& c) override;
    bool onPointer(const PointerEvent& e) override;
    bool onWheel(const WheelEvent& e) override;

private:
    void apply(float x);

    std::string label_;
    const float* value_;
    float lo_, hi_;
    std::string fmt_;
    std::function<void(float)> onSet_;
    bool dragging_ = false;
};

class SliderI : public Node {
public:
    SliderI(std::string label, const int* value, int lo, int hi, std::function<void(int)> onSet = {});
    Size requestSize() const override;
    void paint(SkCanvas& c) override;
    bool onPointer(const PointerEvent& e) override;
    bool onWheel(const WheelEvent& e) override;

private:
    void apply(float x);

    std::string label_;
    const int* value_;
    int lo_, hi_;
    std::function<void(int)> onSet_;
    bool dragging_ = false;
};

class Combo : public Node {
public:
    Combo(std::string label, const int* index, std::vector<std::string> items,
          std::function<void()> onOpen = {}, std::function<void(int)> onChange = {});
    Size requestSize() const override;
    void paint(SkCanvas& c) override;
    bool onPointer(const PointerEvent& e) override;

private:
    std::string label_;
    const int* index_;
    std::vector<std::string> items_;
    std::function<void()> onOpen_;
    std::function<void(int)> onChange_;
    bool hover_ = false;
};

class TextField : public Node {
public:
    TextField(std::string* text, std::function<void()> submit = {}, bool password = false);
    Size requestSize() const override;
    void paint(SkCanvas& c) override;
    bool onPointer(const PointerEvent& e) override;
    bool onKey(const KeyEvent& e) override;
    void setFocus(bool on) override;
    bool focusable() const override { return true; }
    void setWidth(int w) { w_ = w; }
    bool focused() const { return focused_; }

    static void setFocusHost(TextField* f);

private:
    std::string* text_;
    std::function<void()> submit_;
    bool focused_ = false;
    bool password_;
    int w_ = 140;
    ui::TextSel sel;
    float blink_ = 0.0f;
};

class ColorSwatch : public Node {
public:
    ColorSwatch(float* rgba, std::function<void()> onOpen = {});
    Size requestSize() const override;
    void paint(SkCanvas& c) override;
    bool onPointer(const PointerEvent& e) override;

private:
    float* rgba_;
    std::function<void()> onOpen_;
};

class Row : public Container {
public:
    Row(int gap = 6, int pad = 0);
    bool wantsAnimation() const override { return false; }

protected:
    void arrange() override;

private:
    int gap_, pad_;
};

class Column : public Container {
public:
    Column(int gap = 4, int pad = 0);

protected:
    void arrange() override;

private:
    int gap_, pad_;
};

}
