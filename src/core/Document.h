#pragma once

#include "core/Image.h"
#include "core/TextRender.h"

#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include <map>
#include "include/core/SkCanvas.h"
#include "core/Types.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ie {

enum class LayerKind { Pixel, Text };

enum class StrokePos { Outside, Center, Inside };

struct StyleGridKey {
    int pixRev = -1;
    unsigned xfHash = 0;
    int w = 0, h = 0, pad = 0;
    int ox = 0, oy = 0;
    bool operator==(const StyleGridKey& o) const
    {
        return pixRev == o.pixRev && xfHash == o.xfHash && w == o.w && h == o.h && pad == o.pad && ox == o.ox && oy == o.oy;
    }
    bool operator!=(const StyleGridKey& o) const { return !(*this == o); }
};

struct StyleStrokeKey {
    StyleGridKey g;
    int radius = 0;
    StrokePos pos = StrokePos::Outside;
    bool operator==(const StyleStrokeKey& o) const { return g == o.g && radius == o.radius && pos == o.pos; }
    bool operator!=(const StyleStrokeKey& o) const { return !(*this == o); }
};

struct StyleShadowKey {
    StyleGridKey g;
    int dx = 0, dy = 0;
    double blur = 0.0;
    bool operator==(const StyleShadowKey& o) const
    {
        return g == o.g && dx == o.dx && dy == o.dy && blur == o.blur;
    }
    bool operator!=(const StyleShadowKey& o) const { return !(*this == o); }
};

struct StyleCompositeKey {
    StyleGridKey g;
    bool strokeOn = false;
    int strokeRadius = 0;
    StrokePos strokePos = StrokePos::Outside;
    uint32_t strokeRGB = 0;
    uint32_t strokeA255 = 0;
    bool shadowOn = false;
    int shadowDx = 0, shadowDy = 0;
    double shadowBlur = 0.0;
    uint32_t shadowRGB = 0;
    uint32_t shadowA255 = 0;
    bool overlayOn = false;
    uint32_t overlayRGB = 0;
    double overlayT = 0.0;
    bool operator==(const StyleCompositeKey& o) const
    {
        return g == o.g && strokeOn == o.strokeOn && strokeRadius == o.strokeRadius &&
               strokePos == o.strokePos && strokeRGB == o.strokeRGB && strokeA255 == o.strokeA255 &&
               shadowOn == o.shadowOn && shadowDx == o.shadowDx && shadowDy == o.shadowDy &&
               shadowBlur == o.shadowBlur && shadowRGB == o.shadowRGB && shadowA255 == o.shadowA255 &&
               overlayOn == o.overlayOn && overlayRGB == o.overlayRGB && overlayT == o.overlayT;
    }
    bool operator!=(const StyleCompositeKey& o) const { return !(*this == o); }
};

enum class ChildKind { Tri, Lift };

struct LayerChild {
    ChildKind kind = ChildKind::Tri;
    int id = 0;
    bool on = true;

    float r = 1.0f, g = 1.0f, b = 1.0f;
    bool keepLuma = false;

    float liftStrength = 0.0f;
    float liftRadius = 0.0f;
    float liftSensitivity = 0.5f;
    float liftSoftness = 0.0f;
    float liftMaxGain = 3.0f;

    bool identity() const
    {
        if (kind == ChildKind::Tri) return r == 1.0f && g == 1.0f && b == 1.0f && !keepLuma;
        return liftStrength <= 0.0f || liftRadius <= 0.0f;
    }
    bool active() const { return id != 0 && on && !identity(); }
};

struct LayerStyle {
    bool strokeOn = false;
    float strokeWidth = 3.0f;
    StrokePos strokePos = StrokePos::Outside;
    float strokeOpacity = 100.0f;
    float strokeColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    bool shadowOn = false;
    float shadowDist = 6.0f;
    float shadowSize = 8.0f;
    float shadowAngle = 135.0f;
    float shadowOpacity = 100.0f;
    float shadowColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    bool overlayOn = false;
    float overlayOpacity = 100.0f;
    float overlayColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct Layer {
    int id = 0;
    std::string name;
    Image img;
    int x = 0, y = 0;
    bool visible = true;
    float opacity = 1.0f;
    BlendMode blend = BlendMode::Normal;
    bool lockAlpha = false;

    LayerKind kind = LayerKind::Pixel;
    TextData text;
    SkMatrix textXf;

    mutable Rect bboxCache{};
    mutable int bboxRev = -1;
    mutable int bboxX = 0, bboxY = 0;

    int pixRev = 0;

    std::vector<LayerChild> children;


    LayerStyle style;
    Rect styleClip{};
    mutable Image styleCache;
    mutable StyleCompositeKey styleKey;
    mutable int styleCacheOx = 0, styleCacheOy = 0;

    mutable std::vector<uint8_t> maskSrc, maskStroke, maskShadow;
    mutable int maskW = 0, maskH = 0, maskPad = 0;
    mutable int maskOx = 0, maskOy = 0;
    mutable StyleGridKey maskSrcKey;
    mutable StyleStrokeKey maskStrokeKey;
    mutable StyleShadowKey maskShadowKey;

    mutable Image liftCache;
    mutable int liftCacheRev = -1;
    mutable StyleCompositeKey liftCacheStyleKey;
    mutable float liftCacheParams[5] = {0.0f, 0.0f, 0.5f, 0.0f, 3.0f};
    mutable int liftCacheW = 0, liftCacheH = 0;

    mutable Image triCache;
    mutable int triCacheRev = -1;
    mutable StyleCompositeKey triCacheStyleKey;
    mutable float triCacheParams[3] = {1.0f, 1.0f, 1.0f};
    mutable bool triCacheKeep = false;
    mutable int triCacheW = 0, triCacheH = 0;

        std::vector<uint8_t> uMask;
    int uMaskW = 0, uMaskH = 0;
    int uMaskRev = 0;
};

inline uint32_t layerPixelAt(const Layer& l, int x, int y)
{
    const int lx = x - l.x, ly = y - l.y;
    const int w = l.img.width(), h = l.img.height();
    if (lx >= 0 && ly >= 0 && lx < w && ly < h) return l.img.pixels()[(size_t)ly * (size_t)w + (size_t)lx];
    return 0;
}

inline Rect layerRect(const Layer& l) { return Rect{l.x, l.y, l.img.width(), l.img.height()}; }

struct OpaqueReport {
    bool all = true;
    bool doc = true;
};

class Document {
public:
    int width = 0, height = 0;
    std::vector<Layer> layers;
    int active = -1;

    void createEmpty(int w, int h, Color bg);
    void adopt(int w, int h, Image image, std::string name);

    bool valid() const { return width > 0 && height > 0 && !layers.empty(); }
    Rect boundsRect() const { return Rect{0, 0, width, height}; }
    Layer* activeLayer();
    const Layer* activeLayer() const;
    int indexOfId(int id) const;

    Rect layerContentBounds(int index) const;
    Rect layerVisualBounds(int index) const;
    int revision() const { return revision_; }
    int structureRevision() const { return structureRev_; }
    void touchStructure() { ++structureRev_; }

    void addLayer(Image img, std::string name);
    void addLayerAt(Image img, int ox, int oy, std::string name);
    void addEmptyLayer(std::string name);
    void clampLayersState();
    int addTextLayer(const TextData& t, std::string name);
    void refreshTextLayers();
    void refreshTextLayer(int index);
    void invalidateTextLayers();
    void demoteTextLayer(int index);
    void removeLayer(int index);
    void duplicateLayer(int index);
    void moveLayer(int from, int to);
    void mergeDown(int index);
    bool bakeLayerEffects(int index);
    int nextLayerId() const;
    int nextChildId() const { return idCounter_; }
    int claimChildId() { const int id = idCounter_; ++idCounter_; return id; }
    void raiseIdsAbove(int maxId) { idCounter_ = std::max(idCounter_, maxId + 1); }

    std::vector<int> selIds;
    bool layerSelected(int index) const;
    void selectOnly(int index);
    void toggleSelect(int index);
    void selectRange(int index);
    void selectAll();
    void activateWithinSelection(int index);
    std::vector<int> selectedIndices() const;
    void moveLayers(const std::vector<int>& indices, int to);
    void mergeLayers(const std::vector<int>& indices);

    void markDirty(Rect r, int layerHint = -1);
    void markRecompose(Rect r);
    void bumpLayerRev(int index);
    void markAllDirty();
    Rect takeDirty();
    Rect pendingDirty() const { return pending_; }
    void clearDirty() { pending_ = Rect{}; }
    void recomposite(Rect r);
    const Image& compositeImage() const { return cache_; }

    void syncTextLayers();

    Rect layerCanvasRect(int index) const;

    void ensureLayerRect(int index, Rect need);

    bool hasUserMask(int index) const;
    bool ensureUserMask(int index);
    void applyUserMask(int index, int band = 0);
    void dropUserMask(int index);

    void compositeInto(uint32_t* dst, int stride, Rect r, int skip, OpaqueReport* info = nullptr) const
    {
        compositeRangeInto(dst, stride, r, 0, (int)layers.size(), skip, info);
    }

    void compositeScreenInto(uint32_t* dst, int stride, Rect r, OpaqueReport* info = nullptr) const
    {
        compositeRangeInto(dst, stride, r, 0, (int)layers.size(), -1, info, true);
    }

    void compositeRangeInto(uint32_t* dst, int stride, Rect r, int from, int to, int skip,
                            OpaqueReport* info = nullptr, bool skipText = false) const;

    SkRect layerDrawBounds(int index) const;

    void drawTextLayers(SkCanvas& canvas, const SkMatrix& docToDevice, int clipW, int clipH,
                        int onlyLayer = -1) const;
    bool textLayerHasOutline(int index) const;

    void resizeCanvas(int nw, int nh, int ox, int oy);
    void resizeImage(int nw, int nh);

    Image flatten() const;
    void drawTextOnto(Image& out, const SkMatrix& docToPix) const;

    Image flattenScaled(float s) const;

    Image flattenRegionScaled(float s, Rect region, bool includeText = true) const;

private:
    Image cache_;
    Rect pending_{};
    int idCounter_ = 1;
    bool textDirty_ = false;
    int revision_ = 0;
    int structureRev_ = 0;
    void ensureCache();
    struct TextPathEntry {
        unsigned long long key = 0;
        SkPath path;
    };
    mutable std::map<int, TextPathEntry> textPaths_;
    const SkPath* cachedTextPath(const Layer& l) const;
};

}
