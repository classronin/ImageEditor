#pragma once

#include "nodes/Node.h"

#include <memory>
#include <vector>

namespace ie {

class Container : public Node {
public:
    void add(std::unique_ptr<Node> child);
    void onChildLayoutRequested(Node* child) override;
    void layoutIfNeeded();
    void reflow();
    void paint(SkCanvas& canvas) override;
    Node* hit(float x, float y) override;
    bool cursorAt(float x, float y, CursorShape& out) override;
    void takeDirty(std::vector<Rect>& out) override;

    std::size_t childCount() const { return children_.size(); }

protected:
    virtual void arrange();
    void onPlaced(const Rect& oldFrame, const Rect& oldClip) override;

    void placeChildAbs(Node* child, const Rect& absRect);

    std::vector<std::unique_ptr<Node>> children_;
    std::vector<Node*> pending_;
    int pad_ = 0;
    bool interactive_ = false;
};

class OverlayLayer : public Container {
public:
    void addFill(std::unique_ptr<Node> n);
    void addAt(std::unique_ptr<Node> n, float ax, float ay);
    void pop();
    Node* top() const { return children_.empty() ? nullptr : children_.back().get(); }

protected:
    void arrange() override;

private:
    struct Anchor {
        bool fill = false;
        float ax = 0.0f, ay = 0.0f;
    };
    std::vector<Anchor> anchors_;
};

}
