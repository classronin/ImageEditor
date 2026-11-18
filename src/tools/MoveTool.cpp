#include "tools/Tools.h"
#include "nodes/Guides.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace ie {

namespace {
constexpr float kPi = 3.14159265358979f;

inline float distToSeg(const Vec2f& p, const Vec2f& a, const Vec2f& b)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

inline bool pointInQuad(const Vec2f& p, const Vec2f q[4])
{
    bool sign = false;
    for (int i = 0; i < 4; ++i) {
        const Vec2f& a = q[i];
        const Vec2f& b = q[(i + 1) % 4];
        const float cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (i == 0)
            sign = cross > 0;
        else if ((cross > 0) != sign)
            return false;
    }
    return true;
}

class MoveTool final : public Tool {
public:
    ToolId id() const override { return ToolId::Move; }
    const char* name() const override { return "移动"; }

    CursorShape cursor(const InputFrame& in) const override
    {
        if (ed_ && ed_->meshWarpActive()) {
            const Vec2f p = ed_->canvasImage(in.mouse.x, in.mouse.y);
            const float tol = meshTol(*ed_);
            for (int i = 0; i < ed_->meshPinCount(); ++i) {
                const Vec2f q = ed_->meshPinAt(i);
                if (std::fabs(p.x - q.x) <= tol && std::fabs(p.y - q.y) <= tol) return CursorShape::Move;
            }
            for (int i = 0; i < ed_->meshExtraCount(); ++i) {
                const Vec2f q = ed_->meshExtraAt(i);
                if (std::fabs(p.x - q.x) <= tol && std::fabs(p.y - q.y) <= tol) return CursorShape::Move;
            }
            return CursorShape::Arrow;
        }
        if (ed_ && ed_->localWarpActive()) return CursorShape::Cross;
        if (ed_ && ed_->transforming()) {
            const int g = grabAt(*ed_, in.mouse);
            switch (g) {
            case 1: case 3: return CursorShape::ResizeNWSE;
            case 2: case 4: return CursorShape::ResizeNESW;
            case 5: case 7: return CursorShape::ResizeNS;
            case 6: case 8: return CursorShape::ResizeEW;
            case 9: return CursorShape::Rotate;
            default: return CursorShape::Move;
            }
        }
        return CursorShape::Arrow;
    }
    bool busy() const override
    {
        return dragging_ || (ed_ && (ed_->transforming() || ed_->meshWarpActive()));
    }
    bool acceptsButton(int b) const override
    {
        if (ed_ && ed_->meshWarpActive()) return b == 0;
        return (ed_ && ed_->transforming()) ? (b == 0 || b == 1) : (b == 0);
    }

    bool onWheel(Editor& e, const InputFrame& in) override
    {
        if (!e.localWarpActive()) return false;
        const Vec2f pt = e.canvasImage(in.mouse.x, in.mouse.y);
        const int hit = e.localWarpActiveRegionAt(pt);
        if (hit < 0) return false;
        const LocalWarpParams p = e.localWarpRegionAt(hit);
        const float rad = p.rotDeg * 3.14159265f / 180.0f;
        const float ca = std::cos(rad), sa = std::sin(rad);
        const float dx = pt.x - p.center.x, dy = pt.y - p.center.y;
        const float lx = dx * ca + dy * sa;
        const float ly = -dx * sa + dy * ca;
        const float nx = lx / (p.radius.x > 1.0f ? p.radius.x : 1.0f);
        const float ny = ly / (p.radius.y > 1.0f ? p.radius.y : 1.0f);
        const float r = std::sqrt(nx * nx + ny * ny);
        if (r > 1.25f) return false;
        const float factor = std::pow(1.1f, in.wheel > 0.0f ? 1.0f : -1.0f);
        e.localWarpScale(factor);
        return true;
    }

    void onEnter(Editor& e) override
    {
        ed_ = &e;
        dragging_ = false;
        e.endMove();
    }
    void onLeave(Editor& e) override
    {
        if (dragging_) e.endMove();
        dragging_ = false;
        started_ = false;
        ed_ = nullptr;
    }

    void onPress(Editor& e, const InputFrame& in, int btn) override
    {
        if (btn != 0) return;

        if (e.meshWarpActive()) {
            e.meshPress(e.canvasImage(in.mouse.x, in.mouse.y), meshTol(e));
            return;
        }

        if (e.localWarpActive()) {
            e.localWarpPress(e.canvasImage(in.mouse.x, in.mouse.y));
            return;
        }

        if (e.transforming()) {
            const int g = grabAt(e, in.mouse);
            if (g < 0) {
                e.commitFreeTransform();
                return;
            }
            e.transformBeginGrab(g, e.canvasImage(in.mouse.x, in.mouse.y), in.ctrl);
            return;
        }

        const Vec2i ip0 = e.view.pixelAt(in.mouse.x, in.mouse.y, e.doc.width, e.doc.height);
        if (in.ctrl && !in.doubleClick) {
            const int hit = e.layerIndexAt(ip0.x, ip0.y);
            if (hit < 0) {
                return;
            }
            e.toggleLayerSelection(hit);
        }

        if (in.doubleClick) {
            const Vec2i ip = e.view.pixelAt(in.mouse.x, in.mouse.y, e.doc.width, e.doc.height);
            const int hit = e.layerIndexAt(ip.x, ip.y);
            if (hit < 0) {
                return;
            }
            if (e.doc.layers[(size_t)hit].kind == LayerKind::Text) {
                e.requestTextEdit(hit, e.canvasImage(in.mouse.x, in.mouse.y));
                return;
            }
            e.beginFreeTransform(hit);
            return;
        }

        const Vec2i ip = ip0;
        if (!e.beginMoveAt(ip.x, ip.y, in.ctrl)) return;

        duplicate_ = in.alt;
        start_ = e.canvasImage(in.mouse.x, in.mouse.y);
        dragging_ = true;
        started_ = false;
    }

    void onDrag(Editor& e, const InputFrame& in, int btn) override
    {
        if (e.meshWarpActive()) {
            if (btn == 0) e.meshDrag(e.canvasImage(in.mouse.x, in.mouse.y));
            return;
        }
        if (e.localWarpActive()) {
            if (btn == 0) e.localWarpDrag(e.canvasImage(in.mouse.x, in.mouse.y));
            return;
        }
        if (e.transforming()) {
            if (btn != 0 || !e.transformDragging()) return;
            e.transformUpdateGrab(e.canvasImage(in.mouse.x, in.mouse.y), in.shift);
            return;
        }
        if (!dragging_ || btn != 0) return;

        const Vec2f p = e.canvasImage(in.mouse.x, in.mouse.y);
        int dx = (int)std::lround(p.x - start_.x);
        int dy = (int)std::lround(p.y - start_.y);
        if (in.shift) {
            if (std::abs(dx) >= std::abs(dy))
                dy = 0;
            else
                dx = 0;
        }
        if (!started_ && (dx != 0 || dy != 0)) started_ = true;
        if (started_) {
            if (in.ctrl) {
                const Vec2i s = e.snapMoveDelta(dx, dy);
                dx = s.x;
                dy = s.y;
            } else {
                e.clearSnapGuides();
            }
            e.updateMove(dx, dy, duplicate_);
        }
    }

    void onRelease(Editor& e, const InputFrame& in, int) override
    {
        if (e.meshWarpActive()) {
            e.meshRelease();
            return;
        }
        if (e.localWarpActive()) {
            e.localWarpRelease();
            return;
        }
        if (e.transforming()) {
            e.transformEndGrab();
            return;
        }
        if (!dragging_) return;
        const int pressHit = e.movePressHit();
        const Vec2i off = e.moveOffset();
        if (!started_ && pressHit >= 0) e.collapseMovePressTo(pressHit);
        e.endMove();
        dragging_ = false;
        started_ = false;
        if (off.x != 0 || off.y != 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "已移动 %+d, %+d px", off.x, off.y);
        }
        (void)in;
    }

    void onKey(Editor& e, const InputFrame& in) override
    {
        if (e.meshWarpActive()) {
            if (in.key == KeyEnter) {
                e.commitMeshWarp();
                return;
            }
            if (in.key == KeyEsc) {
                e.cancelMeshWarp();
                return;
            }
            return;
        }
        if (e.localWarpActive()) {
            if (in.key == KeyEnter) {
                e.commitLocalWarp();
                return;
            }
            if (in.key == KeyEsc) {
                e.cancelLocalWarp();
                return;
            }
            if (in.ctrl && in.key == KeyC) {
                if (FILE* f = std::fopen("lwkeys.log", "a")) { std::fprintf(f, "tool-copy\n"); std::fclose(f); }
                e.localWarpCopyRegion();
                return;
            }
            if (in.ctrl && in.key == KeyV) {
                if (FILE* f = std::fopen("lwkeys.log", "a")) { std::fprintf(f, "tool-paste\n"); std::fclose(f); }
                e.localWarpPasteRegion();
                return;
            }
            if (in.key == KeyDelete) {
                e.localWarpDeleteRegion();
                return;
            }
            return;
        }
        if (e.transforming()) {
            if (in.key == KeyEnter) {
                e.commitFreeTransform();
                return;
            }
            if (in.key == KeyEsc) {
                e.cancelFreeTransform();
                return;
            }
            int dx = 0, dy = 0;
            switch (in.key) {
            case KeyLeft: dx = -1; break;
            case KeyRight: dx = 1; break;
            case KeyUp: dy = -1; break;
            case KeyDown: dy = 1; break;
            default: return;
            }
            const int step = in.shift ? 10 : 1;
            e.transformNudge((float)(dx * step), (float)(dy * step));
            return;
        }
        if (dragging_) return;
        int dx = 0, dy = 0;
        switch (in.key) {
        case KeyLeft: dx = -1; break;
        case KeyRight: dx = 1; break;
        case KeyUp: dy = -1; break;
        case KeyDown: dy = 1; break;
        default: return;
        }
        const int step = in.shift ? 10 : 1;
        if (!e.beginMove()) return;
        e.updateMove(dx * step, dy * step, false);
        e.endMove();
    }

    void props(Props& p, Editor& e) override
    {
        if (e.meshWarpActive()) {
            p.header("网络变形");
            return;
        }
        if (e.localWarpActive()) {
            p.header("局部变形");
            return;
        }
        if (e.transforming()) {
            p.header("自由变换");
            char buf[128];
            const float deg = e.transformRotation() * 180.0f / kPi;
            std::snprintf(buf, sizeof(buf), "旋转 %.1f°", deg);
            p.label(buf);
            return;
        }
    }

    void overlay(Overlay* dl, const Editor& e, const CanvasViewState& v, const InputFrame& in) override
    {
        if (e.meshWarpActive()) {
            paintMeshGrid(dl, e, v);
            return;
        }
        if (e.localWarpActive()) {
            paintLocalWarp(dl, e, v, in);
            return;
        }
        if (!e.transforming()) return;
        const int W = e.doc.width, H = e.doc.height;
        Vec2f cq[4];
        e.transformQuad(cq);
        Vec2f q[4], mid[4];
        for (int i = 0; i < 4; ++i) {
            const Vec2f s = v.toScreen(cq[i].x, cq[i].y, W, H);
            q[i] = Vec2f(s.x, s.y);
        }
        for (int i = 0; i < 4; ++i)
            mid[i] = Vec2f((q[i].x + q[(i + 1) % 4].x) * 0.5f, (q[i].y + q[(i + 1) % 4].y) * 0.5f);

        dl->polyline(q, 4, IE_COL32(255, 255, 255, 205), 1.4f, true);

        const Vec2f c = v.toScreen(e.transformCenter().x, e.transformCenter().y, W, H);
        dl->line(c.x - 4, c.y, c.x + 4, c.y, IE_COL32(255, 255, 255, 180), 1.0f);
        dl->line(c.x, c.y - 4, c.x, c.y + 4, IE_COL32(255, 255, 255, 180), 1.0f);

        const Vec2f dir(mid[0].x - c.x, mid[0].y - c.y);
        const float len = std::hypot(dir.x, dir.y);
        if (len > 1.0f) {
            const Vec2f u(dir.x / len, dir.y / len);
            const Vec2f tip(mid[0].x + u.x * 18.0f, mid[0].y + u.y * 18.0f);
            dl->line(mid[0].x, mid[0].y, tip.x, tip.y, IE_COL32(255, 255, 255, 150), 1.0f);
            dl->circle(tip.x, tip.y, 3.0f, IE_COL32(255, 255, 255, 150), 1.0f);
        }

        const Color32 fill = IE_COL32(255, 255, 255, 255);
        const Color32 border = IE_COL32(38, 40, 48, 255);
        Vec2f u(q[1].x - q[0].x, q[1].y - q[0].y);
        const float ul = std::hypot(u.x, u.y);
        const Vec2f ax = ul > 1e-4f ? Vec2f(u.x / ul, u.y / ul) : Vec2f(1.0f, 0.0f);
        const Vec2f ay(-ax.y, ax.x);
        auto handle = [&](const Vec2f& p) {
            const float h = 4.0f;
            Vec2f s[5];
            s[0] = Vec2f(p.x + ax.x * h + ay.x * h, p.y + ax.y * h + ay.y * h);
            s[1] = Vec2f(p.x + ax.x * h - ay.x * h, p.y + ax.y * h - ay.y * h);
            s[2] = Vec2f(p.x - ax.x * h - ay.x * h, p.y - ax.y * h - ay.y * h);
            s[3] = Vec2f(p.x - ax.x * h + ay.x * h, p.y - ax.y * h + ay.y * h);
            s[4] = s[0];
            dl->polyline(s, 5, fill, 1.4f, false);
            dl->polyline(s, 5, border, 1.4f, false);
        };
        for (int i = 0; i < 4; ++i) handle(q[i]);
        for (int i = 0; i < 4; ++i) handle(mid[i]);
    }

private:
    static float meshTol(const Editor& e)
    {
        const float z = e.view.zoom > 0.05f ? e.view.zoom : 0.05f;
        const float t = 10.0f / z;
        return t < 2.0f ? 2.0f : t;
    }

    static void paintOneRegion(Overlay* dl, const CanvasViewState& v, const int W, const int H,
                               const LocalWarpParams& p, bool active)
    {
        const uint32_t ringCol = active ? IE_COL32(255, 255, 255, 220) : IE_COL32(190, 190, 190, 150);
        const float width = active ? 1.6f : 1.0f;
        Vec2f pts[64];
        const float a = p.rotDeg * 3.14159265f / 180.0f;
        const float ca = std::cos(a), sa = std::sin(a);
        for (int i = 0; i < 64; ++i) {
            const float th = (float)i * (2.0f * 3.14159265f / 64.0f);
            const float lx = std::cos(th) * p.radius.x;
            const float ly = std::sin(th) * p.radius.y;
            const float dx = lx * ca - ly * sa;
            const float dy = lx * sa + ly * ca;
            pts[i] = v.toScreen(p.center.x + dx, p.center.y + dy, W, H);
        }
        dl->polyline(pts, 64, ringCol, width, true);
        const Vec2f c = v.toScreen(p.center.x, p.center.y, W, H);
        dl->circle(c.x, c.y, 3.0f, ringCol, 1.0f);
        if (!active) return;
        const Vec2f hx{p.radius.x * ca, p.radius.x * sa};
        const Vec2f hy{-p.radius.y * sa, p.radius.y * ca};
        const Vec2f hs[4]{{p.center.x + hx.x, p.center.y + hx.y}, {p.center.x - hx.x, p.center.y - hx.y},
                          {p.center.x + hy.x, p.center.y + hy.y}, {p.center.x - hy.x, p.center.y - hy.y}};
        for (int i = 0; i < 4; ++i) {
            const Vec2f q = v.toScreen(hs[i].x, hs[i].y, W, H);
            dl->circle(q.x, q.y, 4.0f, IE_COL32(255, 255, 255, 220), 1.2f);
        }
    }

    static void paintLocalWarp(Overlay* dl, const Editor& e, const CanvasViewState& v, const InputFrame& in)
    {
        (void)in;
        const int W = e.doc.width, H = e.doc.height;
        const int n = e.localWarpRegionCount();
        const int act = e.localWarpActiveRegion();
        for (int i = 0; i < n; ++i) {
            const LocalWarpParams p = e.localWarpRegionAt(i);
            if (p.radius.x <= 1.0f || p.radius.y <= 1.0f) continue;
            paintOneRegion(dl, v, W, H, p, i == act);
        }
    }

    static void paintMeshGrid(Overlay* dl, const Editor& e, const CanvasViewState& v)
    {
        const int n = e.meshWarpCells();
        const int cnt = e.meshPointCount();
        if (n < 1 || cnt < (n + 1) * (n + 1)) return;
        const int W = e.doc.width, H = e.doc.height;
        const int res = 4;
        const int steps = n * res + 1;
        std::vector<Vec2f> row((size_t)steps);
        const uint32_t lineCol = IE_COL32(255, 255, 255, 150);
        const uint32_t dotCol = IE_COL32(255, 255, 255, 235);
        std::vector<Vec2f> ep, ed;
        e.meshSurfaceData(ep, ed);
        const int gN = e.meshWarpCells();
        for (int j = 0; j <= n; ++j) {
            for (int i = 0; i < steps; ++i) {
                const Vec2f p = meshC1Surface(e.meshPts(), gN, ep, ed, (float)i / (float)res, (float)j);
                const Vec2f s = v.toScreen(p.x, p.y, W, H);
                row[(size_t)i] = Vec2f(s.x, s.y);
            }
            dl->polyline(row.data(), steps, lineCol, 1.0f, false);
        }
        for (int i = 0; i <= n; ++i) {
            for (int j = 0; j < steps; ++j) {
                const Vec2f p = meshC1Surface(e.meshPts(), gN, ep, ed, (float)i, (float)j / (float)res);
                const Vec2f s = v.toScreen(p.x, p.y, W, H);
                row[(size_t)j] = Vec2f(s.x, s.y);
            }
            dl->polyline(row.data(), steps, lineCol, 1.0f, false);
        }
        (void)cnt;
        const uint32_t pinCol = IE_COL32(255, 214, 90, 255);
        const uint32_t pinRing = IE_COL32(255, 214, 90, 150);
        const int pins = e.meshPinCount();
        for (int i = 0; i < pins; ++i) {
            const Vec2f p = e.meshPinAt(i);
            const Vec2f s = v.toScreen(p.x, p.y, W, H);
            if (!e.meshPinMoved(i)) {
                const Vec2f d[5] = {Vec2f(s.x - 3.0f, s.y - 3.0f), Vec2f(s.x + 3.0f, s.y - 3.0f),
                                    Vec2f(s.x + 3.0f, s.y + 3.0f), Vec2f(s.x - 3.0f, s.y + 3.0f),
                                    Vec2f(s.x - 3.0f, s.y - 3.0f)};
                dl->polyline(d, 5, dotCol, 1.2f, false);
                continue;
            }
            const float r = 5.0f;
            Vec2f ring[13];
            for (int k = 0; k <= 12; ++k) {
                const float a = (float)k * 6.2831853f / 12.0f;
                ring[k] = Vec2f{s.x + std::cos(a) * r, s.y + std::sin(a) * r};
            }
            dl->polyline(ring, 13, pinRing, 1.2f, false);
            const Vec2f cross[5] = {Vec2f(s.x - 2.5f, s.y), Vec2f(s.x + 2.5f, s.y), Vec2f(s.x + 2.5f, s.y),
                                    Vec2f(s.x, s.y), Vec2f(s.x, s.y)};
            (void)cross;
            const Vec2f v1[2] = {Vec2f(s.x - 2.5f, s.y - 2.5f), Vec2f(s.x + 2.5f, s.y + 2.5f)};
            const Vec2f v2[2] = {Vec2f(s.x + 2.5f, s.y - 2.5f), Vec2f(s.x - 2.5f, s.y + 2.5f)};
            dl->polyline(v1, 2, pinCol, 1.4f, false);
            dl->polyline(v2, 2, pinCol, 1.4f, false);
        }
        const uint32_t exCol = IE_COL32(120, 220, 255, 255);
        const int exN = e.meshExtraCount();
        for (int i = 0; i < exN; ++i) {
            const Vec2f p = e.meshExtraAt(i);
            const Vec2f s = v.toScreen(p.x, p.y, W, H);
            const float r = e.meshExtraMoved(i) ? 5.0f : 3.0f;
            Vec2f ring[13];
            for (int k = 0; k <= 12; ++k) {
                const float a = (float)k * 6.2831853f / 12.0f;
                ring[k] = Vec2f{s.x + std::cos(a) * r, s.y + std::sin(a) * r};
            }
            dl->polyline(ring, 13, exCol, 1.4f, false);
        }
    }

    Editor* ed_ = nullptr;
    bool dragging_ = false;
    bool started_ = false;
    bool duplicate_ = false;
    Vec2f start_{};

    static int grabAt(const Editor& e, const Vec2f& m)
    {
        const int W = e.doc.width, H = e.doc.height;
        Vec2f cq[4];
        e.transformQuad(cq);
        Vec2f q[4], mid[4];
        for (int i = 0; i < 4; ++i) {
            const Vec2f s = e.view.toScreen(cq[i].x, cq[i].y, W, H);
            q[i] = Vec2f(s.x, s.y);
        }
        for (int i = 0; i < 4; ++i)
            mid[i] = Vec2f((q[i].x + q[(i + 1) % 4].x) * 0.5f, (q[i].y + q[(i + 1) % 4].y) * 0.5f);

        const float kHandle = 14.0f;
        const float kRot = 22.0f;
        const auto inBox = [&](const Vec2f& c, float k) {
            return std::fabs(m.x - c.x) <= k && std::fabs(m.y - c.y) <= k;
        };
        for (int i = 0; i < 4; ++i)
            if (inBox(q[i], kHandle)) return i + 1;
        for (int i = 0; i < 4; ++i)
            if (inBox(mid[i], kHandle * 0.8f)) return 5 + i;
        for (int i = 0; i < 4; ++i)
            if (distToSeg(m, q[i], q[(i + 1) % 4]) <= kRot) return 9;
        if (pointInQuad(m, q)) return 0;
        return 9;
    }

};

}

std::unique_ptr<Tool> makeMoveTool() { return std::make_unique<MoveTool>(); }

}
