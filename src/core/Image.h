#pragma once

#include "core/Types.h"

#include <memory>
#include <vector>

namespace ie {

class Image {
public:
    Image() = default;
    Image(int w, int h) { reset(w, h); }

    void reset(int w, int h)
    {
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        w_ = w;
        h_ = h;
        buf_ = std::make_shared<std::vector<uint8_t>>((size_t)w * h * 4, 0);
    }

    void assign(int w, int h, const uint8_t* rgba)
    {
        reset(w, h);
        if (rgba && w_ > 0 && h_ > 0) std::copy(rgba, rgba + (size_t)w_ * h_ * 4, buf_->begin());
    }

    int width() const { return w_; }
    int height() const { return h_; }
    bool empty() const { return w_ <= 0 || h_ <= 0 || !buf_; }
    Rect bounds() const { return Rect{0, 0, w_, h_}; }

    Rect contentBounds() const;

    const uint8_t* data() const { return buf_ ? buf_->data() : nullptr; }
    const uint32_t* pixels() const { return reinterpret_cast<const uint32_t*>(data()); }

    static std::size_t detachCopies() { return detachCopies_; }
    static void resetDetachCopies() { detachCopies_ = 0; }

    uint8_t* data()
    {
        detach();
        return buf_->data();
    }
    uint32_t* pixels()
    {
        detach();
        return reinterpret_cast<uint32_t*>(data());
    }

    void detach()
    {
        if (!buf_) {
            buf_ = std::make_shared<std::vector<uint8_t>>();
            return;
        }
        if (buf_.use_count() > 1) {
            buf_ = std::make_shared<std::vector<uint8_t>>(*buf_);
            ++detachCopies_;
        }
    }

    bool sharesWith(const Image& o) const { return buf_ && buf_ == o.buf_; }
    bool hasPixels() const { return buf_ && !buf_->empty(); }

    void fillRect(Rect r, uint32_t packed);
    void fillAll(uint32_t packed) { fillRect(bounds(), packed); }

    void blit(const Image& o, int dx, int dy);

private:
    int w_ = 0, h_ = 0;
    std::shared_ptr<std::vector<uint8_t>> buf_;
    inline static std::size_t detachCopies_ = 0;
};

Image cropRegion(const Image& src, Rect r);

Image scaleImage(const Image& src, int nw, int nh);

void copyRegion(Image& dst, const Image& src, Rect r);

}
