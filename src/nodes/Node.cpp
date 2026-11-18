#include "nodes/Node.h"

namespace ie {

void Node::place(const Rect& absRect, const Rect& clipRect)
{
    const Rect oldFrame = frame_;
    const Rect oldClip = clip_;
    if (oldFrame != absRect) {
        markDirty(oldFrame);
        markDirty(absRect);
    }
    frame_ = absRect;
    clip_ = clipRect;
    onPlaced(oldFrame, oldClip);
}

void Node::requestLayout()
{
    if (parent_) parent_->onChildLayoutRequested(this);
}

Node* Node::hit(float x, float y)
{
    if (clip_.empty() || frame_.empty()) return nullptr;
    const int m = hitMargin();
    if (x < (float)(frame_.x - m) || y < (float)(frame_.y - m) || x >= (float)(frame_.right() + m) ||
        y >= (float)(frame_.bottom() + m))
        return nullptr;
    if (x < (float)(clip_.x - m) || y < (float)(clip_.y - m) || x >= (float)(clip_.right() + m) ||
        y >= (float)(clip_.bottom() + m))
        return nullptr;
    return this;
}

bool Node::acceptsPointer(float x, float y) const
{
    const int m = hitMargin();
    return !frame_.empty() && x >= (float)(frame_.x - m) && y >= (float)(frame_.y - m) &&
           x < (float)(frame_.right() + m) && y < (float)(frame_.bottom() + m);
}

void Node::takeDirty(std::vector<Rect>& out)
{
    for (const Rect& r : dirty_) out.push_back(r);
    dirty_.clear();
}

void Node::markDirty(const Rect& absRect)
{
    if (absRect.empty()) return;
    dirty_.push_back(absRect);
    if (dirty_.size() > 8) {
        Rect u = dirty_.front();
        for (const Rect& r : dirty_) u = u.unite(r);
        dirty_.clear();
        dirty_.push_back(u);
    }
}

}
