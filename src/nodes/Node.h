#pragma once

#include "core/Types.h"
#include "host/Input.h"
#include "ui/CursorShape.h"

#include <string>
#include <vector>

class SkCanvas;

namespace ie {

struct Size {
    int w = 0, h = 0;
    bool operator==(const Size& o) const { return w == o.w && h == o.h; }
    bool operator!=(const Size& o) const { return !(*this == o); }
};

class Node {
public:
    virtual ~Node() = default;

    void place(const Rect& absRect, const Rect& clipRect);
    void setParent(Node* p) { parent_ = p; }

    void requestLayout();
    virtual void onChildLayoutRequested(Node*) {}

    const Rect& frame() const { return frame_; }
    const Rect& clipRect() const { return clip_; }
    virtual Size requestSize() const { return Size{0, 0}; }
    virtual bool preferredPos(int& x, int& y) const
    {
        (void)x;
        (void)y;
        return false;
    }

    virtual void paint(SkCanvas&) {}
    virtual int hitMargin() const { return 0; }
    virtual Node* hit(float x, float y);
    bool acceptsPointer(float x, float y) const;
    virtual bool onPointer(const PointerEvent&) { return false; }
    virtual bool onWheel(const WheelEvent&) { return false; }
    virtual bool onKey(const KeyEvent&) { return false; }
    virtual bool editingText() const { return false; }
    virtual bool swallowsOutsidePress() const { return false; }
    virtual int barMenuIndex() const { return -1; }
    virtual bool onDropFile(const std::string& path, float x, float y) { (void)path;(void)x;(void)y; return false; }
    virtual bool wantsAnimation() const { return false; }
    virtual void cancelTransientEdit(float px, float py)
    {
        (void)px;
        (void)py;
    }

    virtual bool cursorAt(float x, float y, CursorShape& out)
    {
        (void)x;
        (void)y;
        (void)out;
        return false;
    }

    virtual void takeDirty(std::vector<Rect>& out);
    void markDirty(const Rect& absRect);

    bool visible() const { return visible_; }

    virtual bool focusable() const { return false; }
    virtual Rect titleBarRect() const { return Rect{}; }
    virtual void setFocus(bool) {}

protected:
    virtual void onPlaced(const Rect& oldFrame, const Rect& oldClip) {}
    void setClip(const Rect& clip) { clip_ = clip; }

    Node* parent_ = nullptr;
    Rect frame_{};
    Rect clip_{};
    std::vector<Rect> dirty_{};
    bool visible_ = true;
};

}
