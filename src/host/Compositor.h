#pragma once

#include "core/Types.h"

#include <vector>

class SkCanvas;

namespace os {
class Window;
class SkiaSurface;
}

namespace ie {

class Node;

class Compositor {
public:
    void add(const Rect& r);
    void clear();
    bool empty() const { return dirty_.empty(); }

    void invalidateAll(const Rect& area);

    void present(os::Window* window, os::SkiaSurface* surface, Node& root);

private:
    void collapse(const Rect& area);
    std::vector<Rect> dirty_{};
    Rect area_{};
};

}
