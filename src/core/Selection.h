#pragma once

#include "core/Types.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ie {

class Selection {
public:
    struct Seg {
        float x0, y0, x1, y1;
    };

    Selection() : mask_(std::make_shared<std::vector<uint8_t>>()) {}

    int width = 0, height = 0;
    bool active = false;

    void reset(int w, int h);
    void clear();
    void selectAll();
    void setRect(Rect r);
    void addRect(Rect r);
    void subtractRect(Rect r);
    void intersectRect(Rect r);
    void translate(int dx, int dy);
    void invert();

    void setEllipse(Rect r);
    void addEllipse(Rect r);
    void subtractEllipse(Rect r);
    void intersectEllipse(Rect r);
    void selectPolygon(const std::vector<Vec2f>& pts);
    void addPolygon(const std::vector<Vec2f>& pts);
    void subtractPolygon(const std::vector<Vec2f>& pts);
    void intersectPolygon(const std::vector<Vec2f>& pts);

    void setAlphaMask(const uint8_t* alpha, int aw, int ah, int ox, int oy, uint8_t threshold);
    void addMask(const Selection& o);
    void subtractMask(const Selection& o);
    void intersectMask(const Selection& o);

    void selectFlood(const uint32_t* canvas, int sx, int sy, int tolerance, bool add);
    void subtractFlood(const uint32_t* canvas, int sx, int sy, int tolerance);
    void intersectFlood(const uint32_t* canvas, int sx, int sy, int tolerance);

    bool empty() const { return !active; }
    Rect bounds() const { return bounds_; }
    bool contains(int x, int y) const;
    const uint8_t* maskPtr() const { return active ? mask_->data() : nullptr; }
    const std::vector<uint8_t>& mask() const { return *mask_; }
    const std::vector<Seg>& segments() const { return segs_; }
    bool sameAs(const Selection& o) const;

private:
    std::shared_ptr<std::vector<uint8_t>> mask_;
    std::vector<Seg> segs_;
    Rect bounds_{};
    Rect lastRect_{};
    bool rectMode_ = false;

    void detachMask();
    void intersectWith(const Selection& o);
    void rebuildOutline();
    void fillMask(Rect r, uint8_t value);
    void fillEllipse(Rect r, uint8_t value);
    void refillRect();
    void fillPolygon(const std::vector<Vec2f>& pts, uint8_t value);
    void floodFill(const uint32_t* canvas, int sx, int sy, int tolerance, uint8_t value);
};

}
