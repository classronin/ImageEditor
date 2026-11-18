#pragma once

#include "nodes/Node.h"
#include "core/Image.h"
#include "nodes/OverlayRecord.h"
#include "tools/Tool.h"
#include "include/core/SkImage.h"
#include "include/core/SkPicture.h"
#include <vector>

namespace ie {

class Editor;
class Tool;

class CanvasNode : public Node {
public:
    CanvasNode();
    void setEditor(Editor* ed) { ed_ = ed; }
    void setTool(Tool* t) { tool_ = t; }
    void setInputSource(const InputFrame* in) { inputSrc_ = in; }
    void setPickMode(bool on) { pickMode_ = on; }

    Size requestSize() const override { return Size{0, 0}; }
    void paint(SkCanvas& canvas) override;
    bool onPointer(const PointerEvent& e) override;
    bool onWheel(const WheelEvent& e) override;
    bool onKey(const KeyEvent& e) override;
    bool cursorAt(float x, float y, CursorShape& out) override;
    bool focusable() const override { return true; }    bool wantsAnimation() const override;

    void syncDocument();
    void syncOverlay();
    void autoScrollDrag(const InputFrame& in);

    static void toNative(uint32_t* px, std::size_t count);

protected:
    void onPlaced(const Rect& oldFrame, const Rect& oldClip) override;

private:
    void syncViewport();
    int lastDocRev_ = -1;
    int lastDocStructRev_ = -1;
    int lastDocW_ = -1;
    int lastDocH_ = -1;
    void drawDocumentLayers(SkCanvas& canvas);
    bool pictureValid() const;
    void rebuildPicture();
    void recordOverlay(Overlay* ov) const;
    void recordCanvasBorder(Overlay* ov) const;
    void drawAnts(Overlay* ov) const;

    Editor* ed_ = nullptr;
    Tool* tool_ = nullptr;
    const InputFrame* inputSrc_ = nullptr;

    struct TexCacheEntry {
        int id = -1;
        int rev = -1;
        const void* data = nullptr;
        int w = 0;
        int h = 0;
        sk_sp<SkImage> img;
        sk_sp<SkImage> minified;
    };
    std::vector<TexCacheEntry> tex_;
    const void* texDoc_ = nullptr;

    struct MaskTexEntry {
        int id = -1;
        int rev = -1;
        int w = 0, h = 0;
        sk_sp<SkImage> img;
    };
    std::vector<MaskTexEntry> mtex_;
    sk_sp<SkImage> maskOverlayImage(const Layer& l, int index);

    sk_sp<SkPicture> pic_;
    int picRev_ = -1;
    int picStructRev_ = -1;
    int picW_ = 0;
    int picH_ = 0;
    float picZoom_ = 0.0f;
    float picPanX_ = 0.0f;
    float picPanY_ = 0.0f;
    float picRot_ = 0.0f;
    float picScaleY_ = 1.0f;
    float picSkewX_ = 0.0f;

    Rect lastViewport_{};
    float lastZoom_ = 0.0f;
    float lastPanX_ = 0.0f, lastPanY_ = 0.0f;
    float lastRot_ = 0.0f, lastScaleY_ = 1.0f, lastSkewX_ = 0.0f;

    OverlayRecording ovShown_;
    OverlayRecording ovNext_;

    bool panning_ = false;
    int dragBtn_ = -1;
    bool panBySpace_ = false;
    bool pickMode_ = false;
    Vec2f panLast_{0, 0};
};

}
