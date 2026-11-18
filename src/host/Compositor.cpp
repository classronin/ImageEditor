#include "host/Compositor.h"

#include "gfx/region.h"
#include "include/core/SkCanvas.h"
#include "nodes/Node.h"
#include "os/skia/skia_surface.h"
#include "os/window.h"

namespace ie {

namespace {
bool overlaps(const Rect& a, const Rect& b)
{
    return !a.intersect(b).empty();
}
}

void Compositor::add(const Rect& r)
{
    if (r.empty()) return;
    if (dirty_.size() >= 32) {
        collapse(r);
        return;
    }
    Rect n = r;
    for (bool merged = true; merged;) {
        merged = false;
        for (auto it = dirty_.begin(); it != dirty_.end(); ++it) {
            if (overlaps(*it, n)) {
                n = n.unite(*it);
                dirty_.erase(it);
                merged = true;
                break;
            }
        }
    }
    dirty_.push_back(n);
}

void Compositor::collapse(const Rect& r)
{
    Rect u = r;
    for (const Rect& d : dirty_) u = u.unite(d);
    dirty_.clear();
    dirty_.push_back(u);
}

void Compositor::invalidateAll(const Rect& a)
{
    area_ = a;
    dirty_.clear();
    dirty_.push_back(a);
}

void Compositor::clear()
{
    dirty_.clear();
}

void Compositor::present(os::Window* window, os::SkiaSurface* surface, Node& root)
{
    if (dirty_.empty() || !window || !surface || !surface->isValid()) return;

    SkCanvas* canvas = &surface->canvas();

    canvas->save();
    for (const Rect& d : dirty_) {
        Rect r = d.intersect(root.clipRect());
        if (r.empty()) continue;
        canvas->save();
        canvas->clipRect(SkRect::MakeLTRB((float)r.x, (float)r.y, (float)r.right(), (float)r.bottom()));
        root.paint(*canvas);
        canvas->restore();
    }
    canvas->restore();

    gfx::Region reg;
    for (const Rect& d : dirty_) {
        gfx::Region one(gfx::Rect(gfx::Point(d.x, d.y), gfx::Point(d.right(), d.bottom())));
        reg += one;
    }
    window->invalidateRegion(reg);
    clear();
}

}
