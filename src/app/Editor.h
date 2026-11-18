#pragma once

#include "app/ViewState.h"
#include "core/Document.h"
#include "core/ImageIo.h"
#include "core/LocalWarp.h"
#include "core/History.h"
#include "core/Selection.h"
#include "core/Transform.h"
#include "core/Types.h"

#include <functional>
#include <string>
#include <vector>

namespace ie {

struct Palette {
    float fg[4] = {0.09f, 0.09f, 0.10f, 1.0f};
    float bg[4] = {1.00f, 1.00f, 1.00f, 1.00f};
    float swatch[8][4] = {
        {0.09f, 0.09f, 0.10f, 1.f}, {1.f, 1.f, 1.f, 1.f}, {0.85f, 0.20f, 0.20f, 1.f}, {0.96f, 0.66f, 0.14f, 1.f},
        {0.30f, 0.72f, 0.36f, 1.f}, {0.22f, 0.53f, 0.90f, 1.f}, {0.55f, 0.36f, 0.86f, 1.f}, {0.35f, 0.35f, 0.38f, 1.f},
    };

    Color fgColor() const;
    Color bgColor() const;
    void swap();
    void reset();
    void setFg(const float* c);
    void setBg(const float* c);
};

class Editor {
public:
    Document doc;
    Selection sel;
    History hist;
    Palette palette;
    CanvasViewState view;

    std::string filePath;
    std::string title = "无标题";
    int meshDensity = 8;
    void setMeshDensity(int n);
    bool modified = false;
    imageio::SaveOptions saveOpts;
    bool pendingFit = true;
    bool composeLines = false;
    void toggleComposeLines()
    {
        composeLines = !composeLines;
    }
    bool canvasMenuRequested = false;
    float canvasMenuX = 0.0f, canvasMenuY = 0.0f;
    void requestCanvasMenu(float x, float y)
    {
        canvasMenuRequested = true;
        canvasMenuX = x;
        canvasMenuY = y;
    }
    bool takeCanvasMenuRequest(float& x, float& y)
    {
        if (!canvasMenuRequested) return false;
        canvasMenuRequested = false;
        x = canvasMenuX;
        y = canvasMenuY;
        return true;
    }

    bool textCommitRequested = false;
    bool textCancelRequested = false;
    void requestTextCommit() { textCommitRequested = true; }
    void requestTextCancel() { textCancelRequested = true; }
    bool takeTextCommit()
    {
        const bool v = textCommitRequested;
        textCommitRequested = false;
        return v;
    }
    bool takeTextCancel()
    {
        const bool v = textCancelRequested;
        textCancelRequested = false;
        return v;
    }

    void newDocument(int w, int h, Color bg);
    bool open(const std::string& path);
    bool saveTo(const std::string& path);
    bool saveTo(const std::string& path, const imageio::SaveOptions& opt);
    void afterEdit(const char* label);

    void copySelection();
    void cutSelection();
    void paste();
    void deleteSelection();
    void deleteByKey();
    void deleteSelectedLayers();
    void selectAll();
    void deselect();
    void invertSelection();
    void cropToSelection();
    void trimCanvas();
    void resizeCanvas(int nw, int nh, int ox, int oy);
    void resizeCanvasAligned(int nw, int nh, int ax, int ay);
    void resizeImage(int nw, int nh);
    void strokeSelection(int width, Color c);
    void applyBrightnessContrast(float brightness, float contrast);
    bool beginPixelPreview(std::string label);
    void previewPixels(const std::function<void(Image&, Rect)>& op);
    void commitPixelPreview();
    void cancelPixelPreview();
    bool pixelPreviewActive() const { return preview_.layerId >= 0; }
    void dropPixelPreviewSession();
    void dropPixelSegment();
    void applyInvert();
    void applyDesaturate();
    void applyThreshold(int level);
    void applyHueShift(int degrees);
    void mergeImageLayer();
    void addEmptyLayer();
    void swapPalette();


    enum class WandMode { Replace, Add, Subtract, Intersect };
    bool wandSelect(int x, int y, int tolerance, WandMode mode);

    enum class BucketFill { Foreground, Background, Pattern };
    bool bucketFill(int x, int y, int tolerance, BucketFill content);

    int addTextLayer(const TextData& t);
    void setTextContent(int index, const std::string& text);
    void applyTextStyle(int index, int size, float lineSpacing, float opacity, const float rgba[4]);
    void applyTextFont(int index, const std::string& family);
    void demoteTextLayer(int index);
    static std::string textLayerName(const TextData& t);

    bool beginTextSession(int index);
    int beginNewTextSession(const TextData& t);
    bool textSessionActive() const { return textSessionActive_; }
    bool updateTextEditing(const std::string& text);
    void commitTextSession();
    void cancelTextSession();

    void requestTextEdit(int layerIndex, const Vec2f& at);
    bool takeTextEditRequest(int& layerIndex, Vec2f& at);
    bool requestTextEditAt(float clientX, float clientY);

    Layer* editableLayer();

    bool activeHasContent() const;
    bool activeHasPixels() const;

    bool placeImageAsLayer(const std::string& path);
    bool placeOraAsLayers(const std::string& path);

    bool beginMeshWarp(int layer);
    void meshPress(const Vec2f& pt, float tol);
    void meshDrag(const Vec2f& pt);
    void meshRelease();
    bool meshWarpActive() const { return mw_.active; }
    bool meshWarpMoved() const { return mw_.active && mw_.moved; }
    int meshWarpCells() const { return mw_.active ? mw_.n : meshDensity; }
    int meshPointCount() const { return mw_.active ? (int)mw_.pts.size() : 0; }
    const std::vector<Vec2f>& meshPts() const { return mw_.pts; }
    Vec2f meshPoint(int i) const
    {
        if (!mw_.active || i < 0 || i >= (int)mw_.pts.size()) return Vec2f{};
        return mw_.pts[(size_t)i];
    }
    int meshDragIndex() const { return mw_.active ? mw_.dragIdx : -1; }
    int meshPinCount() const { return mw_.active ? (int)mw_.pins.size() : 0; }
    Vec2f meshPinAt(int i) const
    {
        if (!mw_.active || i < 0 || i >= (int)mw_.pins.size()) return Vec2f{};
        return mw_.pins[(size_t)i].cur;
    }
    bool meshPinMoved(int i) const
    {
        if (!mw_.active || i < 0 || i >= (int)mw_.pins.size()) return false;
        return mw_.pins[(size_t)i].cur.x != mw_.pins[(size_t)i].origin.x ||
               mw_.pins[(size_t)i].cur.y != mw_.pins[(size_t)i].origin.y;
    }
    void meshSurfaceData(std::vector<Vec2f>& ep, std::vector<Vec2f>& ed) const;
    int meshExtraCount() const { return mw_.active ? (int)mw_.extras.size() : 0; }
    Vec2f meshExtraAt(int i) const
    {
        if (!mw_.active || i < 0 || i >= (int)mw_.extras.size()) return Vec2f{};
        return mw_.extras[(size_t)i].pos;
    }
    bool meshExtraMoved(int i) const
    {
        if (!mw_.active || i < 0 || i >= (int)mw_.extras.size()) return false;
        const MeshExtra& e = mw_.extras[(size_t)i];
        return e.pos.x != e.start.x || e.pos.y != e.start.y;
    }
    bool meshDeletePinAt(const Vec2f& pt, float tol);
    bool meshHasPinAt(const Vec2f& pt, float tol) const;
    void commitMeshWarp();
    void cancelMeshWarp();
    bool undoMeshStep();
    bool redoMeshStep();
    int meshStepCount() const { return mw_.active ? (int)mw_.steps.size() : 0; }

    bool beginLocalWarp(int layer);
    void localWarpPress(const Vec2f& pt);
    void localWarpDrag(const Vec2f& pt);
    void localWarpRelease();
    bool localWarpScale(float factor);
    void commitLocalWarp();
    void cancelLocalWarp();
    bool localWarpActive() const { return lw_.active; }
    const Image& localWarpPreviewImage(int& x, int& y) const
    {
        x = lw_.outX;
        y = lw_.outY;
        return lw_.preview;
    }
    int localWarpLayerIndex() const { return lw_.layer; }
    Rect localWarpPreviewRect() const { return Rect{lw_.outX, lw_.outY, lw_.outW, lw_.outH}; }
    int localWarpRegionCount() const { return (int)lw_.regions.size(); }
    int localWarpActiveRegion() const { return lw_.activeRegion; }
    LocalWarpParams localWarpRegion() const
    {
        if (lw_.activeRegion >= 0 && lw_.activeRegion < (int)lw_.regions.size())
            return lw_.regions[(size_t)lw_.activeRegion];
        return LocalWarpParams{};
    }
    LocalWarpParams localWarpRegionAt(int i) const
    {
        if (i >= 0 && i < (int)lw_.regions.size()) return lw_.regions[(size_t)i];
        return LocalWarpParams{};
    }
    int localWarpActiveRegionAt(const Vec2f& docPt) const;
    void localWarpSetField(int field, double v);
    void localWarpCopyRegion();
    void localWarpPasteRegion();
    void localWarpDeleteRegion();
    bool localWarpContains(const Vec2f& docPt) const;
    double localWarpField(int field) const;
    bool localWarpMoved() const { return lw_.moved; }

    bool beginFreeTransform(int layer);
    void transformBeginGrab(int grab, const Vec2f& pt, bool freeCorner = false);
    void transformUpdateGrab(const Vec2f& cur, bool shift);
    void transformEndGrab();
    void transformNudge(float dx, float dy);
    void commitFreeTransform();
    void cancelFreeTransform();
    bool transforming() const { return ft_.active; }
    bool transformDragging() const { return ft_.active && ft_.dragging; }
    bool transformMoved() const { return ft_.moved; }
    void transformQuad(Vec2f out[4]) const;
    Vec2f transformCenter() const { return Vec2f{ft_.xf.cx, ft_.xf.cy}; }
    float transformRotation() const { return ft_.xf.rot; }
    Rect transformSourceRect() const { return ft_.srcRect; }
    SkRect transformFrameRect() const
    {
        if (!ft_.xfRect.isEmpty()) return ft_.xfRect;
        return SkRect::MakeXYWH((float)(ft_.oldX + ft_.srcRect.x), (float)(ft_.oldY + ft_.srcRect.y),
                                (float)ft_.srcRect.w, (float)ft_.srcRect.h);
    }

private:
    Vec2i copyOrigin_{0, 0};
    bool hasCopyOrigin_ = false;

    bool textSessionActive_ = false;
    bool textSessionWasNew_ = false;
    bool textSessionModifiedPrev_ = false;
    int textSessionLayerId_ = -1;
    TextData textSessionOriginal_;

    int textEditReqLayerId_ = -1;
    Vec2f textEditReqAt_{0.0f, 0.0f};

    struct PixelPreview {
        int layerId = -1;
        Rect cspace{};
        Rect roi{};
        bool committed = false;
    };
    PixelPreview preview_;
    std::vector<uint32_t> previewOrig_;
    struct PixelSegment {
        int layerId = -1;
        Rect roi{};
        std::vector<uint32_t> base;
    };
    PixelSegment segment_;

    int moveLayer_ = -1;
    int moveBaseX_ = 0, moveBaseY_ = 0;
    std::vector<int> moveGroupIds_;
    std::vector<int> moveGroupX_, moveGroupY_;
    int movePressHit_ = -1;
    int moveDx_ = 0, moveDy_ = 0;
    int moveLastDx_ = 0, moveLastDy_ = 0;
    bool moveActive_ = false;
    bool moveRecorded_ = false;
    bool moveDuplicate_ = false;
    bool moveTextLayer_ = false;
    bool moveTextByXf_ = false;
    SkMatrix moveTextXf_;
    Vec2f moveTextPos_{0.0f, 0.0f};
    float snapGuideX_ = -1.0f, snapGuideY_ = -1.0f;


    struct FreeTransformState {
        int layer = -1;
        Image base;
        Rect srcRect;
        XForm xf;
        int oldX = 0, oldY = 0;
        bool active = false;
        bool moved = false;
        int grab = 0;
        Vec2f grabPt{};
        XForm entry;
        XForm dragStart;
        bool dragging = false;
        bool previewed = false;
        SkRect xfRect{};
        SkMatrix baseXf;
        bool isText = false;
        bool freeCorner = false;
        int freeCornerIdx = 0;
        Vec2f freeQuad[4]{};
        double lastQuadMs = 0.0;
    };
    FreeTransformState ft_;

    struct MeshStep {
        int idx = -1;
        int kind = 0;
        Vec2f from{};
        Vec2f to{};
    };

    struct MeshExtra {
        Vec2f param{};
        Vec2f src{};
        Vec2f start{};
        Vec2f pos{};
    };

    struct MeshPin {
        Vec2f origin{};
        Vec2f cur{};
        int vertex = -1;
        float radius = 0.0f;
    };

    struct MeshWarpState {
        int layer = -1;
        bool active = false;
        Image base;
        Rect srcRect;
        int oldX = 0, oldY = 0;
        int n = 8;
        std::vector<Vec2f> pts;
        std::vector<Vec2f> basePts;
        std::vector<MeshPin> pins;
        std::vector<MeshExtra> extras;
        int dragIdx = -1;
        int dragPin = -1;
        int dragExtra = -1;
        bool moved = false;
        double lastMs = 0.0;
        Vec2f dragFrom{};
        Vec2f pressPt{};
        bool dragActive = false;
        std::vector<MeshStep> steps;
        std::vector<MeshStep> redoSteps;
    };
    MeshWarpState mw_;

    struct LocalWarpState {
        int layer = -1;
        bool active = false;
        Image base;
        int oldX = 0, oldY = 0;
        std::vector<LocalWarpParams> regions;
        int activeRegion = 0;
        bool moved = false;
        bool regionDrag = false;
        int dragMode = 0;
        Vec2f dragFrom{};
        Vec2f grabOffset{};
        Vec2f startRadius{};
        float startRot = 0.0f;
        float startAngle = 0.0f;
        bool applied = false;
        double lastMs = 0.0;
        Image preview;
        int outX = 0, outY = 0, outW = 0, outH = 0;
        uint32_t* outPix = nullptr;
        Rect lastRect{};
        std::vector<LocalWarpParams> prevRegions;
        std::vector<LocalWarpField> fields;
        std::vector<LocalWarpField> liveFields;
        std::vector<Rect> prevRects;
        int outBoxX = 0, outBoxY = 0;
    };
    LocalWarpState lw_;
    void localWarpPreview(bool highQuality);
    void rebuildMeshPts();
    int nearestMeshVertex(const Vec2f& pt) const;
    LocalWarpParams lwClip_;
    bool lwHasClip_ = false;

    void buildMeshGrid();
    void meshApply(bool highQuality);

    void transformApply();
    SkMatrix textXfForCurrent() const;

public:

    void undo();
    void redo();
    void setActiveLayer(int index);
    void toggleLayerSelection(int index);
    void extendLayerSelection(int index);
    void selectAllLayers();
    void moveSelectedLayers(int to);
    void mergeSelectedLayers();
    void setLayerVisible(int index, bool visible);
    void renameLayer(int index, const char* name);
    void duplicateLayer(int index);
    void removeLayer(int index);
    void moveLayer(int index, int to);
    void mergeLayerDown(int index);
    void beginLayerOpacityEdit();
    bool wholeLayerMoveActive() const { return moveActive_; }
    bool cancelMoveRestore();
    int importLayer(const Layer& src);
    void setLayerOpacity(int index, float opacity);
    void setLayerBlendMode(int index, BlendMode mode);
    void setLayerLockAlpha(int index, bool lock);
    void centerActiveLayer();
    void applyLayerSettings(int index, BlendMode blend, float opacity, bool lockAlpha, const LayerStyle& style);

    void beginLayerStyleEdit();

    void beginEditStep(const char* label);
    void endEditStep();

    LayerChild* triChild(int index);
    const LayerChild* triChild(int index) const;
    LayerChild& ensureTriChild(int index);
    void beginChildEdit(const char* label = "三色");
    bool applyLayerTri(int index, float r, float g, float b, bool keepLuma);
    bool setTriChildOn(int index, bool on);

    LayerChild* liftChild(int index);
    const LayerChild* liftChild(int index) const;
    LayerChild& ensureLiftChild(int index);
    bool applyLayerLift(int index, float strength, float radius, float sensitivity, float softness,
                        float maxGain);

    bool beginMove();

    int layerIndexAt(int x, int y) const;
    bool beginMoveAt(int x, int y, bool keepCurrent);
    int movePressHit() const { return movePressHit_; }
    void collapseMovePressTo(int index);
    void updateMove(int dx, int dy, bool duplicate);
    void endMove();
    void rotateCanvas(int mode);
    void rotateLayer(int mode);
    void clearSnapGuides()
    {
        snapGuideX_ = -1.0f;
        snapGuideY_ = -1.0f;
    }
    Vec2i snapMoveDelta(int dx, int dy);
    float snapGuideX() const { return snapGuideX_; }
    float snapGuideY() const { return snapGuideY_; }
    bool moving() const { return moveActive_; }
    Vec2i moveOffset() const { return Vec2i{moveDx_, moveDy_}; }
    bool dragBounds(Rect& out) const;

    Rect activeLayerBounds() const;
    int documentRevision() const { return doc.revision(); }

    void zoomIn();
    void zoomOut();
    void fitView();
    void actualPixels();
    void viewRotateBy(float deg);
    void viewResetTransform();

    bool hasSelectionPixels() const { return sel.active; }
    Rect selectionOrCanvas() const;

    bool canvasHas(float sx, float sy) const { return view.containsScreen(sx, sy); }
    Vec2f canvasImage(float sx, float sy) const { return view.toImage(sx, sy, doc.width, doc.height); }
    Vec2i canvasPixel(float sx, float sy) const { return view.pixelAt(sx, sy, doc.width, doc.height); }
    bool colorAtPixel(int ix, int iy, Color& out) const;

    void fitIfPending()
    {
        if (pendingFit && doc.valid()) {
            view.fit(doc.width, doc.height);
            pendingFit = false;
        }
    }

private:
    void markLayerDirty(int index);

public:
    void recordStep(const char* label);
    void recordSelectionChange(const Selection& before, const char* label);

private:
    bool stepOpen_ = false;
    bool stepRecorded_ = false;
    std::string stepLabel_;
};

}
