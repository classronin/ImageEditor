#include "nodes/Panels.h"

#include "app/Editor.h"
#include "core/Blend.h"
#include "core/Primaries.h"
#include "host/Commands.h"
#include "host/Host.h"
#include "host/Shortcuts.h"
#include "nodes/ImagePanel.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "nodes/Style.h"
#include "nodes/Widgets.h"
#include "tools/Tool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ie {
namespace ui {

namespace {

static SkColor toSk(Color32 c)
{
    return SkColorSetARGB((c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255);
}

static Color32 withAlpha(Color32 c, int a)
{
    return (c & 0x00FFFFFFu) | ((Color32)(a & 255) << 24);
}

static void drawSelIcon(SkCanvas& c, SegIcon icon, const Rect& b, Color32 col)
{
    const float k = 16.0f;
    const float s = std::min(1.0f, (float)b.h / 22.0f);
    const float ox = (float)b.x + ((float)b.w - k * s) * 0.5f;
    const float oy = (float)b.y + ((float)b.h - k * s) * 0.5f;
    auto X = [&](float v) { return ox + v * s; };
    auto Y = [&](float v) { return oy + v * s; };

    SkPaint strokeP;
    strokeP.setAntiAlias(true);
    strokeP.setStyle(SkPaint::kStroke_Style);
    strokeP.setStrokeWidth(1.0f * s);
    strokeP.setColor(toSk(col));
    SkPaint fillP;
    fillP.setAntiAlias(true);
    fillP.setColor(toSk(withAlpha(col, 115)));

    SkPath outline;
    SkPath filled;
    switch (icon) {
        case SegIcon::New:
            outline.addRect(SkRect::MakeLTRB(X(2.5f), Y(2.5f), X(13.5f), Y(13.5f)));
            filled.addRect(SkRect::MakeLTRB(X(3.0f), Y(3.0f), X(13.0f), Y(13.0f)));
            break;
        case SegIcon::Add:
            outline.moveTo(X(1.5f), Y(1.5f));
            outline.lineTo(X(10.5f), Y(1.5f));
            outline.lineTo(X(10.5f), Y(5.5f));
            outline.lineTo(X(14.5f), Y(5.5f));
            outline.lineTo(X(14.5f), Y(14.5f));
            outline.lineTo(X(5.5f), Y(14.5f));
            outline.lineTo(X(5.5f), Y(10.5f));
            outline.lineTo(X(1.5f), Y(10.5f));
            outline.close();
            filled.moveTo(X(2.0f), Y(2.0f));
            filled.lineTo(X(10.0f), Y(2.0f));
            filled.lineTo(X(10.0f), Y(6.0f));
            filled.lineTo(X(14.0f), Y(6.0f));
            filled.lineTo(X(14.0f), Y(14.0f));
            filled.lineTo(X(6.0f), Y(14.0f));
            filled.lineTo(X(6.0f), Y(10.0f));
            filled.lineTo(X(2.0f), Y(10.0f));
            filled.close();
            break;
        case SegIcon::Sub:
            outline.addRect(SkRect::MakeLTRB(X(1.5f), Y(1.5f), X(10.5f), Y(10.5f)));
            outline.addRect(SkRect::MakeLTRB(X(5.5f), Y(5.5f), X(14.5f), Y(14.5f)));
            filled.moveTo(X(2.0f), Y(2.0f));
            filled.lineTo(X(10.0f), Y(2.0f));
            filled.lineTo(X(10.0f), Y(5.0f));
            filled.lineTo(X(5.0f), Y(5.0f));
            filled.lineTo(X(5.0f), Y(10.0f));
            filled.lineTo(X(2.0f), Y(10.0f));
            filled.close();
            break;
        case SegIcon::Inter:
            outline.addRect(SkRect::MakeLTRB(X(1.5f), Y(1.5f), X(10.5f), Y(10.5f)));
            outline.addRect(SkRect::MakeLTRB(X(5.5f), Y(5.5f), X(14.5f), Y(14.5f)));
            filled.addRect(SkRect::MakeLTRB(X(6.0f), Y(6.0f), X(10.0f), Y(10.0f)));
            break;
        default:
            return;
    }
    if (!filled.isEmpty()) c.drawPath(filled, fillP);
    if (!outline.isEmpty()) c.drawPath(outline, strokeP);
}


struct MenuItem {
    const char* label;
    Cmd cmd;
};

struct MenuDef {
    const char* title;
    std::vector<MenuItem> items;
    Cmd cmd = Cmd::None;
};

bool menuCmdEnabledImpl(Host& host, Cmd cmd)
{
    const Editor& ed = host.ed();
    const Document& d = ed.doc;
    if (cmd == Cmd::MergeSelected) return d.selectedIndices().size() >= 2;
    if (cmd == Cmd::MergeImage || cmd == Cmd::LayerRotate || cmd == Cmd::LayerCenter) return ed.activeHasContent();
    if (cmd == Cmd::MeshWarp || cmd == Cmd::LocalWarp)
        return ed.activeHasPixels();
    if (cmd == Cmd::FreeTransform)
        return ed.activeHasContent();
    if (cmd == Cmd::InvertColors || cmd == Cmd::Desaturate || cmd == Cmd::Threshold ||
        cmd == Cmd::BrightContrast || cmd == Cmd::HueShift)
        return ed.activeHasPixels();
    return true;
}

const std::vector<MenuDef>& menus()
{
    static const std::vector<MenuDef> m = {
        {"文件",
         {{"新建", Cmd::NewDoc},
           {"打开", Cmd::Open},
          {"保存", Cmd::Save},
          {"另存为", Cmd::SaveAs},
          {"导出", Cmd::Export},
          {"-", Cmd::None},
          {"快捷键", Cmd::Shortcuts},
          {"退出", Cmd::Quit}}},
        {"编辑",
         {{"全选", Cmd::SelectAll},
          {"取消选择", Cmd::Deselect},
          {"反选", Cmd::InvertSelection}}},
        {"图像",
         {{"图像大小", Cmd::ImageSize},
          {"画布大小", Cmd::CanvasSize},
          {"裁切透明", Cmd::TrimCanvas},
          {"-", Cmd::None},
          {"图像旋转", Cmd::ImageRotate},
          {"反相", Cmd::InvertColors},
          {"去色", Cmd::Desaturate}}},
        {"图层",
         {{"合并图层", Cmd::MergeSelected},
          {"合并图像", Cmd::MergeImage},
          {"图层旋转", Cmd::LayerRotate},
          {"图层居中", Cmd::LayerCenter},
          {"网络变形", Cmd::MeshWarp},
          {"局部变形", Cmd::LocalWarp},
          {"变形", Cmd::FreeTransform}}},
        {"视图",
         {{"构图线", Cmd::ComposeLines},
          {"-", Cmd::None},
          {"适应窗口", Cmd::FitView},
          {"实际像素", Cmd::ActualPixels}}},
        {"作者", {}, Cmd::About},
    };
    return m;
}

const std::vector<MenuItem>& canvasItems()
{
    static const std::vector<MenuItem> m = {
        {"取消选择", Cmd::Deselect},
        {"全选", Cmd::SelectAll},
        {"反选", Cmd::InvertSelection},
        {"合并图像", Cmd::MergeImage},
        {"图层居中", Cmd::LayerCenter},
        {"变形", Cmd::FreeTransform},
    };
    return m;
}

int menuItemHeight() { return 24; }
int menuWidth() { return 100; }

class PropsView {
public:
    void sync(Host& host)
    {
        host_ = &host;
        Tool& t = host.tool();
        const ToolId id = t.id();
        const bool identity = &t != tool_ || &host.ed() != ed_ || id != toolId_ || props_.empty() ||
                              activeLayer_ != host.ed().doc.active;
        if (identity || needRebuild_) {
            const int keepActive = active_;
            needRebuild_ = false;
            activeLayer_ = host.ed().doc.active;
            props_.clear();
            t.props(props_, host.ed());
            if (!showToolName_ && !props_.empty() && props_[0].kind == PropKind::Header)
                props_.erase(props_.begin());
            tool_ = &t;
            ed_ = &host.ed();
            toolId_ = id;
            active_ = identity ? -1 : keepActive;
            ++rev_;
        }
    }

    bool empty() const { return props_.empty(); }

    bool numEditing() const { return num_.editing; }

    bool cancelNumEdit()
    {
        if (!num_.editing) return false;
        num_.cancelEdit();
        sess_.clear();
        needRebuild_ = true;
        if (propStepOpen_ && host_) {
            host_->ed().endEditStep();
            propStepOpen_ = false;
        }
        return true;
    }

    static Rect buttonBody(const Rect& row, const Prop& p)
    {
        int rw = 0;
        if (p.right && *p.right) {
            const int tw = (int)ui::textWidth(p.right) + 10;
            const int need = (int)ui::textWidth(p.label) + 16;
            if (row.w - tw >= need) rw = tw;
            return Rect{row.x, row.y, std::max(1, row.w - rw), row.h};
        }
        const int w = std::min(row.w, (int)ui::textWidth(p.label) + 24);
        return Rect{row.x + (row.w - w) / 2, row.y, std::max(1, w), row.h};
    }

    Rect comboBoxRect(const Rect& row) const
    {
        const int lw = labelCol_;
        return Rect{row.x + lw, row.y, std::max(1, row.w - lw), row.h};
    }

    Rect boolBox(const Rect& r) const { return ui::checkBoxIn(r); }
    void segBoxes(const Rect& row, int n, std::vector<Rect>& out) const
    {
        out.clear();
        if (n <= 0) return;
        if (!narrowMode_) {
            const int gapX = 6;
            const int bw = std::max(1, (row.w - gapX * (n - 1)) / n);
            for (int i = 0; i < n; ++i) out.push_back(Rect{row.x + i * (bw + gapX), row.y, bw, row.h});
            return;
        }
        const int gapY = 4;
        const int bh = std::max(1, (row.h - gapY * (n - 1)) / n);
        for (int i = 0; i < n; ++i) out.push_back(Rect{row.x, row.y + i * (bh + gapY), row.w, bh});
    }
    static int segHeight(const Prop& p, int narrow)
    {
        const int unit = narrow ? 26 : theme().rowH;
        const int n = std::max(1, p.itemCount);
        if (!narrow) return unit;
        return unit * n + 4 * (n - 1);
    }
    Rect colorSwatch(const Rect& r) const
    {
        return Rect{r.x + labelCol_, r.y + 3, 40, r.h - 6};
    }

    void setShowToolName(bool on) { showToolName_ = on; }

    bool clearHover()
    {
        if (hover_ < 0 && hoverSegProp_ < 0) return false;
        hover_ = -1;
        hoverSegProp_ = -1;
        hoverSegK_ = -1;
        return true;
    }

    bool pointer(Host& host, const PointerEvent& e, const Rect& area)
    {
        sync(host);
        ensureLayout(area);
        std::vector<std::pair<int, Rect>>& items = layoutCache_;

        if (!e.pressed && !e.released && !e.down) {
            int hov = -1;
            int segProp = -1;
            int segK = -1;
            for (auto& it : items) {
                const Prop& p = props_[(size_t)it.first];
                if (p.kind == PropKind::Button) {
                    if (buttonBody(it.second, p).contains((int)e.pos.x, (int)e.pos.y)) {
                        hov = it.first;
                        break;
                    }
                } else if (p.kind == PropKind::Seg) {
                    std::vector<Rect> boxes;
                    segBoxes(it.second, p.itemCount, boxes);
                    for (int k = 0; k < (int)boxes.size(); ++k)
                        if (boxes[(size_t)k].contains((int)e.pos.x, (int)e.pos.y)) {
                            segProp = it.first;
                            segK = k;
                            break;
                        }
                    if (segProp >= 0) break;
                }
            }
            if (hov != hover_ || segProp != hoverSegProp_ || segK != hoverSegK_) {
                hover_ = hov;
                hoverSegProp_ = segProp;
                hoverSegK_ = segK;
                if (host_) host_->markChromeDirty();
            }
        }

        if (e.released && propStepOpen_ && !num_.editing) {
            host.ed().endEditStep();
            propStepOpen_ = false;
        }

        if (sess_.busy() && sess_.index >= 0 && sess_.index < (int)props_.size()) {
            Rect row{};
            bool haveRow = false;
            for (auto& it : items)
                if (it.first == sess_.index) {
                    row = it.second;
                    haveRow = true;
                }
            if (haveRow) {
                const Prop& p = props_[(size_t)sess_.index];
                bindNum(p, sess_.index);
                if (sess_.route(e, numBoxRect(row))) {
                    needRebuild_ = true;
                    return true;
                }
            }
        }

        for (auto& it : items) {
            const Prop& p = props_[(size_t)it.first];
            Rect hitR = it.second;
            if (p.kind == PropKind::Bool) hitR = boolBox(it.second);
            else if (p.kind == PropKind::Color) hitR = colorSwatch(it.second);
            else if (p.kind == PropKind::Button) hitR = buttonBody(it.second, p);
            else if (p.kind == PropKind::Combo) hitR = comboBoxRect(it.second);
            if (p.kind == PropKind::Seg) {
                std::vector<Rect> boxes;
                segBoxes(it.second, p.itemCount, boxes);
                int pick = -1;
                for (int k = 0; k < (int)boxes.size(); ++k)
                    if (boxes[(size_t)k].contains((int)e.pos.x, (int)e.pos.y)) {
                        pick = k;
                        break;
                    }
                if (pick < 0) continue;
                if (e.pressed) {
                    active_ = it.first;
                    if (p.setV) p.setV((double)pick, p.setUd);
                    needRebuild_ = true;
                }
                return true;
            }
            if (!hitR.contains((int)e.pos.x, (int)e.pos.y)) continue;
            const int i = it.first;
            if (e.pressed) {
                active_ = i;
                if (p.kind == PropKind::Bool && p.b) {
                    emitBool(p);
                    needRebuild_ = true;
                } else if (p.kind == PropKind::Button && p.action) {
                    p.action(p.ud);
                    needRebuild_ = true;
                } else if (p.kind == PropKind::Combo && p.sel) {
                    const Rect box = comboBoxRect(it.second);
                    openCombo(host, p, (float)box.x, (float)box.bottom());
                    Prop c = p;
                    host.armList(box, [c, &host](int dir) {
                        if (!c.sel || c.itemCount <= 0) return;
                        const int cur = *c.sel;
                        const int next = std::clamp(cur + dir, 0, c.itemCount - 1);
                        if (next == cur || !c.setV) return;
                        c.setV((double)next, c.setUd);
                        host.markChromeDirty();
                    });
                } else if (p.kind == PropKind::Color && p.col) {
                    openColor(host, p, it.second.x, it.second.y + it.second.h);
                } else if (p.kind == PropKind::Float || p.kind == PropKind::Int) {
                    if (!propStepOpen_) {
                        host.ed().beginEditStep(p.label);
                        propStepOpen_ = true;
                    }
                    beginNum(p, i);
                    num_.pointer(e, numBoxRect(it.second));
                }
                return true;
            }
            if (e.released) {
                active_ = -1;
                return true;
            }
            if ((p.kind == PropKind::Float || p.kind == PropKind::Int) && active_ == i) {
                bindNum(p, i);
                if (num_.pointer(e, numBoxRect(it.second))) {
                    needRebuild_ = true;
                    return true;
                }
            }
            return true;
        }
        return false;
    }

    void paint(SkCanvas& c, const Rect& area, Host& host) const
    {
        const Theme& t = theme();
        if (props_.empty()) return;
        ensureLayout(area);
        std::vector<std::pair<int, Rect>>& items = layoutCache_;
        for (auto& it : items) {
            const Prop& p = props_[(size_t)it.first];
            const Rect r = it.second;
            if (p.kind == PropKind::Header || p.kind == PropKind::Label) {
                drawText(c, (float)r.x, (float)(r.y + (r.h - (int)lineHeight()) / 2), theme().dim, p.label);
                if (p.hint && *p.hint) {
                    const float tw = textWidth(p.label) + 8;
                    drawText(c, (float)r.x + tw, (float)(r.y + (r.h - (int)lineHeight()) / 2), theme().dim,
                             p.hint);
                }
            } else if (p.kind == PropKind::Separator) {
                fill(c, Rect{r.x + 4, r.y + r.h / 2, std::max(0, r.w - 8), 1}, t.border);
            } else if (p.kind == PropKind::Button) {
                const bool hot = hover_ == it.first;
                const Rect body = buttonBody(r, p);
                fill(c, body, hot ? t.hover : t.panelHi, (float)t.radius);
                stroke(c, body, hot ? t.accent : t.border, 1.0f, (float)t.radius);
                c.save();
                c.clipRect(SkRect::MakeXYWH((float)(body.x + 1), (float)body.y, (float)(body.w - 2),
                                            (float)body.h));
                const float tw = textWidth(p.label);
                const float tx = (p.right && *p.right) ? (float)(body.x + 8)
                                                       : (float)body.x + ((float)body.w - tw) * 0.5f;
                drawText(c, tx, (float)(body.y + (body.h - (int)lineHeight()) / 2), t.text, p.label);
                c.restore();
                if (body.w < r.w && p.right && *p.right) {
                    const float tw = ui::textWidth(p.right);
                    drawText(c, (float)r.right() - tw, (float)(r.y + (r.h - (int)lineHeight()) / 2), t.dim,
                             p.right);
                }
            } else if (p.kind == PropKind::Bool) {
                Rect box = boolBox(r);
                fill(c, box, t.panelHi, 3.0f);
                controlLine(c, box, 3.0f);
                if (p.b && *p.b) fill(c, Rect{box.x + 3, box.y + 3, 8, 8}, t.accent, 2.0f);
                drawText(c, (float)(box.right() + 6), (float)(r.y + (r.h - (int)lineHeight()) / 2), t.text,
                         p.label);
            } else if (p.kind == PropKind::Float || p.kind == PropKind::Int) {
                drawText(c, (float)r.x, (float)(r.y + (r.h - (int)lineHeight()) / 2), t.dim, p.label);
                const bool editing = sess_.index == it.first && num_.editing;
                bindNum(p, it.first);
                num_.paint(c, numBoxRect(r), editing);
            } else if (p.kind == PropKind::Combo) {
                drawText(c, (float)r.x, (float)(r.y + (r.h - (int)lineHeight()) / 2), t.dim, p.label);
                const Rect box = comboBoxRect(r);
                fill(c, box, t.panelHi, (float)t.radius);
                stroke(c, box, host.listArmed(box) ? t.accent : t.border, 1.0f, (float)t.radius);
                const int sel = p.sel ? *p.sel : -1;
                const char* cur = (sel >= 0 && sel < p.itemCount) ? p.items[sel] : "";
                drawText(c, (float)(box.x + 6), (float)(box.y + (box.h - (int)lineHeight()) / 2), t.text, cur);
            } else if (p.kind == PropKind::Seg) {
                std::vector<Rect> boxes;
                segBoxes(r, p.itemCount, boxes);
                const int sel = p.sel ? *p.sel : -1;
                for (int k = 0; k < (int)boxes.size(); ++k) {
                    const Rect b = boxes[(size_t)k];
                    const bool on = k == sel;
                    const bool hot = hoverSegProp_ == it.first && hoverSegK_ == k;
                    fill(c, b, on ? t.accent : (hot ? t.hover : t.panelHi), (float)t.radius);
                    stroke(c, b, on ? t.accent : (hot ? t.accent : t.border), 1.0f, (float)t.radius);
                    if (p.icons && p.icons[k] != SegIcon::Text) {
                        drawSelIcon(c, p.icons[k], b, t.text);
                    } else {
                        const char* txt = k < p.itemCount ? p.items[k] : "";
                        const float tw = textWidth(txt);
                        drawText(c, (float)b.x + ((float)b.w - tw) * 0.5f,
                                 (float)(b.y + (b.h - (int)lineHeight()) / 2), t.text, txt);
                    }
                }
            } else if (p.kind == PropKind::Color) {
                drawText(c, (float)r.x, (float)(r.y + (r.h - (int)lineHeight()) / 2), t.dim, p.label);
                Rect sw = colorSwatch(r);
                if (p.col) {
                    Color32 col = rgba((int)(p.col[0] * 255), (int)(p.col[1] * 255), (int)(p.col[2] * 255), 255);
                    fill(c, Rect{sw.x + 2, sw.y + 2, sw.w - 4, sw.h - 4}, col, 2.0f);
                }
                controlLine(c, sw, 2.0f);
            }
        }
    }

private:
    static void emitBool(const Prop& p)
    {
        if (!p.setV) return;
        Prop c = p;
        c.action = nullptr;
        c.ud = nullptr;
        const bool cur = c.b && *c.b;
        c.setV(cur ? 0.0 : 1.0, c.setUd);
    }
    Rect numBoxRect(const Rect& row) const
    {
        const int lw = labelCol_;
        return Rect{row.x + lw, row.y + 2, std::max(1, row.w - lw), row.h - 4};
    }
    static double numReadF(const void* ud) { return (double)*(const float*)ud; }
    static double numReadI(const void* ud) { return (double)*(const int*)ud; }
    static void numWrite(void* ud, double v)
    {
        const Prop* p = (const Prop*)ud;
        if (!p || !p->setV) return;
        Prop c = *p;
        c.action = nullptr;
        c.ud = nullptr;
        c.setV(v, c.setUd);
    }
    void bindNum(const Prop& p, int idx) const
    {
        num_.get = (p.kind == PropKind::Float) ? numReadF : numReadI;
        num_.readUd = (p.kind == PropKind::Float) ? (const void*)p.f : (const void*)p.i;
        num_.set = numWrite;
        num_.writeUd = (void*)&props_[(size_t)idx];
        num_.lo = p.kind == PropKind::Float ? (double)p.lo : (double)p.ilo;
        num_.hi = p.kind == PropKind::Float ? (double)p.hi : (double)p.ihi;
        num_.step = p.kind == PropKind::Float ? (double)p.step : (double)p.istep;
        num_.fmt = p.kind == PropKind::Float ? p.fmt : nullptr;
    }

    void beginNum(const Prop& p, int idx)
    {
        sess_.take(idx, &num_);
        bindNum(p, idx);
    }

public:
    bool key(const KeyEvent& e, const Rect&) { return keyImpl(e); }

private:
    bool keyImpl(const KeyEvent& e)
    {
        if (!num_.editing) return false;
        if (sess_.index >= 0 && sess_.index < (int)props_.size()) bindNum(props_[(size_t)sess_.index], sess_.index);
        num_.key(e);
        if (!num_.editing) {
            needRebuild_ = true;
            sess_.clear();
            if (propStepOpen_ && host_) {
                host_->ed().endEditStep();
                propStepOpen_ = false;
            }
        }
        return true;
    }

public:
    void openCombo(Host& host, const Prop& p, float x, float y)
    {
        std::vector<std::string> items;
        for (int i = 0; i < p.itemCount; ++i) items.push_back(p.items[i]);
        Prop c = p;
        c.action = nullptr;
        c.ud = nullptr;
        host.openListPopup(items, c.sel ? *c.sel : 0, x, y, [c](int pick) {
            if (c.setV) c.setV((double)pick, c.setUd);
        });
    }

    void openColor(Host& host, const Prop& p, float x, float y)
    {
        (void)x;
        (void)y;
        if (!p.setColor) return;
        Prop c = p;
        c.action = nullptr;
        c.ud = nullptr;
        host.openDialogNode(makeColorPopup(host, c.col,
                                           [c](const float rgba[4]) {
                                               if (c.setColor) c.setColor(rgba, c.setUd);
                                           }));
    }

public:
    int contentHeight(const Rect& area) const
    {
        std::vector<std::pair<int, Rect>> tmp;
        Rect tall{area.x, area.y, area.w, 1 << 20};
        layout(tall, tmp);
        int bottom = area.y;
        for (auto& it : tmp) bottom = std::max(bottom, it.second.bottom());
        return bottom - area.y;
    }

private:
    void layoutInto(const Props& props, const Rect& area, std::vector<std::pair<int, Rect>>& out) const
    {
        const int padX = 8;
        int x = area.x + padX;
        const int rowW = std::max(60, area.w - padX * 2);
        const bool narrow = area.h > 40;
        int y = area.y + (narrow ? 6 : 4);
        const int h = narrow ? 26 : theme().rowH;
        labelCol_ = propsLabelCol(props, rowW);
        narrowMode_ = narrow;
        for (int i = 0; i < (int)props.size(); ++i) {
            if (narrow) x = area.x + padX;
            const Prop& p = props[(size_t)i];
            int w = 0;
            int hh = h;
            switch (p.kind) {
                case PropKind::Header: w = (int)textWidth(p.label) + 8; break;
                case PropKind::Label: w = (int)textWidth(p.label) + (int)textWidth(p.hint) + 16; break;
                case PropKind::Separator: w = narrow ? rowW : 12; break;
                case PropKind::Float:
                case PropKind::Int: w = 168; break;
                case PropKind::Bool: w = (int)textWidth(p.label) + 30; break;
                case PropKind::Combo: w = p.comboBoxW > 0 ? labelCol_ + p.comboBoxW : 172; break;
                case PropKind::Color: w = (int)textWidth(p.label) + 56; break;
                case PropKind::Button: w = rowW; break;
                case PropKind::Seg: {
                    if (p.icons) {
                        const int cell = 30;
                        if (narrow) w = cell;
                        else w = cell * p.itemCount + 6 * std::max(0, p.itemCount - 1);
                    } else if (narrow) {
                        int mx = 0;
                        for (int k = 0; k < p.itemCount; ++k)
                            mx = std::max(mx, (int)textWidth(p.items[k]) + 16);
                        w = mx;
                    } else {
                        int sum = 0;
                        for (int k = 0; k < p.itemCount; ++k) sum += (int)textWidth(p.items[k]) + 16;
                        w = sum + 6 * std::max(0, p.itemCount - 1);
                    }
                    hh = segHeight(p, narrow ? 1 : 0);
                    break;
                }
            }
            if (narrow && w > 0) w = std::min(w, rowW);
            if (narrow && p.kind == PropKind::Seg && w > 0) x = area.x + (area.w - w) / 2;
            if (!narrow && x + w > area.right() - 8 && w > 0) {
                x = area.x + 8;
                y += h + 4;
            }
            if (y + hh > area.bottom()) break;
            out.emplace_back(i, Rect{x, y, w, hh});
            if (narrow) y += hh + 4;
            else x += w + 8;
        }
    }

    void layout(const Rect& area, std::vector<std::pair<int, Rect>>& out) const
    {
        layoutInto(props_, area, out);
    }

public:
    int measureWidth(const Props& props) const
    {
        std::vector<std::pair<int, Rect>> tmp;
        layoutInto(props, Rect{0, 0, 100000, 34}, tmp);
        if (tmp.empty()) return 0;
        int right = 0;
        for (auto& it : tmp) right = std::max(right, it.second.right());
        return right + 8;
    }

    bool hitControl(float x, float y, const Rect& area) const
    {
        ensureLayout(area);
        for (auto& it : layoutCache_) {
            const Prop& p = props_[(size_t)it.first];
            Rect hitR = it.second;
            if (p.kind == PropKind::Bool) hitR = boolBox(it.second);
            else if (p.kind == PropKind::Color) hitR = colorSwatch(it.second);
            else if (p.kind == PropKind::Button) hitR = buttonBody(it.second, p);
            else if (p.kind == PropKind::Combo) hitR = comboBoxRect(it.second);
            if (p.kind == PropKind::Seg) {
                std::vector<Rect> boxes;
                segBoxes(it.second, p.itemCount, boxes);
                for (auto& b : boxes)
                    if (b.contains((int)x, (int)y)) return true;
                continue;
            }
            if (hitR.contains((int)x, (int)y)) return true;
        }
        return false;
    }

private:
    void ensureLayout(const Rect& area) const
    {
        if (layoutValid_ && layoutRev_ == rev_ && layoutArea_.x == area.x && layoutArea_.y == area.y &&
            layoutArea_.w == area.w && layoutArea_.h == area.h)
            return;
        layoutCache_.clear();
        layout(area, layoutCache_);
        layoutArea_ = area;
        layoutRev_ = rev_;
        layoutValid_ = true;
    }

    Props props_;
    mutable int labelCol_ = 0;
    mutable bool narrowMode_ = false;
    ToolId toolId_ = ToolId::Move;
    const Tool* tool_ = nullptr;
    const Editor* ed_ = nullptr;
    int activeLayer_ = -2;
    bool needRebuild_ = false;
    bool showToolName_ = true;
    bool propStepOpen_ = false;
    Host* host_ = nullptr;
    int rev_ = 0;
    mutable int layoutRev_ = -1;
    mutable bool layoutValid_ = false;
    mutable Rect layoutArea_{};
    mutable NumField num_;
    mutable NumSession sess_;
    mutable std::vector<std::pair<int, Rect>> layoutCache_;
    int active_ = -1;
    int hover_ = -1;
    int hoverSegProp_ = -1;
    int hoverSegK_ = -1;
};

class MenuBarNode : public Node {
public:
    explicit MenuBarNode(Host& h) : host_(h) {}

    Size requestSize() const override { return Size{0, dims::menuH}; }

    void paint(SkCanvas& c) override
    {
        const Theme& t = theme();
        fill(c, frame_, t.panel);
        fill(c, Rect{frame_.x, frame_.bottom() - 1, frame_.w, 1}, t.border);
        int x = frame_.x + 8;
        for (size_t i = 0; i < menus().size(); ++i) {
            const int w = (int)textWidth(menus()[i].title) + 24;
            Rect r{x, frame_.y, w, frame_.h};
            if (hover_ == (int)i) fill(c, r, t.hover);
            drawText(c, (float)(x + 12), (float)(frame_.y + (frame_.h - (int)lineHeight()) / 2), t.text,
                     menus()[i].title);
            x += w;
        }
    }

    bool onPointer(const PointerEvent& e) override
    {
        const int h = hitMenu((int)e.pos.x, (int)e.pos.y);
        if (h != hover_) {
            hover_ = h;
            markDirty(frame_);
        }
        const int openIdx = host_.barMenuIndex();
        const bool direct = h >= 0 && menus()[(size_t)h].cmd != Cmd::None;
        if (!e.pressed && !e.released && openIdx >= 0 && h >= 0 && h != openIdx && !direct) {
            host_.openMenu(h, (float)(frame_.x + menuOffset(h)), (float)frame_.bottom());
            return true;
        }
        if (e.pressed && hover_ >= 0) {
            if (menus()[(size_t)hover_].cmd != Cmd::None) {
                const Cmd cmd = menus()[(size_t)hover_].cmd;
                hover_ = -1;
                markDirty(frame_);
                host_.execute(cmd);
                return true;
            }
            host_.openMenu(hover_, (float)(frame_.x + menuOffset(hover_)), (float)frame_.bottom());
            return true;
        }
        return hover_ >= 0;
    }

private:
    int hitMenu(int x, int y) const
    {
        if (y < frame_.y || y >= frame_.bottom()) return -1;
        int xx = frame_.x + 8;
        for (size_t i = 0; i < menus().size(); ++i) {
            const int w = (int)textWidth(menus()[i].title) + 24;
            if (x >= xx && x < xx + w) return (int)i;
            xx += w;
        }
        return -1;
    }

    int menuOffset(int idx) const
    {
        int x = 8;
        for (int i = 0; i < idx; ++i) x += (int)textWidth(menus()[(size_t)i].title) + 24;
        return x;
    }

    Host& host_;
    int hover_ = -1;
};

class MenuPopupNode : public Node {
public:
    MenuPopupNode(Host& h, const std::vector<MenuItem>& items, int ownerMenu = -1)
        : host_(h), items_(items), ownerMenu_(ownerMenu)
    {
    }

    int barMenuIndex() const override { return ownerMenu_; }

    Size requestSize() const override { return Size{menuWidth(), 8 + (int)items_.size() * menuItemHeight()}; }
    bool focusable() const override { return true; }

    bool onKey(const KeyEvent& e) override
    {
        if (e.pressed && e.scancode == KeyEsc) requestClose();
        return true;
    }

    void paint(SkCanvas& c) override
    {
        const Theme& t = theme();
        fill(c, frame_, t.panel, 3.0f);
        stroke(c, frame_, t.border, 1.0f, 3.0f);
        stroke(c, Rect{frame_.x + 2, frame_.y + 2, frame_.w - 4, frame_.h - 4}, rgba(255, 255, 255, 90),
               1.0f, 2.0f);
        const auto& items = items_;
        int y = frame_.y + 4;
        for (size_t i = 0; i < items.size(); ++i) {
            const int h = menuItemHeight();
            Rect r{frame_.x + 2, y, frame_.w - 4, h};
            if (!std::strcmp(items[i].label, "-")) {
                fill(c, Rect{r.x + 6, r.y + h / 2, r.w - 12, 1}, t.border);
            } else {
                const bool on = menuCmdEnabledImpl(host_, items[i].cmd);
                if (hover_ == (int)i && on) fill(c, r, t.hover, 2.0f);
                drawText(c, (float)(r.x + 10), (float)(r.y + (h - (int)lineHeight()) / 2),
                         on ? t.text : t.dim, items[i].label);
            }
            y += h;
        }
    }

    bool onPointer(const PointerEvent& e) override
    {
        const bool inside = frame_.contains((int)e.pos.x, (int)e.pos.y);
        int idx = -1;
        if (inside) {
            idx = ((int)e.pos.y - frame_.y - 4) / menuItemHeight();
            if (idx >= (int)items_.size()) idx = -1;
        }
        if (idx != hover_) {
            hover_ = idx;
            markDirty(frame_);
        }
        if (e.pressed && inside && idx >= 0 && std::strcmp(items_[(size_t)idx].label, "-") &&
            menuCmdEnabledImpl(host_, items_[(size_t)idx].cmd)) {
            Host* hp = &host_;
            Cmd cmd = items_[(size_t)idx].cmd;
            if (cmd == Cmd::ImageRotate || cmd == Cmd::LayerRotate) {
                const bool image = cmd == Cmd::ImageRotate;
                const float px = (float)frame_.right() - 2.0f;
                const float py = (float)(frame_.y + 4 + idx * menuItemHeight() + menuItemHeight());
                hp->post([hp, px, py, image]() {
                    std::vector<std::string> sub = {"180度", "向右90度", "向左90度", "水平翻转", "垂直翻转"};
                    hp->replaceWithListPopup(sub, px, py, [hp, image](int pick) {
                        Cmd c = Cmd::None;
                        if (image) {
                            c = pick == 0   ? Cmd::ImageRotate180
                                : pick == 1 ? Cmd::ImageRotateCW
                                : pick == 2 ? Cmd::ImageRotateCCW
                                : pick == 3 ? Cmd::ImageFlipH
                                            : Cmd::ImageFlipV;
                        } else {
                            c = pick == 0   ? Cmd::LayerRotate180
                                : pick == 1 ? Cmd::LayerRotateCW
                                : pick == 2 ? Cmd::LayerRotateCCW
                                : pick == 3 ? Cmd::LayerFlipH
                                            : Cmd::LayerFlipV;
                        }
                        hp->closePopup();
                        hp->execute(c);
                    });
                });
                return true;
            }
            host_.post([hp, cmd]() { hp->execute(cmd); });
            return true;
        }
        return inside;
    }

private:
    Host& host_;
    const std::vector<MenuItem>& items_;
    const int ownerMenu_;
    int hover_ = -1;

    void requestClose()
    {
        Host* hp = &host_;
        host_.post([hp]() { hp->closePopup(); });
    }
};

class ListPopupNode : public Node {
public:
    ListPopupNode(Host& h, std::vector<std::string> items, int current, std::function<void(int)> pick,
                  std::vector<char> disabled = {}, std::vector<Cmd> cmds = {})
        : host_(h), items_(std::move(items)), current_(current), pick_(std::move(pick)),
          disabled_(std::move(disabled)), cmds_(std::move(cmds))
    {
    }

    bool itemEnabled(int i) const
    {
        if (i >= 0 && i < (int)cmds_.size()) return menuCmdEnabledImpl(host_, cmds_[(size_t)i]);
        return i < 0 || disabled_.empty() || i >= (int)disabled_.size() || !disabled_[(size_t)i];
    }

    Size requestSize() const override
    {
        return Size{100, 8 + (int)items_.size() * menuItemHeight()};
    }
    bool focusable() const override { return true; }

    bool onKey(const KeyEvent& e) override
    {
        if (e.pressed && e.scancode == KeyEsc) requestClose();
        return true;
    }

    void paint(SkCanvas& c) override
    {
        const Theme& t = theme();
        fill(c, frame_, t.panel, 3.0f);
        stroke(c, frame_, t.border, 1.0f, 3.0f);
        stroke(c, Rect{frame_.x + 2, frame_.y + 2, frame_.w - 4, frame_.h - 4}, rgba(255, 255, 255, 90),
               1.0f, 2.0f);
        int y = frame_.y + 4;
        for (size_t i = 0; i < items_.size(); ++i) {
            Rect r{frame_.x + 2, y, frame_.w - 4, menuItemHeight()};
            const bool on = itemEnabled((int)i);
            if (hover_ == (int)i && on) fill(c, r, t.hover, 2.0f);
            drawText(c, (float)(r.x + 10), (float)(r.y + (r.h - (int)lineHeight()) / 2),
                     !on ? t.dim : (current_ == (int)i ? t.accent : t.text), items_[i]);
            y += menuItemHeight();
        }
    }

    bool onPointer(const PointerEvent& e) override
    {
        const bool inside = frame_.contains((int)e.pos.x, (int)e.pos.y);
        int idx = -1;
        if (inside) {
            idx = ((int)e.pos.y - frame_.y - 4) / menuItemHeight();
            if (idx >= (int)items_.size()) idx = -1;
        }
        if (idx != hover_) {
            hover_ = idx;
            markDirty(frame_);
        }
        if (e.pressed && inside && idx >= 0 && !itemEnabled(idx)) return true;
        if (e.pressed && inside && idx >= 0) {
            Host* hp = &host_;
            std::function<void(int)> pick = pick_;
            host_.post([hp, idx, pick]() {
                hp->closePopup();
                if (pick) pick(idx);
            });
            return true;
        }
        return inside;
    }

private:
    Host& host_;
    std::vector<std::string> items_;
    int current_;
    std::function<void(int)> pick_;
    std::vector<char> disabled_;
    std::vector<Cmd> cmds_;
    int hover_ = -1;

    void requestClose()
    {
        Host* hp = &host_;
        host_.post([hp]() { hp->closePopup(); });
    }
};

class TabBarNode : public Node {
public:
    explicit TabBarNode(Host& h) : host_(h)
    {
        host_.setTabProbe([this](float x, float y) { return this->indexAt(x, y); });
    }

    Size requestSize() const override { return Size{0, dims::tabH}; }

    Rect tabRect(int i) const
    {
        int tx = frame_.x + 6;
        for (int k = 0; k < i; ++k) tx += std::max(110, (int)textWidth(host_.tab(k).title) + 46) + 4;
        const int w = std::max(110, (int)textWidth(host_.tab(i).title) + 46);
        return Rect{tx, frame_.y + 2, w, frame_.h - 4};
    }

    int indexAt(float x, float y) const
    {
        for (int i = 0; i < host_.tabCount(); ++i)
            if (tabRect(i).contains((int)x, (int)y)) return i;
        return -1;
    }

    void paint(SkCanvas& c) override
    {
        const Theme& t = theme();
        fill(c, frame_, t.panel);
        fill(c, Rect{frame_.x, frame_.bottom() - 1, frame_.w, 1}, t.border);
        for (int i = 0; i < host_.tabCount(); ++i) {
            const std::string& title = host_.tab(i).title;
            const Rect r = tabRect(i);
            const bool active = i == host_.activeTabIndex();
            const bool dropTarget = i == host_.dropTabTarget();
            fill(c, r, active ? t.panelHi : (hoverTab_ == i ? t.hover : t.surface), 3.0f);
            if (dropTarget) stroke(c, r, t.accent, 2.0f, 3.0f);
            drawText(c, (float)(r.x + 10), (float)(r.y + (r.h - (int)lineHeight()) / 2), active ? t.text : t.dim,
                     title);
            const bool closeHover = hoverTab_ == i && hoverClose_;
            drawText(c, (float)(r.right() - 18), (float)(r.y + (r.h - (int)lineHeight()) / 2),
                     closeHover ? t.accent : t.dim, "x");
        }
    }

    bool onPointer(const PointerEvent& e) override
    {
        const int px = (int)e.pos.x, py = (int)e.pos.y;
        const bool inside = frame_.contains(px, py);

        const int tab = inside ? indexAt(e.pos.x, e.pos.y) : -1;
        int close = -1;
        if (tab >= 0 && px >= tabRect(tab).right() - 24) close = tab;
        const bool closeHover = close >= 0;

        if (tab != hoverTab_ || closeHover != hoverClose_) {
            hoverTab_ = tab;
            hoverClose_ = closeHover;
            markDirty(frame_);
        }

        if (!e.pressed) return inside;
        if (close >= 0) {
            host_.promptCloseTab(close);
            return true;
        }
        if (tab >= 0) {
            host_.selectTab(tab);
            return true;
        }
        return true;
    }

    bool onDropFile(const std::string& path, float x, float y) override
    {
        const int tab = indexAt(x, y);
        if (tab < 0) return false;
        host_.selectTab(tab);
        host_.placeImageLayer(path);
        return true;
    }

private:
    Host& host_;
    int hoverTab_ = -1;
    bool hoverClose_ = false;
};

class OptionsBarNode : public Node {
public:
    explicit OptionsBarNode(Host& h) : host_(h), hp_(&h)
    {
        pads_.setShowToolName(false);
        num_.lo = 2.0;
        num_.hi = 16.0;
        num_.step = 1.0;
        num_.fmt = "%.0f";
        num_.get = &meshGet;
        num_.set = &meshSet;
        num_.readUd = hp_;
        num_.writeUd = hp_;

        static const double lo[kFieldCount] = {-400.0, -400.0, -180.0, -1.0, 0.0, 0.2, 0.0};
        static const double hi[kFieldCount] = {400.0, 400.0, 180.0, 1.0, 8.0, 8.0, 128.0};
        static const double step[kFieldCount] = {1.0, 1.0, 1.0, 0.05, 1.0, 0.1, 1.0};
        static const char* fmt[kFieldCount] = {"%.0f", "%.0f", "%.0f", "%.2f", "%.0f", "%.1f", "%.0f"};
        for (int i = 0; i < kFieldCount; ++i) {
            slots_[i].h = hp_;
            slots_[i].idx = i;
            lw_[i].lo = lo[i];
            lw_[i].hi = hi[i];
            lw_[i].step = step[i];
            lw_[i].fmt = fmt[i];
            lw_[i].get = &lwGet;
            lw_[i].set = &lwSet;
            lw_[i].readUd = &slots_[i];
            lw_[i].writeUd = &slots_[i];
        }
    }

    Size requestSize() const override
    {
        if (warpOn()) return Size{280, warpH()};
        Props tmp;
        host_.tool().props(tmp, host_.ed());
        if (!tmp.empty() && tmp[0].kind == PropKind::Header) tmp.erase(tmp.begin());
        const int w = pads_.measureWidth(tmp);
        if (w <= 0) return Size{0, 0};
        return Size{std::clamp(w, 220, 900), dims::optionsH};
    }
    bool focusable() const override { return true; }

    void paint(SkCanvas& c) override
    {
        if (frame_.w <= 0 || frame_.h <= 0) return;
        const Theme& t = theme();
        const Rect p = frame_;
        fill(c, p, t.panel, (float)t.radius);
        stroke(c, p, t.border, 1.0f, (float)t.radius);
        if (warpOn()) {
            paintWarp(c);
            return;
        }
        pads_.sync(host_);
        if (pads_.empty()) return;
        pads_.paint(c, p, host_);
    }

    bool onPointer(const PointerEvent& e) override
    {
        if (frame_.w <= 0 || frame_.h <= 0) return false;
        if (dragging_) {
            if (e.released) {
                endDrag();
                return true;
            }
            if (e.down) dragMove(e.pos);
            return true;
        }
        if (e.pressed && frame_.contains((int)e.pos.x, (int)e.pos.y)) {
            if (warpOn()) {
                if (!warpHit(e.pos)) {
                    startDrag(e.pos);
                    return true;
                }
            } else {
                pads_.sync(host_);
                if (!pads_.hitControl(e.pos.x, e.pos.y, frame_)) {
                    startDrag(e.pos);
                    return true;
                }
            }
        }
        if (warpOn()) {
            if (!frame_.contains((int)e.pos.x, (int)e.pos.y)) {
                cancelWarpEdit();
                return false;
            }
            if (host_.ed().meshWarpActive()) {
                if (num_.pointer(e, warpBox(0, "网络密度"))) {
                    markDirty(frame_);
                    return true;
                }
                return true;
            }
            bool used = false;
            for (int i = 0; i < kShowCount; ++i) {
                const int f = kShow[i];
                if (lw_[f].pointer(e, warpBox(i, kLabels[f]))) used = true;
            }
            if (used) markDirty(frame_);
            return true;
        }
        if (!frame_.contains((int)e.pos.x, (int)e.pos.y)) {
            if (pads_.clearHover()) markDirty(frame_);
            return false;
        }
        if (pads_.pointer(host_, e, frame_)) {
            markDirty(frame_);
            if (e.pressed) host_.relayout();
            return true;
        }
        return true;
    }

    bool onKey(const KeyEvent& e) override
    {
        if (warpOn()) {
            for (int i = 0; i < kFieldCount; ++i) {
                if (!lw_[i].editing) continue;
                if (!lw_[i].key(e)) return false;
                markDirty(frame_);
                return true;
            }
            if (!num_.editing) return false;
            if (!num_.key(e)) return false;
            markDirty(frame_);
            return true;
        }
        if (!pads_.key(e, frame_)) return false;
        markDirty(frame_);
        return true;
    }

    bool editingText() const override
    {
        if (warpOn()) {
            if (num_.editing) return true;
            for (int i = 0; i < kFieldCount; ++i)
                if (lw_[i].editing) return true;
            return false;
        }
        return pads_.numEditing();
    }

    void cancelTransientEdit(float px, float py) override
    {
        if (frame_.contains((int)px, (int)py)) return;
        if (warpOn()) {
            cancelWarpEdit();
            return;
        }
        if (pads_.cancelNumEdit()) markDirty(frame_);
    }

private:
    static constexpr int kFieldCount = 7;
    static constexpr int kShowCount = 5;
    static constexpr int kRowH = 30;

    inline static const char* const kLabels[kFieldCount] = {"幅度X", "幅度Y", "旋转", "泊松", "谐波", "衰减", "羽化"};
    inline static const int kShow[kShowCount] = {0, 1, 3, 4, 5};

    struct LWSlot {
        Host* h = nullptr;
        int idx = 0;
    };

    static double lwGet(const void* ud)
    {
        const LWSlot* s = (const LWSlot*)ud;
        return s->h->ed().localWarpField(s->idx);
    }

    static void lwSet(void* ud, double v)
    {
        LWSlot* s = (LWSlot*)ud;
        s->h->ed().localWarpSetField(s->idx, v);
    }

    static double meshGet(const void* ud)
    {
        const Host* h = (const Host*)ud;
        return (double)h->ed().meshDensity;
    }

    static void meshSet(void* ud, double v)
    {
        Host* h = (Host*)ud;
        h->ed().setMeshDensity((int)std::lround(v));
    }

    bool warpOn() const
    {
        return host_.ed().meshWarpActive() || host_.ed().localWarpActive();
    }

    int warpH() const
    {
        return host_.ed().meshWarpActive() ? 42 : 162;
    }

    bool warpHit(const Vec2f& p) const
    {
        if (host_.ed().meshWarpActive())
            return warpBox(0, "网络密度").contains((int)p.x, (int)p.y);
        for (int i = 0; i < kShowCount; ++i) {
            const int f = kShow[i];
            if (warpBox(i, kLabels[f]).contains((int)p.x, (int)p.y)) return true;
        }
        return false;
    }

    void startDrag(const Vec2f& p)
    {
        dragging_ = true;
        grabX_ = p.x - (float)frame_.x;
        grabY_ = p.y - (float)frame_.y;
        host_.capturePointer(this);
    }

    void dragMove(const Vec2f& p)
    {
        const Size v = host_.viewSize();
        const int nx = std::clamp((int)std::lround(p.x - grabX_), 0, std::max(0, v.w - frame_.w));
        const int ny = std::clamp((int)std::lround(p.y - grabY_), 0, std::max(0, v.h - frame_.h));
        host_.settings.optX = nx;
        host_.settings.optY = ny;
        host_.relayout();
    }

    void endDrag()
    {
        dragging_ = false;
        host_.releasePointer(this);
        host_.persistSettings();
    }

    Rect warpRow(int i) const
    {
        return Rect{frame_.x + 4, frame_.y + 6 + i * kRowH, std::max(0, frame_.w - 8), 26};
    }

    Rect warpBox(int i, const char* label) const
    {
        const Rect row = warpRow(i);
        const int lw = (int)ui::textWidth(label) + 6;
        const int x = row.x + 8 + lw;
        return Rect{x, row.y + 2, std::max(40, row.right() - 8 - x), row.h - 4};
    }

    void paintWarp(SkCanvas& c)
    {
        const Theme& t = theme();
        if (host_.ed().meshWarpActive()) {
            const Rect row = warpRow(0);
            fill(c, row, t.surface, 2.0f);
            drawText(c, (float)(row.x + 8), (float)(row.y + (row.h - (int)lineHeight()) / 2), t.text,
                     "网络密度");
            const Rect box = warpBox(0, "网络密度");
            fill(c, box, t.panelHi, 2.0f);
            stroke(c, box, t.border, 1.0f, 2.0f);
            num_.paint(c, box, num_.editing);
            return;
        }
        for (int i = 0; i < kShowCount; ++i) {
            const Rect row = warpRow(i);
            if (row.y + row.h > frame_.bottom()) continue;
            const int f = kShow[i];
            fill(c, row, t.surface, 2.0f);
            drawText(c, (float)(row.x + 8), (float)(row.y + (row.h - (int)lineHeight()) / 2), t.dim,
                     kLabels[f]);
            const Rect box = warpBox(i, kLabels[f]);
            fill(c, box, t.panelHi, 2.0f);
            stroke(c, box, t.border, 1.0f, 2.0f);
            lw_[f].paint(c, box, lw_[f].editing);
        }
    }

    void cancelWarpEdit()
    {
        bool any = false;
        for (int i = 0; i < kFieldCount; ++i) {
            if (!lw_[i].editing) continue;
            lw_[i].cancelEdit();
            any = true;
        }
        if (num_.editing) {
            num_.cancelEdit();
            any = true;
        }
        if (any) markDirty(frame_);
    }

    Host& host_;
    Host* hp_ = nullptr;
    PropsView pads_;
    NumField num_;
    NumField lw_[kFieldCount];
    LWSlot slots_[kFieldCount];
    bool dragging_ = false;
    float grabX_ = 0.0f;
    float grabY_ = 0.0f;
};
SkColor toolIconColor(Color32 c)
{
    return SkColorSetARGB((c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255);
}

SkPaint toolIconStroke(Color32 c, float w)
{
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(toolIconColor(c));
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(w);
    p.setStrokeCap(SkPaint::kRound_Cap);
    p.setStrokeJoin(SkPaint::kRound_Join);
    return p;
}

SkPaint toolIconFill(Color32 c)
{
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(toolIconColor(c));
    return p;
}

void drawToolIcon(SkCanvas& c, ToolId id, float cx, float cy, Color32 col)
{
    c.save();
    c.translate(cx, cy);
    c.scale(1.75f, 1.75f);
    c.translate(-cx, -cy);
    switch (id) {
    case ToolId::Move: {
        SkPath p;
        p.moveTo(cx - 7.5f, cy - 9.0f);
        p.lineTo(cx - 7.5f, cy + 6.0f);
        p.lineTo(cx - 3.2f, cy + 2.0f);
        p.lineTo(cx - 0.8f, cy + 8.5f);
        p.lineTo(cx + 2.0f, cy + 7.2f);
        p.lineTo(cx - 0.5f, cy + 1.2f);
        p.lineTo(cx + 4.2f, cy + 1.2f);
        p.close();
        const SkPaint fl = toolIconFill(col);
        c.drawPath(p, fl);
        c.drawPath(p, toolIconStroke(col, 0.9f));
        break;
    }
    case ToolId::Select: {
        const SkPaint st = toolIconStroke(col, 1.5f);
        const float x0 = cx - 8, x1 = cx + 8, y0 = cy - 6, y1 = cy + 6;
        for (float x = x0; x < x1; x += 5.0f) {
            const float xe = std::min(x + 3.0f, x1);
            c.drawLine(x, y0, xe, y0, st);
            c.drawLine(x, y1, xe, y1, st);
        }
        for (float y = y0; y < y1; y += 5.0f) {
            const float ye = std::min(y + 3.0f, y1);
            c.drawLine(x0, y, x0, ye, st);
            c.drawLine(x1, y, x1, ye, st);
        }
        break;
    }
    case ToolId::Lasso: {
        const SkPaint st = toolIconStroke(col, 1.4f);
        SkPath poly;
        const float px[] = {cx - 7.0f, cx - 4.0f, cx + 3.0f, cx + 8.0f, cx + 4.5f};
        const float py[] = {cy + 5.5f, cy - 5.5f, cy - 7.0f, cy + 0.5f, cy + 7.5f};
        for (int i = 0; i < 5; ++i) {
            if (i == 0) poly.moveTo(px[0], py[0]);
            else poly.lineTo(px[i], py[i]);
        }
        poly.close();
        c.drawPath(poly, st);
        fill(c, Rect{(int)(cx - 8.0f), (int)(cy + 4.0f), 3, 3}, col, 1.0f);
        break;
    }
    case ToolId::Wand: {
        c.drawCircle(cx, cy, 9.2f, toolIconStroke(col, 1.0f));
        c.drawCircle(cx, cy, 6.2f, toolIconStroke(col, 1.2f));
        c.drawCircle(cx, cy, 3.4f, toolIconStroke(col, 1.4f));
        c.drawCircle(cx, cy, 1.4f, toolIconFill(col));
        break;
    }
    case ToolId::Crop: {
        const SkPaint st = toolIconStroke(col, 1.6f);
        const float o = 9.0f, l = 4.5f;
        c.drawLine(cx - o, cy - o + l, cx - o, cy - o, st);
        c.drawLine(cx - o, cy - o, cx - o + l, cy - o, st);
        c.drawLine(cx + o - l, cy - o, cx + o, cy - o, st);
        c.drawLine(cx + o, cy - o, cx + o, cy - o + l, st);
        c.drawLine(cx - o, cy + o - l, cx - o, cy + o, st);
        c.drawLine(cx - o, cy + o, cx - o + l, cy + o, st);
        c.drawLine(cx + o - l, cy + o, cx + o, cy + o, st);
        c.drawLine(cx + o, cy + o - l, cx + o, cy + o, st);
        c.drawLine(cx - 4.0f, cy, cx + 4.0f, cy, toolIconStroke(col, 1.4f));
        c.drawLine(cx, cy - 4.0f, cx, cy + 4.0f, toolIconStroke(col, 1.4f));
        break;
    }
    case ToolId::Brush: {
        const SkPaint st = toolIconStroke(col, 1.6f);
        SkPath handle;
        handle.moveTo(cx + 9.0f, cy - 9.0f);
        handle.lineTo(cx + 6.6f, cy - 4.4f);
        handle.lineTo(cx + 0.4f, cy + 1.8f);
        handle.lineTo(cx - 2.2f, cy - 0.4f);
        handle.close();
        c.drawPath(handle, st);
        c.drawLine(cx - 3.0f, cy + 3.0f, cx + 0.8f, cy - 1.4f, toolIconStroke(col, 2.0f));
        SkPath trail;
        trail.moveTo(cx - 2.6f, cy + 1.7f);
        trail.quadTo(cx - 5.8f, cy + 5.6f, cx - 9.4f, cy + 8.8f);
        trail.quadTo(cx - 7.2f, cy + 8.6f, cx - 5.4f, cy + 7.0f);
        trail.quadTo(cx - 4.0f, cy + 5.0f, cx - 1.8f, cy + 2.8f);
        trail.close();
        c.drawPath(trail, toolIconFill(col));
        break;
    }
    case ToolId::Bucket: {
        c.save();
        c.translate(cx - 0.6f, cy + 0.2f);
        c.rotate(-32.0f);
        SkPath body;
        body.moveTo(-7.2f, -5.8f);
        body.lineTo(7.2f, -5.8f);
        body.lineTo(4.6f, 6.8f);
        body.lineTo(-4.6f, 6.8f);
        body.close();
        c.drawPath(body, toolIconStroke(col, 1.4f));
        c.drawOval(SkRect::MakeLTRB(-7.4f, -7.6f, 7.4f, -4.0f), toolIconStroke(col, 1.2f));
        SkPath handle;
        handle.moveTo(-4.6f, -6.6f);
        handle.quadTo(0.0f, -12.4f, 4.6f, -6.6f);
        c.drawPath(handle, toolIconStroke(col, 1.3f));
        c.restore();
        SkPath drop;
        drop.moveTo(cx - 7.3f, cy + 3.9f);
        drop.quadTo(cx - 10.6f, cy + 7.2f, cx - 7.3f, cy + 9.1f);
        drop.quadTo(cx - 4.0f, cy + 7.2f, cx - 7.3f, cy + 3.9f);
        drop.close();
        c.drawPath(drop, toolIconFill(col));
        break;
    }
    case ToolId::Text: {
        const SkPaint st = toolIconStroke(col, 2.0f);
        c.drawLine(cx - 6, cy - 7, cx + 6, cy - 7, st);
        c.drawLine(cx, cy - 7, cx, cy + 7, st);
        c.drawLine(cx - 3, cy + 7, cx + 3, cy + 7, toolIconStroke(col, 1.5f));
        break;
    }
    case ToolId::Shape: {
        const SkPaint st = toolIconStroke(col, 1.6f);
        c.drawRect(SkRect::MakeLTRB(cx - 9, cy - 7, cx + 1, cy + 3), st);
        c.drawCircle(cx + 3, cy + 2, 5.0f, st);
        break;
    }
    case ToolId::Eraser: {
        const SkPaint st = toolIconStroke(col, 1.6f);
        SkPath body;
        body.moveTo(cx - 8.5f, cy + 1.5f);
        body.lineTo(cx + 1.0f, cy - 8.0f);
        body.lineTo(cx + 8.5f, cy - 0.5f);
        body.lineTo(cx - 1.0f, cy + 9.0f);
        body.close();
        c.drawPath(body, st);
        c.drawLine(cx - 4.2f, cy + 5.8f, cx + 4.2f, cy - 2.6f, toolIconStroke(col, 1.3f));
        break;
    }
    case ToolId::Mask: {
        const SkPaint st = toolIconStroke(col, 1.5f);
        for (int k = 0; k < 6; ++k) {
            SkPath arc;
            arc.addArc(SkRect::MakeXYWH(cx - 8.0f, cy - 8.0f, 16.0f, 16.0f), (float)(k * 60 + 8), 44.0f);
            c.drawPath(arc, st);
        }
        c.drawCircle(cx, cy, 2.0f, toolIconStroke(col, 2.0f));
        break;
    }
    case ToolId::Gradient: {
        const SkPaint st = toolIconStroke(col, 1.4f);
        const SkRect box = SkRect::MakeLTRB(cx - 9, cy - 7, cx + 9, cy + 7);
        c.drawRect(box, st);
        const SkPaint fl = toolIconFill(col);
        c.drawRect(SkRect::MakeLTRB(cx - 7, cy - 5, cx - 1, cy + 5), fl);
        c.drawLine(cx + 1.5f, cy - 5, cx + 1.5f, cy + 5, toolIconStroke(col, 3.0f));
        c.drawLine(cx + 4.5f, cy - 5, cx + 4.5f, cy + 5, toolIconStroke(col, 2.0f));
        c.drawLine(cx + 7.0f, cy - 5, cx + 7.0f, cy + 5, toolIconStroke(col, 1.0f));
        break;
    }

    }
    c.restore();
}

class ToolRailNode : public Node {
public:
    explicit ToolRailNode(Host& h) : host_(h) {}

    Size requestSize() const override { return Size{dims::railW, 0}; }

    void paint(SkCanvas& c) override
    {
        const Theme& t = theme();
        fill(c, frame_, t.panel);
        fill(c, Rect{frame_.right() - 1, frame_.y, 1, frame_.h}, t.border);
        for (int i = 0; i < (int)host_.toolsCount(); ++i) {
            Rect r = itemRect(i);
            const bool on = i == host_.activeToolIndex();
            fill(c, r, on ? t.accent : (hover_ == i ? t.hover : t.panelHi), 3.0f);
            if (hover_ == i && !on) stroke(c, r, t.accent, 1.0f, 3.0f);
            drawToolIcon(c, host_.toolId(i), r.x + r.w * 0.5f, r.y + r.h * 0.5f,
                         (on || hover_ == i) ? t.text : t.dim);
        }

        const Palette& pal = host_.ed().palette;
        drawColorCell(c, bgRect(), pal.bg, hoverCtl_ == 1);
        drawColorCell(c, fgRect(), pal.fg, hoverCtl_ == 0);
        const Rect sw = swapRect();
        fill(c, sw, hoverCtl_ == 2 ? t.hover : t.panelHi, 3.0f);
        stroke(c, sw, hoverCtl_ == 2 ? t.accent : t.border, 1.0f, 3.0f);
        const SkPaint arrow = toolIconStroke(hoverCtl_ == 2 ? t.text : t.dim, 1.3f);
        const float inset = 4.0f;
        const float ax = (float)sw.x + inset, bx = (float)sw.right() - inset;
        const float top = (float)sw.y + (float)sw.h * 0.34f;
        const float bot = (float)sw.y + (float)sw.h * 0.66f;
        c.drawLine(ax, top, bx, top, arrow);
        c.drawLine(bx - 2.0f, top - 2.0f, bx, top, arrow);
        c.drawLine(bx - 2.0f, top + 2.0f, bx, top, arrow);
        c.drawLine(bx, bot, ax, bot, arrow);
        c.drawLine(ax + 2.0f, bot - 2.0f, ax, bot, arrow);
        c.drawLine(ax + 2.0f, bot + 2.0f, ax, bot, arrow);
    }

    bool cursorAt(float x, float y, CursorShape& out) override
    {
        if (!frame_.contains((int)x, (int)y)) return false;
        out = CursorShape::Arrow;
        return true;
    }

    bool onPointer(const PointerEvent& e) override
    {
        if (!frame_.contains((int)e.pos.x, (int)e.pos.y)) {
            if (hover_ >= 0 || hoverCtl_ >= 0) {
                hover_ = -1;
                hoverCtl_ = -1;
                host_.setHoverHint({});
                markDirty(frame_);
            }
            return false;
        }
        int ctl = -1;
        if (swapRect().contains((int)e.pos.x, (int)e.pos.y))
            ctl = 2;
        else if (fgRect().contains((int)e.pos.x, (int)e.pos.y))
            ctl = 0;
        else if (bgRect().contains((int)e.pos.x, (int)e.pos.y))
            ctl = 1;

        int h = -1;
        if (ctl < 0)
            for (int i = 0; i < (int)host_.toolsCount(); ++i)
                if (itemRect(i).contains((int)e.pos.x, (int)e.pos.y)) h = i;
        if (h != hover_ || ctl != hoverCtl_) {
            hover_ = h;
            hoverCtl_ = ctl;
            std::string hint;
            Rect anchor{};
            if (ctl == 0) {
                hint = "前景色";
                anchor = fgRect();
            } else if (ctl == 1) {
                hint = "背景色";
                anchor = bgRect();
            } else if (ctl == 2) {
                hint = "交换前景/背景色";
                anchor = swapRect();
            } else if (h >= 0) {
                hint = host_.toolName(h);
                const std::string key = shortcuts::toolCombo(h);
                if (!key.empty()) hint += " " + key;
                anchor = itemRect(h);
            }
            host_.setHoverHint(std::move(hint), anchor);
            markDirty(frame_);
        }
        if (e.pressed && ctl >= 0) {
            Host* hp = &host_;
            if (ctl == 2) {
                hp->ed().swapPalette();
                hp->markChromeDirty();
                return true;
            }
            const float* col = ctl == 0 ? hp->ed().palette.fg : hp->ed().palette.bg;
            hp->openDialogNode(makeColorPopup(*hp, col, [hp, ctl](const float rgba[4]) {
                if (ctl == 0)
                    hp->ed().palette.setFg(rgba);
                else
                    hp->ed().palette.setBg(rgba);
                hp->markChromeDirty();
            }));
            return true;
        }
        if (e.pressed) {
            if (h >= 0) host_.setTool(h);
        }
        return true;
    }

private:


    Rect itemRect(int i) const
    {
        const RailGrid g = railGrid(frame_, (int)host_.toolsCount());
        const int c = i % g.cols, r = i / g.cols;
        return Rect{g.area.x + c * (g.cell + g.gap), g.area.y + r * (g.cell + g.gap), g.cell, g.cell};
    }

    Rect swatchArea() const { return railSwatchDocked(frame_, (int)host_.toolsCount()); }
    Rect fgRect() const
    {
        Rect fg, bg, sw;
        swatchCellsIn(swatchArea(), fg, bg, sw);
        return fg;
    }
    Rect bgRect() const
    {
        Rect fg, bg, sw;
        swatchCellsIn(swatchArea(), fg, bg, sw);
        return bg;
    }
    Rect swapRect() const
    {
        Rect fg, bg, sw;
        swatchCellsIn(swatchArea(), fg, bg, sw);
        return sw;
    }

    void drawColorCell(SkCanvas& c, const Rect& r, const float* col, bool hot) const
    {
        const Theme& t = theme();
        for (int y = 0; y < r.h; y += 6)
            for (int x = 0; x < r.w; x += 6)
                fill(c, Rect{r.x + x, r.y + y, std::min(6, r.w - x), std::min(6, r.h - y)},
                     (((x / 6) + (y / 6)) & 1) ? t.checkB : t.checkA);
        fill(c, r, rgba((int)(col[0] * 255.0f + 0.5f), (int)(col[1] * 255.0f + 0.5f),
                        (int)(col[2] * 255.0f + 0.5f), (int)(col[3] * 255.0f + 0.5f)));
        stroke(c, r, hot ? t.accent : IE_COL32(255, 255, 255, 255), 1.0f);
    }

    Host& host_;
    int hover_ = -1;
    int hoverCtl_ = -1;
};

class PaneNode : public Node {
public:
    PaneNode(Host& h, Host::Pane id, const char* title) : host_(h), id_(id), title_(title) {}

    Size requestSize() const override
    {
        return Size{id_ == Host::Pane::Rail ? dims::railW : dims::rightW, 0};
    }
    bool focusable() const override { return true; }
    Rect titleBarRect() const override
    {
        return Rect{frame_.x + 6, frame_.y + 4, std::max(0, frame_.w - 12), 26};
    }

    void paint(SkCanvas& c) override
    {
        const Theme& t = theme();
        fill(c, frame_, t.panel);
        fill(c, Rect{frame_.x, frame_.y, 1, frame_.h}, t.border);

        const Rect hb = titleBarRect();
        fill(c, hb, t.panelHi, 3.0f);
        drawText(c, (float)(hb.x + 8), (float)(hb.y + (hb.h - (int)lineHeight()) / 2), t.text, title_);
        paintBody(c);
    }

    bool cursorAt(float x, float y, CursorShape& out) override
    {
        if (!bodyHoldsPointer() && !frame_.contains((int)x, (int)y)) return false;
        out = CursorShape::Arrow;
        return true;
    }

    bool onPointer(const PointerEvent& e) override
    {
        if (!bodyHoldsPointer() && !frame_.contains((int)e.pos.x, (int)e.pos.y)) {
            if (clearBodyHover()) markDirty(frame_);
            return false;
        }
        return onBody(e);
    }

    Host::Pane paneId() const { return id_; }

protected:
    virtual void paintBody(SkCanvas&) = 0;
    virtual bool onBody(const PointerEvent&) = 0;
    virtual bool clearBodyHover() { return false; }
    virtual bool bodyHoldsPointer() const { return false; }
    Rect bodyRect() const
    {
        return Rect{frame_.x + 4, frame_.y + 28, std::max(0, frame_.w - 8),
                    std::max(0, frame_.h - 32)};
    }

    Host& host_;
    Host::Pane id_;
    const char* title_;
};


class LayerPaneNode : public PaneNode {
public:
    explicit LayerPaneNode(Host& h) : PaneNode(h, Host::Pane::Layers, "图层") {}

protected:
    void paintBody(SkCanvas& c) override
    {
        const Theme& t = theme();
        Editor& e = host_.ed();
        const Zones z = zones();

        const Layer* act = e.doc.activeLayer();
        if (act) {
            drawText(c, (float)z.modeRow.x, (float)(z.modeRow.y + (z.modeRow.h - (int)lineHeight()) / 2),
                     t.dim, "混合");
            fill(c, z.modeBox, t.panelHi, 2.0f);
            stroke(c, z.modeBox, host_.listArmed(z.modeBox) ? t.accent : t.border, 1.0f, 2.0f);
            drawText(c, (float)(z.modeBox.x + 6),
                     (float)(z.modeBox.y + (z.modeBox.h - (int)lineHeight()) / 2), t.text,
                     blendModeName(act->blend));

            drawText(c, (float)z.opRow.x, (float)(z.opRow.y + (z.opRow.h - (int)lineHeight()) / 2), t.dim,
                     "不透明度");
            bindOpacity();
            op_.paint(c, opacityBox(z), op_.editing);

            Rect box{z.lockRow.x, z.lockRow.y + (z.lockRow.h - 18) / 2, 18, 18};
            stroke(c, box, t.border, 1.0f, 2.0f);
            if (act->lockAlpha) fill(c, Rect{box.x + 4, box.y + 4, 10, 10}, t.accent, 2.0f);
            drawText(c, (float)(box.right() + 8),
                     (float)(z.lockRow.y + (z.lockRow.h - (int)lineHeight()) / 2), t.text, "锁定透明像素");
        }

        for (int i = 0; i < ui::kLayerBtnCount; ++i) {
            const Rect r = buttonRect(i);
            const bool hot = hoverBtn_ == i;
            fill(c, r, hot ? t.hover : t.panelHi, 2.0f);
            stroke(c, r, hot ? t.accent : t.border, 1.0f, 2.0f);
            const std::string s = ui::layerBtnLabel(i);
            drawText(c, (float)(r.x + (r.w - (int)textWidth(s)) / 2),
                     (float)(r.y + (r.h - (int)lineHeight()) / 2), t.text, s);
        }

        const Rect list = z.list;
        c.save();
        c.clipRect(SkRect::MakeLTRB((float)list.x, (float)list.y, (float)list.right(),
                                    (float)list.bottom()));
        const int n = (int)e.doc.layers.size();
        for (int i = 0; i < n; ++i) {
            const Layer& l = e.doc.layers[(size_t)i];
            Rect r{list.x, list.y + (n - 1 - i) * z.rowH - scroll_, list.w, z.rowH - 2};
            if (r.bottom() < list.y || r.y > list.bottom()) continue;
            if (e.doc.layerSelected(i)) fill(c, r, t.hover, 2.0f);
            Rect eye{r.x + 4, r.y + (z.rowH - 16) / 2, 16, 16};
            stroke(c, eye, t.border, 1.0f, 2.0f);
            if (l.visible) fill(c, Rect{eye.x + 4, eye.y + 4, 8, 8}, t.accent, 2.0f);            drawText(c, (float)(eye.right() + 6), (float)(r.y + (z.rowH - (int)lineHeight()) / 2),
                     t.text, l.name);
        }
        if (dragging_ && dragTo_ >= 0) {
            const int y = list.y + (n - 1 - dragTo_) * z.rowH - scroll_;
            fill(c, Rect{list.x, y, list.w, 2}, t.accent);
        }
        c.restore();
    }

    void cancelTransientEdit(float px, float py) override
    {
        if (!op_.editing) return;
        const Zones z = zones();
        if (opacityBox(z).contains((int)px, (int)py)) return;
        op_.cancelEdit();
        host_.ed().endEditStep();
        markDirty(frame_);
    }

    bool onBody(const PointerEvent& e) override
    {
        Editor& ed = host_.ed();
        const Zones z = zones();
        if (!e.pressed && !e.released && !e.down) {
            const int hb = (z.buttons.contains((int)e.pos.x, (int)e.pos.y)) ? buttonIndexAt((int)e.pos.x) : -1;
            if (hb != hoverBtn_) {
                hoverBtn_ = hb;
                markDirty(frame_);
            }
        }
        if (e.pressed && z.modeBox.contains((int)e.pos.x, (int)e.pos.y)) {
            openBlendPopup((float)z.modeBox.x, (float)z.modeBox.bottom());
            armBlendBox(z.modeBox);
            return true;
        }
        if (e.pressed && opacityBox(z).contains((int)e.pos.x, (int)e.pos.y)) {
            bindOpacity();
            if (!opHeld_) {
                opHeld_ = true;
                host_.capturePointer(this);
                ed.beginLayerOpacityEdit();
            }
            op_.pointer(e, opacityBox(z));
            markDirty(frame_);
            return true;
        }
        if (opHeld_ && (e.down || e.released)) {
            bindOpacity();
            op_.pointer(e, opacityBox(z));
            if (e.released) {
                opHeld_ = false;
                host_.releasePointer(this);
                if (!op_.editing) ed.endEditStep();
            }
            markDirty(frame_);
            return true;
        }
        if (e.pressed && Rect{z.lockRow.x, z.lockRow.y, z.lockRow.w, z.lockRow.h}.contains((int)e.pos.x,
                                                                                         (int)e.pos.y)) {
            const Layer* l = ed.doc.activeLayer();
            ed.setLayerLockAlpha(ed.doc.active, !(l && l->lockAlpha));
            markDirty(frame_);
            return true;
        }
        if (e.pressed && z.buttons.contains((int)e.pos.x, (int)e.pos.y)) {
            const int idx = buttonIndexAt((int)e.pos.x);
            if (idx == 0) ed.addEmptyLayer();
            else if (idx == 1 && ed.doc.active >= 0) ed.duplicateLayer(ed.doc.active);
            else if (idx == 2 && ed.doc.active >= 0) ed.removeLayer(ed.doc.active);
            markDirty(frame_);
            return true;
        }
        if (e.pressed && e.button == 1 && z.list.contains((int)e.pos.x, (int)e.pos.y)) {
            const int idx = rowIndexAt(e.pos.y, z);
            if (idx >= 0) {
                if (!ed.doc.layerSelected(idx)) ed.setActiveLayer(idx);
                openLayerMenu(e.pos.x, e.pos.y);
                markDirty(frame_);
            }
            return true;
        }
        if (e.pressed && z.list.contains((int)e.pos.x, (int)e.pos.y)) {
            const int n = (int)ed.doc.layers.size();
            const int row = ((int)e.pos.y - z.list.y + scroll_) / z.rowH;
            const int idx = n - 1 - row;
            if (idx >= 0 && idx < n) {
                if (e.pos.x - (float)z.list.x < 24) {
                    ed.setLayerVisible(idx, !ed.doc.layers[(size_t)idx].visible);
                } else {
                    if (e.shift) ed.extendLayerSelection(idx);
                    else if (e.ctrl) ed.toggleLayerSelection(idx);
                    else ed.setActiveLayer(idx);
                    dragFrom_ = idx;
                    dragTo_ = idx;
                    dragPressY_ = e.pos.y;
                    dragX_ = e.pos.x;
                    dragY_ = e.pos.y;
                    grabDx_ = e.pos.x - (float)(z.list.x + 4);
                    grabDy_ = e.pos.y - (float)(z.list.y + (n - 1 - idx) * z.rowH - scroll_);
                    dragValid_ = true;
                    dragging_ = false;
                    host_.capturePointer(this);
                }
                markDirty(frame_);
            }
            return true;
        }
        if (dragFrom_ >= 0 && (e.down || e.released)) {
            if (e.down) {
                if (!dragging_ && std::fabs(e.pos.y - dragPressY_) >= 4.0f) dragging_ = true;
                if (dragging_) {
                    dragX_ = e.pos.x;
                    dragY_ = e.pos.y;
                    dragValid_ = z.list.contains((int)e.pos.x, (int)e.pos.y);
                    const int cnt = (int)ed.doc.layers.size();
                    if (dragValid_ && dragFrom_ >= 0 && dragFrom_ < cnt) {
                        const std::string& nm = ed.doc.layers[(size_t)dragFrom_].name;
                        const int gw = std::max(z.list.w - 8, 26 + (int)ui::textWidth(nm) + 12);
                        host_.setLayerDragGhost(Rect{(int)(dragX_ - grabDx_), (int)(dragY_ - grabDy_), gw,
                                                     z.rowH - 2},
                                                nm);
                    } else {
                        host_.clearLayerDragGhost();
                    }
                    const int to = dragValid_ ? rowIndexAt(e.pos.y, z) : -1;
                    if (to != dragTo_) {
                        dragTo_ = to;
                        markDirty(frame_);
                    }
                }
            }
            if (e.released) {
                host_.releasePointer(this);
                if (dragging_ && dragValid_ && dragTo_ >= 0) ed.moveSelectedLayers(dragTo_);
                host_.clearLayerDragGhost();
                dragFrom_ = dragTo_ = -1;
                dragValid_ = false;
                dragging_ = false;
                markDirty(frame_);
            }
            return true;
        }
        return true;
    }

    bool bodyHoldsPointer() const override { return opHeld_ || op_.drag || op_.editing || dragFrom_ >= 0; }
    bool clearBodyHover() override
    {
        if (hoverBtn_ < 0) return false;
        hoverBtn_ = -1;
        return true;
    }

    bool onWheel(const WheelEvent& e) override
    {
        if (!frame_.contains((int)e.pos.x, (int)e.pos.y)) return false;
        scroll_ = std::max(0, scroll_ - (int)(e.dy * 32.0f));
        markDirty(frame_);
        return true;
    }

    bool editingText() const override { return op_.editing; }

    bool onKey(const KeyEvent& e) override
    {
        if (op_.editing) {
            if (op_.key(e)) {
                markDirty(frame_);
                return true;
            }
        }
        if (e.pressed && e.scancode == KeyDelete) {
            host_.execute(Cmd::Delete);
            markDirty(frame_);
            return true;
        }
        if (e.pressed && e.ctrl && !e.alt && e.scancode == KeyA && frame_.contains((int)host_.input().mouse.x, (int)host_.input().mouse.y)) {
            host_.ed().selectAllLayers();
            markDirty(frame_);
            return true;
        }
        return false;
    }

private:
    void armBlendBox(const Rect& box)
    {
        host_.armList(box, [this](int dir) {
            Editor& ed = host_.ed();
            if (ed.doc.active < 0) return;
            const int cur = (int)ed.doc.layers[(size_t)ed.doc.active].blend;
            const int next = std::clamp(cur + dir, 0, kBlendModeCount - 1);
            if (next == cur) return;
            ed.setLayerBlendMode(ed.doc.active, (BlendMode)next);
            markDirty(zones().modeBox);
        });
    }

private:
    struct Zones {
        Rect modeRow, modeBox, opRow, lockRow, buttons, list;
        int rowH = 30;
    };

    Zones zones() const
    {
        Zones z;
        z.rowH = 30;
        const int x = frame_.x + 8;
        const int w = std::max(0, frame_.w - 16);
        int y = frame_.y + 32;
        const int modeLw = (int)ui::textWidth("混合") + 10;
        z.modeRow = Rect{x, y, w, 24};
        z.modeBox = Rect{x + modeLw, y, std::max(0, w - modeLw), 24};
        z.opRow = Rect{x, y + 28, w, 24};
        z.lockRow = Rect{x, y + 56, w, 24};
        y += 84;
        z.buttons = Rect{x, y, ui::layerBtnTotalW(), 26};
        y += 32;
        z.list = Rect{x, y, w, std::max(0, frame_.bottom() - 6 - y)};
        return z;
    }

    Rect buttonRect(int i) const
    {
        const Zones z = zones();
        return Rect{z.buttons.x + ui::layerBtnOffset(i), z.buttons.y, ui::layerBtnW(i), z.buttons.h};
    }

    int buttonIndexAt(int px) const
    {
        for (int i = 0; i < ui::kLayerBtnCount; ++i) {
            const Rect r = buttonRect(i);
            if (px >= r.x && px < r.right()) return i;
        }
        return -1;
    }

    void openBlendPopup(float x, float y)
    {
        Editor& ed = host_.ed();
        if (ed.doc.active < 0) return;
        std::vector<std::string> items;
        for (int m = 0; m < kBlendModeCount; ++m) items.push_back(blendModeName((BlendMode)m));
        const int cur = (int)ed.doc.layers[(size_t)ed.doc.active].blend;
        int layer = ed.doc.active;
        host_.openListPopup(items, cur, x, y,
                            [this, layer](int pick) { this->host_.ed().setLayerBlendMode(layer, (BlendMode)pick); });
    }

    Rect opacityBox(const Zones& z) const
    {
        const int lw = (int)ui::textWidth("不透明度") + 10;
        return Rect{z.opRow.x + lw, z.opRow.y, std::max(1, z.opRow.w - lw), z.opRow.h - 2};
    }
    static double opacityRead(const void* ud)
    {
        const Host* h = (const Host*)ud;
        const Layer* l = h->ed().doc.activeLayer();
        return l ? (double)(l->opacity * 100.0f) : 0.0;
    }
    static void opacityWrite(void* ud, double v)
    {
        Host* h = (Host*)ud;
        if (h->ed().doc.active < 0) return;
        h->ed().setLayerOpacity(h->ed().doc.active, (float)(std::clamp(v, 0.0, 100.0) / 100.0));
    }
    void bindOpacity()
    {
        op_.get = &opacityRead;
        op_.set = &opacityWrite;
        op_.readUd = &host_;
        op_.writeUd = &host_;
        op_.lo = 0.0;
        op_.hi = 100.0;
        op_.step = 1.0;
        op_.fmt = nullptr;
    }

    int rowIndexAt(float y, const Zones& z) const
    {
        const int n = (int)host_.ed().doc.layers.size();
        if (n <= 0) return -1;
        const int row = ((int)y - z.list.y + scroll_) / z.rowH;
        if (row < 0) return n - 1;
        if (row >= n) return 0;
        return n - 1 - row;
    }

    void openLayerMenu(float x, float y)
    {
        static const Cmd cmds[] = {Cmd::MergeSelected, Cmd::MergeImage, Cmd::LayerRotate,
                                   Cmd::LayerCenter};
        static const char* labels[] = {"合并图层", "合并图像", "图层旋转", "图层居中"};
        std::vector<std::string> items(labels, labels + 4);
        host_.openCmdListPopup(items, std::vector<Cmd>(cmds, cmds + 4), x, y,
                               [this](int pick) {
                                   static const Cmd pickCmds[] = {Cmd::MergeSelected, Cmd::MergeImage,
                                                                  Cmd::LayerRotate, Cmd::LayerCenter};
                                   if (pick < 0 || pick >= 4) return;
                                   host_.execute(pickCmds[pick]);
                                   markDirty(frame_);
                               });
    }

    int scroll_ = 0;
    NumField op_;
    bool opHeld_ = false;
    int dragFrom_ = -1, dragTo_ = -1;
    float dragPressY_ = 0.0f;
    float dragX_ = 0.0f, dragY_ = 0.0f;
    float grabDx_ = 0.0f, grabDy_ = 0.0f;
    bool dragValid_ = false;
    bool dragging_ = false;
    int hoverBtn_ = -1;
};


class StatusBarNode : public Node {
public:
    explicit StatusBarNode(Host& h) : host_(h) {}

    Size requestSize() const override { return Size{0, dims::statusH}; }

    void paint(SkCanvas& c) override
    {
        const Theme& t = theme();
        fill(c, frame_, t.panel);
        fill(c, Rect{frame_.x, frame_.y, frame_.w, 1}, t.border);
        const Editor& e = host_.ed();
        char buf[128];
        char live[48];
        live[0] = 0;
        int lw = 0, lh = 0;
        if (host_.tool().liveSize(lw, lh))
            std::snprintf(live, sizeof(live), "   %d × %d", lw, lh);
        std::snprintf(buf, sizeof(buf), "%d x %d   %d%%%s", e.doc.width, e.doc.height,
                      host_.zoomPercent(), live);
        drawText(c, (float)(frame_.x + (frame_.w - (int)ui::textWidth(buf)) / 2),
                 (float)(frame_.y + (frame_.h - (int)lineHeight()) / 2), t.text, buf);
    }

    bool onPointer(const PointerEvent&) override { return false; }

private:
    Host& host_;
};

}

Rect hoverTipRect(Host& host)
{
    const std::string& text = host.hoverHint();
    const Rect a = host.hoverHintAnchor();
    if (text.empty() || a.empty()) return Rect{};
    const int padX = 8, padY = 4;
    const int bw = (int)ui::textWidth(text) + padX * 2;
    const int bh = (int)lineHeight() + padY * 2;
    const Size v = host.viewSize();
    Rect box{a.right() + 8, a.y + (a.h - bh) / 2, bw, bh};
    if (box.right() > v.w - 4) box.x = a.x - 8 - bw;
    if (box.x < 4) box.x = 4;
    if (box.y < 4) box.y = 4;
    if (box.bottom() > v.h - 4) box.y = std::max(4, v.h - 4 - bh);
    return box;
}

void paintHoverTip(SkCanvas& c, Host& host)
{
    const Rect box = hoverTipRect(host);
    if (box.empty()) return;
    const Theme& t = theme();
    const int padX = 8, padY = 4;
    fill(c, box, t.panelHi, (float)t.radius);
    stroke(c, box, t.border, 1.0f, (float)t.radius);
    drawText(c, (float)(box.x + padX), (float)(box.y + padY), t.text, host.hoverHint());
}

void paintDragGhost(SkCanvas& c, Host& host)
{
    if (!host.layerGhostOn()) return;
    const Rect box = host.layerGhostRect();
    if (box.empty()) return;
    const Theme& t = theme();
    fill(c, box, rgba(44, 44, 52, 235), (float)t.radius);
    stroke(c, box, t.accent, 1.0f, (float)t.radius);
    fill(c, Rect{box.x + 4, box.y + (box.h - 16) / 2, 16, 16}, rgba(230, 235, 245, 40), 2.0f);
    drawText(c, (float)(box.x + 26), (float)(box.y + (box.h - (int)lineHeight()) / 2), t.text,
             host.layerGhostText());
}

const char* layerBtnLabel(int i)
{
    switch (i) {
        case 0: return "新建";
        case 1: return "复制";
        case 2: return "删除";
        default: return "";
    }
}

int layerBtnW(int i)
{
    return std::max(56, (int)ui::textWidth(layerBtnLabel(i)) + 16);
}

int layerBtnOffset(int i)
{
    int x = 0;
    for (int k = 0; k < i && k < kLayerBtnCount; ++k) x += layerBtnW(k) + 6;
    return x;
}

int layerBtnTotalW()
{
    return layerBtnOffset(kLayerBtnCount - 1) + layerBtnW(kLayerBtnCount - 1);
}

Rect railSwatch(const Rect& frame)
{
    const int cell = std::clamp(frame.w - 16, 18, 36);
    const int h = cell * 3 + 2 * 4;
    return Rect{frame.x + (frame.w - cell) / 2, frame.bottom() - h - 10, cell, h};
}

void railSwatchCells(const Rect& frame, Rect& fg, Rect& bg)
{
    Rect swap{};
    railSwatchCells(frame, fg, bg, swap);
}

void swatchCellsIn(const Rect& a, Rect& fg, Rect& bg, Rect& swap)
{
    const int gap = 4;
    const int cell = std::clamp(std::min(a.w, (a.h - 2 * gap) / 3), 12, 40);
    const int x = a.x + (a.w - cell) / 2;
    const int y = a.y + (a.h - (3 * cell + 2 * gap)) / 2;
    fg = Rect{x, y, cell, cell};
    bg = Rect{x, y + cell + gap, cell, cell};
    swap = Rect{x, y + 2 * (cell + gap), cell, cell};
}

void railSwatchCells(const Rect& frame, Rect& fg, Rect& bg, Rect& swap)
{
    const Rect a = railSwatch(frame);
    swatchCellsIn(a, fg, bg, swap);
}

RailGrid railGrid(const Rect& frame, int count)
{
    RailGrid g;
    const int n = std::max(1, count);
    const Rect sw = railSwatch(frame);
    const int top = 6;
    const int avail = std::max(0, sw.y - frame.y - top - 6);
    const int byW = std::max(0, frame.w - 10);
    const int fit = (avail - (n - 1) * g.gap) / n;
    g.cols = 1;
    g.rows = n;
    g.cell = std::clamp(std::min(byW, fit), 8, 44);
    const int totalH = n * g.cell + (n - 1) * g.gap;
    g.area = Rect{frame.x + (frame.w - g.cell) / 2, frame.y + top, g.cell, totalH};
    return g;
}

Rect railSwatchDocked(const Rect& frame, int count)
{
    const Rect base = railSwatch(frame);
    const RailGrid g = railGrid(frame, count);
    Rect r = base;
    const int y = g.area.bottom() + 6;
    if (y < base.y) r.y = y;
    return r;
}

int railNeededH(int width, int count)
{
    Rect probe{0, 0, width, 2000};
    const int wantH = railGrid(probe, count).area.h;
    const int swH = railSwatch(Rect{0, 0, width, 0}).h;
    return 6 + wantH + 6 + swH + 10;
}

std::unique_ptr<Node> makeMenuBar(Host& host)
{
    return std::make_unique<MenuBarNode>(host);
}

std::unique_ptr<Node> makeTabBar(Host& host)
{
    return std::make_unique<TabBarNode>(host);
}

int propsLabelCol(const Props& props, int rowW)
{
    int col = 0;
    for (const Prop& q : props)
        if ((q.kind == PropKind::Float || q.kind == PropKind::Int || q.kind == PropKind::Combo ||
             q.kind == PropKind::Color) &&
            q.label)
            col = std::max(col, (int)textWidth(q.label) + 6);
    return std::min(col, std::max(0, rowW / 2));
}

std::unique_ptr<Node> makeOptionsBar(Host& host)
{
    return std::make_unique<OptionsBarNode>(host);
}

std::unique_ptr<Node> makeToolRail(Host& host)
{
    return std::make_unique<ToolRailNode>(host);
}


std::unique_ptr<Node> makeLayerPanel(Host& host)
{
    return std::make_unique<LayerPaneNode>(host);
}

std::unique_ptr<Node> makeStatusBar(Host& host)
{
    return std::make_unique<StatusBarNode>(host);
}


std::unique_ptr<Node> makeImagePanel(Host& host)
{
    return makeImagePanelImpl(host);
}

std::unique_ptr<Node> makeMenuPopup(Host& host, int menuIndex)
{
    if (menuIndex < 0 || menuIndex >= (int)menus().size()) return nullptr;
    return std::make_unique<MenuPopupNode>(host, menus()[(size_t)menuIndex].items, menuIndex);
}

std::unique_ptr<Node> makeCanvasMenu(Host& host)
{
    return std::make_unique<MenuPopupNode>(host, canvasItems());
}

std::unique_ptr<Node> makeListPopup(Host& host, std::vector<std::string> items, int current,
                                    std::function<void(int)> pick, std::vector<char> disabled)
{
    return std::make_unique<ListPopupNode>(host, std::move(items), current, std::move(pick),
                                           std::move(disabled));
}

std::unique_ptr<Node> makeCmdListPopup(Host& host, const std::vector<std::string>& items,
                                       const std::vector<Cmd>& cmds, std::function<void(int)> pick)
{
    return std::make_unique<ListPopupNode>(host, items, -1, std::move(pick), std::vector<char>(),
                                           cmds);
}

std::vector<std::string> menuItemTitles(const std::string& title)
{
    std::vector<std::string> out;
    for (const auto& m : menus()) {
        if (title != m.title) continue;
        for (const auto& it : m.items) out.push_back(it.label);
        break;
    }
    return out;
}

std::vector<std::string> menuTitles()
{
    std::vector<std::string> out;
    for (const auto& m : menus()) out.push_back(m.title);
    return out;
}

bool menuCmdEnabled(Host& host, Cmd cmd)
{
    return menuCmdEnabledImpl(host, cmd);
}

}
}
