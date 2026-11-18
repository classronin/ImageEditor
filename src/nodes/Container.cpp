#include "nodes/Container.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>

namespace ie {

void Container::add(std::unique_ptr<Node> child)
{
    if (!child) return;
    child->setParent(this);
    Node* raw = child.get();
    children_.push_back(std::move(child));

    Rect temp = frame_;
    temp.w = std::max(temp.w, 1);
    temp.h = std::max(temp.h, 1);
    raw->place(temp, temp);
    pending_.push_back(raw);
    layoutIfNeeded();
}

void Container::onChildLayoutRequested(Node* child)
{
    if (!child) return;
    if (std::find(pending_.begin(), pending_.end(), child) == pending_.end()) pending_.push_back(child);
}

void Container::layoutIfNeeded()
{
    if (pending_.empty()) return;
    pending_.clear();
    arrange();
    for (auto& c : children_)
        if (auto* cont = dynamic_cast<Container*>(c.get())) cont->layoutIfNeeded();
}

void Container::reflow()
{
    arrange();
    for (auto& c : children_)
        if (auto* cont = dynamic_cast<Container*>(c.get())) cont->reflow();
}

void Container::arrange()
{
    int y = frame_.y + pad_;
    const int innerW = std::max(0, frame_.w - pad_ * 2);
    for (auto& c : children_) {
        const Size s = c->requestSize();
        int h = s.h;
        if (h <= 0) h = std::max(0, frame_.bottom() - pad_ - y);
        placeChildAbs(c.get(), Rect{frame_.x + pad_, y, innerW, h});
        y += h;
    }
}

void Container::onPlaced(const Rect& oldFrame, const Rect& oldClip)
{
    if (oldFrame == frame_ && oldClip == clip_ && pending_.empty()) return;

    const int dx = frame_.x - oldFrame.x;
    const int dy = frame_.y - oldFrame.y;
    for (auto& c : children_) {
        Rect rel = c->frame();
        if (!oldFrame.empty() && !c->frame().empty()) {
            rel.x += dx;
            rel.y += dy;
        }
        Rect abs = rel;
        abs.x = frame_.x + (c->frame().x - oldFrame.x);
        abs.y = frame_.y + (c->frame().y - oldFrame.y);
        placeChildAbs(c.get(), abs);
        pending_.push_back(c.get());
    }
    layoutIfNeeded();
}

void Container::placeChildAbs(Node* child, const Rect& absRect)
{
    const Rect clip = absRect.intersect(clip_.empty() ? frame_ : clip_);
    child->place(absRect, clip);
}

void Container::paint(SkCanvas& canvas)
{
    for (auto& c : children_) {
        if (!c->visible()) continue;
        const Rect& f = c->frame();
        Rect visible = f.intersect(c->clipRect());
        if (visible.empty()) continue;

        canvas.save();
        canvas.clipRect(SkRect::MakeLTRB((float)visible.x, (float)visible.y, (float)visible.right(),
                                         (float)visible.bottom()));
        c->paint(canvas);
        canvas.restore();
    }
}

Node* Container::hit(float x, float y)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (!it->get()->visible()) continue;
        if (Node* n = it->get()->hit(x, y)) return n;
    }
    return interactive_ ? Node::hit(x, y) : nullptr;
}

bool Container::cursorAt(float x, float y, CursorShape& out)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        Node* c = it->get();
        if (!c->visible()) continue;
        if (!c->acceptsPointer(x, y)) continue;
        if (c->cursorAt(x, y, out)) return true;
        if (c->hit(x, y)) {
            out = CursorShape::Arrow;
            return true;
        }
    }
    return interactive_ ? Node::cursorAt(x, y, out) : false;
}

void Container::takeDirty(std::vector<Rect>& out)
{
    Node::takeDirty(out);
    for (auto& c : children_) c->takeDirty(out);
}

void OverlayLayer::addFill(std::unique_ptr<Node> n)
{
    anchors_.push_back(Anchor{true, 0.0f, 0.0f});
    Container::add(std::move(n));
    layoutIfNeeded();
}

void OverlayLayer::addAt(std::unique_ptr<Node> n, float ax, float ay)
{
    anchors_.push_back(Anchor{false, ax, ay});
    Container::add(std::move(n));
    layoutIfNeeded();
}

void OverlayLayer::pop()
{
    if (children_.empty()) return;
    children_.pop_back();
    if (!anchors_.empty()) anchors_.pop_back();
    pending_.clear();
    markDirty(frame_);
}

void OverlayLayer::arrange()
{
    for (size_t i = 0; i < children_.size(); ++i) {
        Node* ch = children_[i].get();
        Rect r = frame_;
        if (i < anchors_.size() && !anchors_[i].fill) {
            const Size s = ch->requestSize();
            const int w = std::clamp(s.w, 0, std::max(0, frame_.w));
            const int h = std::clamp(s.h, 0, std::max(0, frame_.h));
            const int ax = (int)std::lround(anchors_[i].ax);
            const int ay = (int)std::lround(anchors_[i].ay);
            r = Rect{frame_.x + std::clamp(ax - frame_.x, 0, std::max(0, frame_.w - w)),
                    frame_.y + std::clamp(ay - frame_.y, 0, std::max(0, frame_.h - h)), w, h};
        }
        placeChildAbs(ch, r);
    }
}

}
