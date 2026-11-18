#include "core/Blend.h"
#include "core/Transform.h"
#include "tools/Tool.h"
#include "core/Clipboard.h"
#include "core/Document.h"
#include "core/Matting.h"
#include "core/History.h"
#include "core/ImageIo.h"
#include "core/LayerStyle.h"
#include "core/Ora.h"
#include "core/ShadowLift.h"
#include "core/Primaries.h"
#include "tools/Paint.h"
#include "tools/Tools.h"
#include "app/Editor.h"
#include "app/SavePath.h"
#include "core/Selection.h"
#include "core/TextRender.h"
#include "core/Transform.h"
#include "nodes/CanvasNode.h"
#include "nodes/Container.h"
#include "nodes/Guides.h"
#include "nodes/ImagePanel.h"
#include "nodes/Widgets.h"
#include "host/Host.h"
#include "host/GlobalKeys.h"
#include "host/Shortcuts.h"
#include "host/Shortcuts.h"
#include "include/core/SkColorType.h"
#include "include/core/SkSurface.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkData.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkColor.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "[ ok ]" : "[FAIL]", what);
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

int runSelfTest()
{
    using namespace ie;

    const float kImgRowY0 = (float)(30 + 28 + 6) + (float)(30 - 6) / 2.0f;
    const float kImgRowY1 = kImgRowY0 + 30.0f;

    {
        Document doc;
        doc.createEmpty(8, 4, Color(0, 0, 0, 255));
        doc.addEmptyLayer("top");
        Layer* top = doc.activeLayer();
        top->img.detach();
        top->img.fillAll(Color(0, 0, 255, 255).packed());
        Image out(8, 4);
        doc.compositeInto(out.pixels(), out.width(), Rect{0, 0, 8, 4}, -1, nullptr);
        const Color c1 = Color::fromPacked(out.pixels()[0]);
        check(c1.r == 0 && c1.g == 0 && c1.b == 255 && c1.a == 255,
              "composite: 整行不透明时上层盖住下层（等于拷贝，over 的退化形式）");

        top->img.pixels()[10] = 0;
        doc.compositeInto(out.pixels(), out.width(), Rect{0, 0, 8, 4}, -1, nullptr);
        const Color c2 = Color::fromPacked(out.pixels()[10]);
        check(c2.r == 0 && c2.g == 0 && c2.b == 0 && c2.a == 255,
              "composite: 行里有一颗透明像素，就回到混合（透出下层黑）");

        Document off;
        off.createEmpty(8, 4, Color(0, 0, 0, 0));
        Layer* offL = off.activeLayer();
        offL->img.detach();
        offL->img.fillAll(0xFF204060u);
        offL->img.pixels()[2] = 0;
        offL->x = 10;
        offL->y = 0;
        Image offBuf(6, 4);
        OpaqueReport offRep;
        off.compositeInto(offBuf.pixels(), offBuf.width(), Rect{10, 0, 6, 4}, -1, &offRep);
        check(offRep.doc, "composite: 画布外的行不算画布有洞（负宽不再进 allOpaque）");
    }


    {
        const Color32 c = IE_COL32(72, 140, 240, 255);
        check(((c >> 24) & 0xff) == 255 && ((c >> 16) & 0xff) == 72 && ((c >> 8) & 0xff) == 140 &&
                  (c & 0xff) == 240,
              "color32: rgba() 按 0xAARRGGBB 打包");
    }

    {
        Image dst(4, 4);
        dst.fillAll(Color(0, 0, 255, 255).packed());
        Image src(4, 4);
        src.fillAll(Color(255, 0, 0, 255).packed());
        blend::over(dst.pixels(), src.pixels(), 16, 1.0f);
        const Color c = Color::fromPacked(dst.pixels()[0]);
        check(c.r == 255 && c.b == 0 && c.a == 255, "blend: 不透明 over 结果正确");
    }

    {
        Image dst(1, 1);
        dst.fillAll(Color(0, 0, 255, 255).packed());
        Image src(1, 1);
        src.fillAll(Color(255, 0, 0, 128).packed());
        blend::over(dst.pixels(), src.pixels(), 1, 1.0f);
        const Color c = Color::fromPacked(dst.pixels()[0]);
        check(c.r > 120 && c.r < 135 && c.b > 120 && c.b < 135, "blend: 50% over 结果正确");
    }
    {
        Image flat(32, 16);
        flat.fillAll(Color(120, 120, 120, 255).packed());
        const std::vector<uint32_t> flatBase(flat.pixels(), flat.pixels() + 32 * 16);
        ie::shadow::Params id;
        ie::shadow::removeShadow(flat, Rect{0, 0, 32, 16}, id);
        bool identity = true;
        for (size_t k = 0; k < flatBase.size(); ++k)
            if (flat.pixels()[k] != flatBase[k]) identity = false;
        check(identity, "去阴影: 强度 0 / 半径 0 时恒等");

        Image img(96, 48);
        img.fillAll(Color(200, 200, 200, 255).packed());
        for (int y = 4; y < 20; ++y)
            for (int x = 16; x < 32; ++x) img.pixels()[(size_t)y * 48 + x] = Color(120, 120, 120, 255).packed();
        img.pixels()[0] = Color(150, 60, 40, 128).packed();
        const uint32_t a0 = img.pixels()[0] & 0xFF000000u;
        ie::shadow::Params p;
        p.strength = 1.0f;
        p.radius = 16.0f;
        ie::shadow::removeShadow(img, Rect{0, 0, 48, 24}, p);
        const Color band = Color::fromPacked(img.pixels()[(size_t)24 * 96 + 48]);
        check(band.r > 135 && band.r <= 200, "去阴影: 阴影带被抬亮（不越过背景）");
        const Color bright = Color::fromPacked(img.pixels()[(size_t)24 * 96 + 10]);
        check(bright.r >= 195, "去阴影: 背景本身不被压暗也不被抬亮（提亮与去阴影的分界）");
        {
            Image flat2(64, 32);
            flat2.fillAll(Color(100, 100, 100, 255).packed());
            const std::vector<uint32_t> orig(flat2.pixels(), flat2.pixels() + 64 * 32);
            ie::shadow::Params q;
            q.strength = 1.0f;
            q.radius = 10.0f;
            ie::shadow::removeShadow(flat2, Rect{0, 0, 64, 32}, q);
            bool same = true;
            for (size_t k = 0; k < orig.size(); ++k)
                if (flat2.pixels()[k] != orig[k]) same = false;
            check(same, "去阴影: 均匀受光的图完全不变（不是整幅提亮）");
        }
        check((img.pixels()[0] & 0xFF000000u) == a0, "去阴影: alpha 不被改动");
        const Color neutral = Color::fromPacked(img.pixels()[(size_t)24 * 96 + 48]);
        check(neutral.r == neutral.g && neutral.g == neutral.b, "去阴影: 中性灰抬完仍然中性（三通道同倍率）");
    }

    {
        Editor ed;
        ed.newDocument(64, 64, Color(140, 140, 140, 255));
        Layer* l = ed.doc.activeLayer();
        for (int y = 16; y < 48; ++y)
            for (int x = 16; x < 48; ++x) l->img.pixels()[(size_t)y * 64 + x] = Color(60, 60, 60, 255).packed();
        l->img.pixels()[100] = Color(230, 230, 230, 255).packed();
        const std::vector<uint32_t> base(l->img.pixels(), l->img.pixels() + 64 * 64);
        const uint32_t bright = base[100];

        check(ed.beginPixelPreview("阴影"), "shadow 会话: 在可编辑层上能开起来");
        shadow::Params p;
        p.strength = 0.9f;
        p.radius = 10.0f;
        auto opP = [p](Image& img, Rect roi) { shadow::removeShadow(img, roi, p); };
        ed.previewPixels(opP);
        const uint32_t once = l->img.pixels()[(size_t)32 * 64 + 32];
        check(Color::fromPacked(once).r > 90, "shadow 会话: 预览把暗带抬了起来");
        ed.previewPixels(opP);
        check(l->img.pixels()[(size_t)32 * 64 + 32] == once, "shadow 会话: 同参数再预览一次结果不变（连拖不叠加）");
        shadow::Params strong = p;
        strong.strength = 1.0f;
        ed.previewPixels([strong](Image& img, Rect roi) { shadow::removeShadow(img, roi, strong); });
        check(Color::fromPacked(l->img.pixels()[(size_t)32 * 64 + 32]).r >=
                  Color::fromPacked(once).r, "shadow 会话: 加大强度抬得更多");
        check(l->img.pixels()[100] == bright, "shadow 会话: 比周围亮的那点不被改动");
        ed.cancelPixelPreview();
        Layer* after = ed.doc.activeLayer();
        bool restored = true;
        for (int k = 0; k < 64 * 64; ++k)
            if (after->img.pixels()[k] != base[(size_t)k]) restored = false;
        check(restored, "shadow 会话: 取消后逐字节复原");
        check(!ed.hist.canUndo(), "shadow 会话: 取消不留空历史步");

        check(ed.beginPixelPreview("阴影"), "shadow 会话: 取消后能再次打开");
        ed.previewPixels(opP);
        ed.commitPixelPreview();
        l = ed.doc.activeLayer();
        check(Color::fromPacked(l->img.pixels()[(size_t)32 * 64 + 32]).r > 90, "shadow 会话: 确定后保留结果");
        check(ed.hist.canUndo(), "shadow 会话: 确定后历史里正好一步");
        ed.undo();
        after = ed.doc.activeLayer();
        bool back = true;
        for (int k = 0; k < 64 * 64; ++k)
            if (after->img.pixels()[k] != base[(size_t)k]) back = false;
        check(back, "shadow 会话: 一步撤销退回原样");

        Image big(1280, 800);
        big.fillAll(Color(140, 140, 140, 255).packed());
        for (int y = 200; y < 600; ++y)
            for (int x = 400; x < 900; ++x) big.pixels()[(size_t)y * 1280 + x] = Color(60, 60, 60, 255).packed();
        shadow::Params perf;
        perf.strength = 1.0f;
        perf.radius = 60.0f;
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 5; ++k) shadow::removeShadow(big, Rect{0, 0, 1280, 800}, perf);
        const auto t1 = std::chrono::steady_clock::now();
        std::printf("[ info ]  shadow 去阴影一趟（1280x800 = 1.0 M 像素，半径 60px）= %ld ms/次\n",
                    (long)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 5 / 1000);
    }
    {
        primaries::Params id;
        Image img(4, 2);
        img.fillAll(Color(100, 100, 100, 255).packed());
        img.pixels()[1] = Color(200, 30, 60, 128).packed();
        const std::vector<uint32_t> orig(img.pixels(), img.pixels() + 8);
        primaries::apply(img, Rect{0, 0, 4, 2}, id);
        bool same = true;
        for (int i = 0; i < 8; ++i)
            if (img.pixels()[i] != orig[(size_t)i]) same = false;
        check(same, "primaries: 三根 100% 为恒等");

        primaries::Params red;
        red.r = 2.0f;
        img.fillAll(Color(100, 100, 100, 255).packed());
        primaries::apply(img, Rect{0, 0, 4, 2}, red);
        const Color c = Color::fromPacked(img.pixels()[0]);
        check(c.r == 200 && c.g == 100 && c.b == 100, "primaries: 红 200% 只把 R 翻倍，G/B 不动");
        check((img.pixels()[0] & 0xFF000000u) == 0xFF000000u, "primaries: alpha 原样");

        img.fillAll(Color(200, 10, 10, 255).packed());
        primaries::apply(img, Rect{0, 0, 4, 2}, red);
        check(Color::fromPacked(img.pixels()[0]).r == 255, "primaries: 超上限被夹在 255");

        primaries::Params redKeep = red;
        redKeep.keepLuma = true;
        img.fillAll(Color(100, 100, 100, 255).packed());
        primaries::apply(img, Rect{0, 0, 4, 2}, redKeep);
        const Color k = Color::fromPacked(img.pixels()[0]);
        const float lum = primaries::luma(k.r, k.g, k.b);
        check(std::fabs(lum - 100.0f) < 1.5f, "primaries: 保持亮度后明度回到 100");
        check(k.r > k.g && k.g == k.b, "primaries: 偏红但明度不变");

        Editor ed;
        ed.newDocument(48, 48, Color(100, 100, 100, 255));
        Layer* l = ed.doc.activeLayer();
        const std::vector<uint32_t> base2(l->img.pixels(), l->img.pixels() + 48 * 48);
        check(ed.beginPixelPreview("三色"), "primaries 会话: 能开起来");
        primaries::Params blue;
        blue.b = 1.5f;
        ed.previewPixels([blue](Image& img, Rect roi) { primaries::apply(img, roi, blue); });
        l = ed.doc.activeLayer();
        check(Color::fromPacked(l->img.pixels()[0]).b == 150, "primaries 会话: 预览真改像素");
        ed.cancelPixelPreview();
        Layer* after = ed.doc.activeLayer();
        bool back2 = true;
        for (int i = 0; i < 48 * 48; ++i)
            if (after->img.pixels()[i] != base2[(size_t)i]) back2 = false;
        check(back2 && !ed.hist.canUndo(), "primaries 会话: 取消后逐字节复原且不留空步");

        Image big(1280, 800);
        big.fillAll(Color(100, 100, 100, 255).packed());
        const auto t0 = std::chrono::steady_clock::now();
        for (int k2 = 0; k2 < 5; ++k2) primaries::apply(big, Rect{0, 0, 1280, 800}, redKeep);
        const auto t1 = std::chrono::steady_clock::now();
        const long keepMs = (long)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 5 / 1000;
        const auto t2 = std::chrono::steady_clock::now();
        for (int k2 = 0; k2 < 5; ++k2) primaries::apply(big, Rect{0, 0, 1280, 800}, red);
        const auto t3 = std::chrono::steady_clock::now();
        std::printf("[ info ]  三色预览一趟（1280x800）：不保持亮度 = %ld ms，保持亮度 = %ld ms\n",
                    (long)std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count() / 5 / 1000, keepMs);
    }
    {
        Editor ed;
        ed.newDocument(8, 8, Color(100, 100, 100, 255));
        Layer* l0 = ed.doc.activeLayer();
        const std::vector<uint32_t> base(l0->img.pixels(), l0->img.pixels() + 64);
        check(Color::fromPacked(ed.doc.flatten().pixels()[0]).r == 100, "tri 子对象: 初始合成 R=100");

        ed.beginChildEdit();
        ed.applyLayerTri(0, 2.0f, 1.0f, 1.0f, false);
        Layer* l1 = ed.doc.activeLayer();
        bool pixSame = true;
        for (int i = 0; i < 64; ++i)
            if (l1->img.pixels()[i] != base[(size_t)i]) pixSame = false;
        check(pixSame, "tri 子对象: 改参数后图层像素逐字节未动（不烘焙）");
        check(Color::fromPacked(ed.doc.flatten().pixels()[0]).r == 200, "tri 子对象: 合成结果 R=200");
        const int kid = ed.triChild(0) ? ed.triChild(0)->id : 0;
        check(kid > 0, "tri 子对象: 落到图层上时有存在ID");
        ed.applyLayerTri(0, 1.0f, 2.0f, 1.0f, false);
        check(ed.triChild(0) && ed.triChild(0)->id == kid, "tri 子对象: 反复改参数，ID 不变");

        ed.undo();
        check(ed.triChild(0) == nullptr, "tri 子对象: 撤销回到「这一层没有三色」的状态");
        check(Color::fromPacked(ed.doc.flatten().pixels()[0]).r == 100, "tri 子对象: 撤销后画面回到原样");

        ed.applyLayerTri(0, 3.0f, 0.5f, 0.5f, true);
        const int kid2 = ed.triChild(0) ? ed.triChild(0)->id : 0;
        ed.applyLayerTri(0, 1.0f, 1.0f, 1.0f, false);
        check(ed.triChild(0) && ed.triChild(0)->id == kid2 && ed.triChild(0)->identity(),
              "tri 子对象: 重置后参数回恒等、ID 仍留着");
        check(Color::fromPacked(ed.doc.flatten().pixels()[0]).r == 100, "tri 子对象: 重置后画面等于原样");

        Document d2;
        d2.createEmpty(8, 8, Color(60, 60, 60, 255));
        Image cimg(8, 8);
        cimg.fillAll(Color(200, 100, 100, 255).packed());
        d2.addLayerAt(std::move(cimg), 0, 0, "上层");
        d2.layers[1].children.push_back(LayerChild{ChildKind::Tri, d2.claimChildId(), true, 0.5f, 1.0f, 1.0f, false});
        const Image comp2 = d2.flatten();
        check(Color::fromPacked(comp2.pixels()[0]).r == 100, "tri 子对象: 只对这一层施加（上层 R 200×0.5=100）");
        check(d2.layers[1].img.pixels()[0] == Color(200, 100, 100, 255).packed(), "tri 子对象: 施加后源像素仍是 200");

        {
            Document fd;
            fd.createEmpty(16, 16, Color(100, 100, 100, 255));
            LayerChild fc;
            fc.id = fd.claimChildId();
            fc.r = 1.5f;
            fc.g = 0.75f;
            fc.keepLuma = true;
            fd.layers[0].children.push_back(fc);
            const std::string fp = "selftest_tri.ora";
            check(ora::save(fp, fd), "ora-tri: 含三色子对象保存");
            Document fr;
            check(ora::load(fp, fr), "ora-tri: 读回");
            const LayerChild* rc = fr.layers.empty() ? nullptr : (fr.layers[0].children.empty() ? nullptr : &fr.layers[0].children[0]);
            check(rc && rc->id == fc.id && std::abs(rc->r - 1.5f) < 0.01f && std::abs(rc->g - 0.75f) < 0.01f &&
                      rc->keepLuma && rc->kind == ChildKind::Tri,
                  "ora-tri: 存在ID 与三根参数一起还原");
            const Image fc2 = fr.flatten();
            check(Color::fromPacked(fc2.pixels()[0]).r >= Color::fromPacked(fc2.pixels()[0]).g,
                  "ora-tri: 读回后合成仍带这条调整");
            std::remove(fp.c_str());
        }

        Document md;
        md.createEmpty(8, 8, Color(50, 50, 50, 255));
        Image t8(8, 8);
        t8.fillAll(Color(100, 100, 100, 255).packed());
        md.addLayerAt(std::move(t8), 0, 0, "上层");
        md.layers[1].children.push_back(LayerChild{ChildKind::Tri, md.claimChildId(), true, 2.0f, 1.0f, 1.0f, false});
        md.mergeDown(1);
        check(md.layers.size() == 1 && md.layers[0].children.empty(), "merge: 合并后子对象被吃掉");
        check(Color::fromPacked(md.layers[0].img.pixels()[0]).r == 200, "merge: 三色在合并时烘进像素（结果等于所见）");
    }

    {
        Document doc;
        doc.createEmpty(64, 48, Color(10, 20, 30, 255));
        doc.addEmptyLayer("图层 2");
        Layer* l = doc.activeLayer();
        l->img.detach();
        l->img.fillAll(Color(200, 100, 50, 255).packed());
        doc.markAllDirty();
        const Rect d = doc.takeDirty();
        doc.recomposite(d);
        const Color c = Color::fromPacked(doc.compositeImage().pixels()[10 * 64 + 10]);
        check(c.r == 200 && c.g == 100 && c.b == 50, "document: 上层覆盖下层");

        l->visible = false;
        doc.markAllDirty();
        const Rect d2 = doc.takeDirty();
        doc.recomposite(d2);
        const Color c2 = Color::fromPacked(doc.compositeImage().pixels()[10 * 64 + 10]);
        check(c2.r == 10 && c2.g == 20 && c2.b == 30, "document: 隐藏图层恢复下层");
    }

    {
        Selection sel;
        sel.reset(100, 80);
        sel.setRect(Rect{10, 10, 50, 30});
        check(sel.segments().size() == 4, "selection: 矩形轮廓 4 条边");
        check(sel.bounds().w == 50 && sel.bounds().h == 30, "selection: 边界正确");
        check(sel.contains(20, 20) && !sel.contains(5, 5), "selection: 命中测试正确");
        sel.invert();
        check(!sel.contains(20, 20) && sel.contains(5, 5), "selection: 反选正确");
        sel.clear();
        check(!sel.active && sel.segments().empty(), "selection: 清除正确");
    }

    {
        Selection sel;
        sel.reset(100, 80);
        sel.setEllipse(Rect{10, 10, 50, 30});
        check(sel.active && sel.bounds().x == 10 && sel.bounds().w == 50 && sel.bounds().h == 30,
              "ellipse: 外接框 = 拖出的矩形");
        check(sel.contains(35, 25), "ellipse: 中心命中");
        check(!sel.contains(11, 11) && !sel.contains(58, 38), "ellipse: 外接矩形四角不命中（不是矩形近似）");
        check(sel.contains(35, 11), "ellipse: 上边缘中点命中（靠中心轴的窄处仍在圆内）");
        const size_t rectEdges = sel.segments().size();
        check(rectEdges > 4, "ellipse: 轮廓是多段折线（不是矩形快路径的 4 条边）");
        sel.addEllipse(Rect{30, 20, 20, 20});
        check(sel.contains(40, 30) && sel.contains(35, 25), "ellipse: 加选并集成立");
        sel.subtractEllipse(Rect{30, 20, 20, 20});
        check(!sel.contains(40, 30), "ellipse: 减选挖掉重叠区");
        sel.clear();
        check(!sel.active, "ellipse: 清除正确");
    }

    {
        Document doc;
        doc.createEmpty(32, 32, Color(0, 0, 0, 255));
        History hist;
        Selection sel;
        sel.reset(32, 32);
        hist.reset(doc);

        Layer* l = doc.activeLayer();
        hist.record(doc, sel, "绘制");
        l->img.detach();
        l->img.fillAll(Color(255, 255, 255, 255).packed());
        doc.markAllDirty();
        doc.recomposite(doc.takeDirty());

        const bool okBefore = Color::fromPacked(l->img.pixels()[0]).r == 255;
        hist.undo(doc, sel);
        const bool okAfter = Color::fromPacked(doc.activeLayer()->img.pixels()[0]).r == 0;
        check(okBefore && okAfter, "history: 撤销恢复像素");
        hist.redo(doc, sel);
        check(Color::fromPacked(doc.activeLayer()->img.pixels()[0]).r == 255, "history: 重做恢复像素");
    }

    {
        Document doc;
        doc.createEmpty(100, 100, Color(5, 5, 5, 255));
        doc.activeLayer()->img.detach();
        doc.activeLayer()->img.fillRect(Rect{50, 50, 10, 10}, Color(255, 255, 0, 255).packed());
        doc.resizeCanvas(60, 60, -40, -40);
        const Color c = Color::fromPacked(layerPixelAt(*doc.activeLayer(), 15, 15));
        check(c.r == 255 && c.g == 255, "document: 裁剪后像素位置正确");
    }

    {
        Document doc;
        doc.createEmpty(40, 20, Color(255, 255, 255, 255));
        doc.resizeImage(80, 40);
        check(doc.width == 80 && doc.height == 40, "document: 图像大小更新画布");
        check(doc.layers[0].img.width() == 80 && doc.layers[0].img.height() == 40, "document: 图像大小缩放图层");
        check(Color::fromPacked(layerPixelAt(doc.layers[0], 40, 20)).r == 255, "document: 图像大小保留内容");
    }

    {
        Image img(37, 23);
        uint32_t* p = img.pixels();
        for (int y = 0; y < 23; ++y)
            for (int x = 0; x < 37; ++x) p[y * 37 + x] = Color((uint8_t)(x * 6), (uint8_t)(y * 10), 90, 255).packed();

        const std::string path = "selftest_roundtrip.png";
        const bool saved = imageio::save(path, img, 95);
        Image back;
        const bool loaded = saved && imageio::load(path, back);
        bool same = loaded && back.width() == 37 && back.height() == 23;
        if (same) {
            for (int i = 0; i < 37 * 23; ++i) {
                const Color a = Color::fromPacked(p[i]);
                const Color b = Color::fromPacked(back.pixels()[i]);
                if (a.r != b.r || a.g != b.g || a.b != b.b) {
                    same = false;
                    break;
                }
            }
        }
        check(same, "imageio: PNG 保存/读取往返一致");
        std::remove(path.c_str());
    }

    {
        Image img(48, 32);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 48; ++x) img.pixels()[y * 48 + x] = Color(200, (uint8_t)(y * 7), 40, 255).packed();

        struct Case { const char* path; const char* name; int tol; };
        const Case cases[] = {
            {"selftest_rt.jpg", "JPEG", 12},
            {"selftest_rt.webp", "WebP", 4},
        };
        for (const Case& c : cases) {
            Image back;
            bool ok = imageio::save(c.path, img, 95) && imageio::load(c.path, back) && back.width() == 48 &&
                      back.height() == 32;
            if (ok) {
                for (int i = 0; i < 48 * 32 && ok; ++i) {
                    const Color a = Color::fromPacked(img.pixels()[i]);
                    const Color b = Color::fromPacked(back.pixels()[i]);
                    if (c.tol == 0) {
                        ok = a.r == b.r && a.g == b.g && a.b == b.b;
                    } else {
                        ok = std::abs((int)a.g - (int)b.g) <= c.tol * 2 && b.r > 100 && b.b < 100;
                    }
                }
            }
            if (!ok) {
                std::printf("       imageio: %s\n", ie::imageio::lastError().c_str());
                std::printf("       %dx%d", back.width(), back.height());
                if (!back.empty()) {
                    const ie::Color p0 = ie::Color::fromPacked(back.pixels()[0]);
                    std::printf(" p0=(%d,%d,%d,%d)", p0.r, p0.g, p0.b, p0.a);
                }
                std::printf("\n");
            }
            check(ok, (std::string("imageio: ") + c.name + " 保存/读取往返").c_str());
            std::remove(c.path);
        }
    }

    {
        Image img(64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                img.pixels()[y * 64 + x] = Color((uint8_t)(x * 4), (uint8_t)(y * 4), (uint8_t)((x + y) * 2), 255).packed();

        auto sizeOf = [](const char* p) {
            FILE* f = std::fopen(p, "rb");
            if (!f) return 0L;
            long n = 0;
            std::fseek(f, 0, SEEK_END);
            n = std::ftell(f);
            std::fclose(f);
            return n;
        };

        imageio::SaveOptions o;
        o.compression = 9;
        imageio::save("selftest_c9.png", img, o);
        o.compression = 0;
        imageio::save("selftest_c0.png", img, o);
        check(sizeOf("selftest_c9.png") > 0 && sizeOf("selftest_c9.png") <= sizeOf("selftest_c0.png"),
              "imageio: PNG 压缩级别 9 不比 0 更大");

        o.quality = 95;
        imageio::save("selftest_q95.jpg", img, o);
        o.quality = 30;
        imageio::save("selftest_q30.jpg", img, o);
        check(sizeOf("selftest_q30.jpg") > 0 && sizeOf("selftest_q30.jpg") < sizeOf("selftest_q95.jpg"),
              "imageio: JPEG 质量 30 比 95 更小");

        o.quality = 92;
        o.lossless = true;
        imageio::save("selftest_lossless.webp", img, o);
        Image back;
        bool ok = imageio::load("selftest_lossless.webp", back) && back.width() == 64 && back.height() == 64;
        for (int i = 0; ok && i < 64 * 64; ++i) {
            const Color a = Color::fromPacked(img.pixels()[i]);
            const Color b = Color::fromPacked(back.pixels()[i]);
            ok = a.r == b.r && a.g == b.g && a.b == b.b;
        }
        check(ok, "imageio: WebP 无损往返逐像素一致");

        check(!imageio::save("selftest_notbmp.bmp", img, o), "imageio: BMP 不再支持（保存被拒绝）");

        std::remove("selftest_c9.png");
        std::remove("selftest_c0.png");
        std::remove("selftest_q95.jpg");
        std::remove("selftest_q30.jpg");
        std::remove("selftest_lossless.webp");
    }

    {
        Image img(16, 16);
        img.fillAll(Color(10, 200, 30, 255).packed());
        const std::string path = "selftest_中文路径_测试.png";        const bool saved = imageio::save(path, img, 95);
        Image back;
        const bool loaded = saved && imageio::load(path, back);
        const bool ok = loaded && back.width() == 16 && back.height() == 16 &&
                        Color::fromPacked(back.pixels()[0]).g == 200;
        check(ok, "imageio: 中文路径保存/读取往返（UTF-8 路径）");
#ifdef _WIN32
        const int wn = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
        std::wstring wpath((size_t)wn, L'\0');
        if (wn > 0) MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wpath[0], wn);
        _wremove(wpath.c_str());
#else
        std::remove(path.c_str());
#endif
    }

    {
        Image cmyk;
        if (imageio::load("icc_test_cmyk.jpg", cmyk)) {
            check(cmyk.width() == 256 && cmyk.height() == 128, "icc: 带 ICC 的 CMYK JPEG 读成 256x128");
            check((cmyk.pixels()[(size_t)96 * 256 + 192] >> 24) == 0xffu, "icc: 转出后补满不透明 alpha");
            const Color k = Color::fromPacked(cmyk.pixels()[(size_t)96 * 256 + 192]);
            const Color c = Color::fromPacked(cmyk.pixels()[(size_t)32 * 256 + 64]);
            check(std::abs((int)k.r - 34) <= 4 && std::abs((int)k.g - 33) <= 4 && std::abs((int)k.b - 33) <= 4,
                  "icc: 黑版经 SWOP 配置转 sRGB（与 lcms 相对比色一致，±4）");
            check(std::abs((int)c.r - 230) <= 4 && std::abs((int)c.g - 52) <= 4 && std::abs((int)c.b - 45) <= 4,
                  "icc: 彩色版经配置转换后色相正确（相对比色，±4）");
        } else {
            check(false, "icc: icc_test_cmyk.jpg 缺失，ICC 路径未被验证");
        }
    }

    {
        Image src(48, 32);
        for (int i = 0; i < 48 * 32; ++i) src.pixels()[i] = Color(0, 200, 0, 255).packed();
        const std::string p = "selftest_place.png";
        imageio::save(p, src, 95);

        Editor e;
        e.newDocument(120, 80, Color(255, 255, 255, 255));
        const int before = (int)e.doc.layers.size();
        const bool ok = e.placeImageAsLayer(p);
        check(ok && (int)e.doc.layers.size() == before + 1, "drop: 置入图像成为当前文档的新图层（不新开标签）");
        const Layer& top = e.doc.layers.back();
        const Color c = Color::fromPacked(layerPixelAt(top, 60, 40));
        const Color outside = Color::fromPacked(layerPixelAt(top, 30, 20));
        check(top.img.width() == 48 && top.img.height() == 32 && top.x == 36 && top.y == 24 && c.g == 200 &&
                  outside.a == 0,
              "drop: 置入的图像按画布居中落位（图层自带尺寸+原点）");
        e.undo();
        check((int)e.doc.layers.size() == before, "drop: 置入图像可撤销");
        std::remove(p.c_str());
    }

    {
        Image img(64, 64);
        Rect dirty;
        Dab d;
        d.radius = 10.0f;
        d.hardness = 1.0f;
        d.opacity = 1.0f;
        stampDab(img, 0, 0, nullptr, 0, 0, dirty, 32, 32, d, Color(255, 0, 0, 255), false);
        const Color center = Color::fromPacked(img.pixels()[32 * 64 + 32]);
        const Color corner = Color::fromPacked(img.pixels()[2 * 64 + 2]);
        check(center.r == 255 && corner.a == 0, "paint: 笔尖命中中心且不污染远处");
        check(dirty.w > 0 && dirty.h > 0, "paint: 脏区域有效");

        stampDab(img, 0, 0, nullptr, 0, 0, dirty, 32, 32, d, Color(0, 0, 0, 255), true);
        check(Color::fromPacked(img.pixels()[32 * 64 + 32]).a == 0, "paint: 擦除清空 alpha");
    }

    {
        Editor ed;
        ed.newDocument(128, 128, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.panX = ed.view.panY = 0.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        auto tool = makeBrushTool();

        InputFrame in;
        in.mouse = Vec2f(30, 64);
        in.pressed[0] = true;
        tool->onPress(ed, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        for (int x = 31; x <= 90; ++x) {
            in.mouse = Vec2f((float)x, 64.0f);
            tool->onDrag(ed, in, 0);
        }
        in.down[0] = false;
        in.released[0] = true;
        tool->onRelease(ed, in, 0);

        const Rect d = ed.doc.takeDirty();
        ed.doc.recomposite(d);
        const Color mid = Color::fromPacked(ed.doc.compositeImage().pixels()[64 * 128 + 60]);
        const Color out = Color::fromPacked(ed.doc.compositeImage().pixels()[10 * 128 + 10]);
        check(mid.r < 80 && out.r == 255, "tool: 画笔笔画写入并合成");

        ed.undo();
        const Rect d2 = ed.doc.takeDirty();
        ed.doc.recomposite(d2);
        const Color back = Color::fromPacked(ed.doc.compositeImage().pixels()[64 * 128 + 60]);
        check(back.r == 255, "tool: 撤销后笔画消失");
    }

    {
        Editor ed;
        ed.newDocument(8, 8, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        check(ed.doc.layers.size() == 1 && Color::fromPacked(ed.doc.layers[0].img.pixels()[0]).a == 255,
              "新建: 新标签页底色不透明");
        ed.removeLayer(0);
        check(ed.doc.layers.size() == 1, "删除: 唯一图层被删后仍留一层（文档结构不塌）");
        check(Color::fromPacked(ed.doc.layers[0].img.pixels()[0]).a == 0,
              "删除: 唯一图层被删后画布变透明（白底消失）");
        check(ed.doc.layers[0].name == "图层 1", "删除: 唯一图层被删后改名为“图层 1”");
        check(ed.doc.valid(), "删除: 删完文档仍可保存（valid 不为假）");
        ed.undo();
        check(Color::fromPacked(ed.doc.layers[0].img.pixels()[0]).a == 255 && ed.doc.layers[0].name == "背景",
              "删除: 撤销后白底与原名一起回来");
        ed.redo();
        check(Color::fromPacked(ed.doc.layers[0].img.pixels()[0]).a == 0, "删除: 重做后又变透明");
    }


    {
        Editor ed;
        ed.newDocument(128, 128, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        const bool selEmptyBefore = ed.sel.empty();
        ed.view.zoom = 1.0f;
        ed.view.panX = ed.view.panY = 0.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        auto tool = makeMaskTool();

        InputFrame in;
        in.mouse = Vec2f(30, 64);
        in.pressed[0] = true;
        tool->onPress(ed, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        for (int x = 31; x <= 90; ++x) {
            in.mouse = Vec2f((float)x, 64.0f);
            tool->onDrag(ed, in, 0);
        }
        in.down[0] = false;
        in.released[0] = true;
        tool->onRelease(ed, in, 0);

        Layer* l = ed.doc.activeLayer();
        check(l && !l->uMask.empty(), "蒙版: 一笔画出图层蒙版（存在且与图层同尺寸）");
        check(l->uMask[(size_t)64 * 128 + 60] < 40, "蒙版: 笔画处被剔除（keep≈0）");
        check(l->uMask[(size_t)10 * 128 + 10] == 255, "蒙版: 笔画外仍完全保留");
        check(ed.hist.undoCount() == 1, "蒙版: 一笔只进一条撤销");

        const Rect d0 = ed.doc.takeDirty();
        ed.doc.recomposite(d0);
        const Color c0 = Color::fromPacked(ed.doc.compositeImage().pixels()[(size_t)64 * 128 + 60]);
        check(c0.a == 255 && c0.r == 255, "蒙版: 未应用前图像不被实时剪切（仍是原样）");
        check(ed.sel.empty() == selEmptyBefore, "蒙版: 不产出选区（选区状态没变）");

        ed.doc.applyUserMask(ed.doc.active);
        const Rect d1 = ed.doc.takeDirty();
        ed.doc.recomposite(d1);
        const Color c1 = Color::fromPacked(ed.doc.compositeImage().pixels()[(size_t)64 * 128 + 60]);
        const Color c2 = Color::fromPacked(ed.doc.compositeImage().pixels()[(size_t)10 * 128 + 10]);
        check(c1.a < 40, "蒙版: 应用后该处 alpha 真的变透明");
        check(c2.a == 255, "蒙版: 应用后未涂区域 alpha 不变");
        check(ed.doc.activeLayer()->uMask.empty(), "蒙版: 应用后蒙版清空（已烘焙进 alpha）");

        ed.undo();
        const Rect d2 = ed.doc.takeDirty();
        ed.doc.recomposite(d2);
        const Color c3 = Color::fromPacked(ed.doc.compositeImage().pixels()[(size_t)64 * 128 + 60]);
        check(c3.a == 255, "蒙版: 撤销后 alpha 回到原样");
        check(ed.doc.activeLayer()->uMask.empty(), "蒙版: 撤销回到“没有蒙版”的状态");
    }

    {
        Editor ed;
        ed.newDocument(128, 128, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        auto tool = makeMaskTool();

        InputFrame in;
        in.mouse = Vec2f(64, 64);
        in.pressed[0] = true;
        tool->onPress(ed, in, 0);
        in.pressed[0] = false;
        in.down[0] = false;
        in.released[0] = true;
        tool->onRelease(ed, in, 0);
        check(ed.doc.hasUserMask(ed.doc.active), "抠图: 一笔之后蒙版在");

        InputFrame key;
        key.key = KeyEnter;
        tool->onKey(ed, key);
        check(ed.doc.activeLayer()->uMask.empty(), "抠图: 回车等于「应用」，蒙版已烘焙");
        const Rect dd = ed.doc.takeDirty();
        ed.doc.recomposite(dd);
        check(Color::fromPacked(ed.doc.compositeImage().pixels()[(size_t)64 * 128 + 64]).a < 40,
              "抠图: 回车应用之后那个像素真的透了");
    }

    {
        Editor ed;
        ed.newDocument(128, 128, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        auto tool = makeMaskTool();

        InputFrame in;
        in.mouse = Vec2f(64, 64);
        in.pressed[0] = true;
        tool->onPress(ed, in, 0);
        in.pressed[0] = false;
        in.down[0] = false;
        in.released[0] = true;
        tool->onRelease(ed, in, 0);
        check(ed.doc.hasUserMask(ed.doc.active), "抠图: 换工具前蒙版在");

        tool->onLeave(ed);
        check(ed.doc.activeLayer()->uMask.empty(), "抠图: 换别的工具等于「清除」，蒙版丢掉");
        const Rect dd = ed.doc.takeDirty();
        check(!dd.empty() && dd.w == 128 && dd.h == 128,
              "抠图: 换工具时把整层标脏（不标的话青色要等缩放大刷新）");
        ed.doc.recomposite(dd);
        check(Color::fromPacked(ed.doc.compositeImage().pixels()[(size_t)64 * 128 + 64]).a == 255,
              "抠图: 换工具只是丢弃，图像本身没被裁");
    }

    {
        ie::Host host;
        host.ed().newDocument(128, 128, ie::Color(255, 255, 255, 255));
        host.ed().sel.reset(host.ed().doc.width, host.ed().doc.height);
        host.ed().view.zoom = 1.0f;
        host.ed().view.vpX = host.ed().view.vpY = 0.0f;
        host.ed().view.vpW = (float)host.ed().doc.width;
        host.ed().view.vpH = (float)host.ed().doc.height;

        auto bar = ie::ui::makeOptionsBar(host);
        host.focusNode(bar.get());

        int slot = -1;
        for (int i = 0; i < host.toolsCount(); ++i)
            if (host.toolId(i) == ie::ToolId::Mask) slot = i;
        host.setTool(slot);

        ie::InputFrame in;
        in.mouse = ie::Vec2f(64, 64);
        in.pressed[0] = true;
        host.tool().onPress(host.ed(), in, 0);
        in.pressed[0] = false;
        in.down[0] = false;
        in.released[0] = true;
        host.tool().onRelease(host.ed(), in, 0);
        check(host.ed().doc.hasUserMask(host.ed().doc.active), "抠图: 画了一笔");

        ie::KeyEvent k;
        k.pressed = true;
        k.scancode = ie::KeyEnter;
        host.dispatchKey(k);
        check(host.ed().doc.activeLayer()->uMask.empty(),
              "抠图: 焦点还在属性面板时回车也应用（不用先点画布）");
    }

    {
        ie::Host host;
        host.ed().newDocument(64, 64, ie::Color(0, 0, 0, 0));
        ie::Layer* l = host.ed().doc.activeLayer();
        l->img.detach();
        l->img.fillRect(ie::Rect{20, 20, 20, 20}, ie::Color(255, 0, 0, 255).packed());
        host.ed().doc.markAllDirty();
        auto bar = ie::ui::makeOptionsBar(host);
        host.focusNode(bar.get());

        ie::KeyEvent r;
        r.pressed = true;
        r.scancode = ie::KeyR;
        host.dispatchKey(r);
        check(host.ed().doc.width == 20 && host.ed().doc.height == 20,
              "快捷键: 焦点在面板时 R 照样裁切透明");

        host.ed().newDocument(64, 64, ie::Color(0, 0, 0, 255));
        host.ed().sel.setRect(ie::Rect{10, 10, 30, 20});
        ie::KeyEvent r2;
        r2.pressed = true;
        r2.scancode = ie::KeyR;
        host.dispatchKey(r2);
        check(host.ed().doc.width == 30 && host.ed().doc.height == 20,
              "快捷键: R 在有选区时裁切到选区（整幅不透明也不空转）");

        host.setTool(7);
        ie::KeyEvent v;
        v.pressed = true;
        v.scancode = ie::KeyV;
        host.dispatchKey(v);
        check(host.activeToolIndex() == 0, "快捷键: 焦点在面板时裸键 V 照样换工具（不只认画布）");
    }

    {
        Editor ed;
        ed.newDocument(64, 64, Color(255, 255, 255, 255));
        Layer* l = ed.doc.activeLayer();
        l->img = Image(64, 64);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                uint8_t rr = 210, gg = 60, bb = 60;
                if (x >= 36) { rr = 50; gg = 70; bb = 210; }
                else if (x >= 28) {
                    const float t = (float)(x - 28) / 8.0f;
                    rr = (uint8_t)(210 + (50 - 210) * t);
                    gg = (uint8_t)(60 + (70 - 60) * t);
                    bb = (uint8_t)(60 + (210 - 60) * t);
                }
                l->img.pixels()[(size_t)y * 64 + (size_t)x] = Color(rr, gg, bb, 255).packed();
            }
        }
        check(ed.doc.ensureUserMask(ed.doc.active), "matting: 蒙版建立");
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) l->uMask[(size_t)y * 64 + (size_t)x] = x < 32 ? 255 : 0;
        ed.doc.applyUserMask(ed.doc.active, 6);
        l = ed.doc.activeLayer();
        int midCount = 0;
        for (int x = 22; x <= 40; ++x) {
            const int a = (int)((l->img.pixels()[(size_t)32 * 64 + (size_t)x] >> 24) & 0xff);
            if (a > 8 && a < 248) ++midCount;
        }
        const uint32_t farPx = l->img.pixels()[(size_t)32 * 64 + 10];
        const uint32_t outPx = l->img.pixels()[(size_t)32 * 64 + 60];
        check(midCount >= 3, "matting: 半透明边缘解出中间 alpha（不是 0/255 硬切）");
        check(((farPx >> 24) & 0xff) == 255, "matting: 前景内部保持不透明");
        check(((outPx >> 24) & 0xff) == 0, "matting: 背景完全透明");
        check(l->uMask.empty(), "matting: 解完清空蒙版");
    }

    {
        ie::Image photo;
        if (ie::imageio::load("1.webp", photo)) {
            Editor ed;
            ed.newDocument(photo.width(), photo.height(), Color(0, 0, 0, 0));
            Layer* l = ed.doc.activeLayer();
            l->img = photo;
            ed.doc.ensureUserMask(ed.doc.active);
            const int w = photo.width(), h = photo.height();
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    l->uMask[(size_t)y * (size_t)w + (size_t)x] =
                        (x > w / 3 && x < w * 2 / 3 && y > h / 3) ? 0 : 255;
            const auto t0 = std::chrono::steady_clock::now();
            ed.doc.applyUserMask(ed.doc.active, 12);
            const auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            int soft = 0;
            const uint32_t* px = ed.doc.activeLayer()->img.pixels();
            for (size_t i = 0; i < (size_t)w * (size_t)h; ++i) {
                const int a = (int)((px[i] >> 24) & 0xff);
                if (a > 8 && a < 247) ++soft;
            }
            printf("MATTE %dx%d %.0f ms soft=%d\n", w, h, ms, soft);
            check(ms < 8000.0, "matting: 1024 级实图 8 秒内完成");
            check(soft > 200, "matting: 实图边缘解出软 alpha 像素");
            const int cutA = (int)((px[(size_t)(h * 3 / 4) * (size_t)w + (size_t)(w / 2)] >> 24) & 0xff);
            check(cutA < 40, "matting: 实图被涂区域变透明");
        }
    }

    {
        Editor ed;
        ed.newDocument(64, 16, Color(0, 0, 0, 0));
        Layer* l = ed.doc.activeLayer();
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 64; ++x) {
                const int t = x < 30 ? 0 : (x > 33 ? 255 : (x - 30) * 85);
                l->img.pixels()[(size_t)y * 64 + (size_t)x] = Color(255, (uint8_t)(t / 4), (uint8_t)t, 255).packed();
            }
        ed.doc.ensureUserMask(ed.doc.active);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 64; ++x)
                l->uMask[(size_t)y * 64 + (size_t)x] = x < 32 ? 255 : 0;
        const uint32_t before = l->img.pixels()[(size_t)8 * 64 + 32];
        ed.doc.applyUserMask(ed.doc.active, 6);
        const uint32_t after = ed.doc.activeLayer()->img.pixels()[(size_t)8 * 64 + 32];
        const int bIn = (int)((before >> 16) & 0xff);
        const int bOut = (int)((after >> 16) & 0xff);
        printf("DECON before_b=%d after_b=%d\n", bIn, bOut); fflush(stdout);
        check(bOut < bIn, "matting: 边缘去掉背景色混入（去光晕）");
    }


    {
        Editor ed;
        ed.newDocument(128, 128, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.panX = ed.view.panY = 0.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        auto tool = makeEraserTool();

        InputFrame in;
        in.mouse = Vec2f(30, 64);
        in.pressed[0] = true;
        tool->onPress(ed, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        for (int x = 31; x <= 90; ++x) {
            in.mouse = Vec2f((float)x, 64.0f);
            tool->onDrag(ed, in, 0);
        }
        in.down[0] = false;
        in.released[0] = true;
        tool->onRelease(ed, in, 0);

        const Rect d = ed.doc.takeDirty();
        ed.doc.recomposite(d);
        const Color mid = Color::fromPacked(ed.doc.compositeImage().pixels()[64 * 128 + 60]);
        check(mid.a < 60, "eraser: 笔画擦掉 alpha");
        ed.undo();
        const Rect d2 = ed.doc.takeDirty();
        ed.doc.recomposite(d2);
        check(Color::fromPacked(ed.doc.compositeImage().pixels()[64 * 128 + 60]).a > 200, "eraser: 撤销后恢复");
    }

    {
        Editor ed;
        ed.newDocument(64, 64, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.panX = ed.view.panY = 0.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        const float fg[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        ed.palette.setFg(fg);

        auto tool = makeShapeTool();
        ie::Props ps;
        tool->props(ps, ed);
        for (auto& p : ps) {
            if (p.kind == ie::PropKind::Bool && std::strcmp(p.label, "填充") == 0 && p.setV)
                p.setV(0.0, p.setUd);
            if ((p.kind == ie::PropKind::Float || p.kind == ie::PropKind::Int) && std::strcmp(p.label, "线宽") == 0 &&
                p.setV)
                p.setV(1.0, p.setUd);
        }

        InputFrame in;
        in.mouse = Vec2f(10, 10);
        in.pressed[0] = true;
        tool->onPress(ed, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        in.mouse = Vec2f(50, 50);
        tool->onDrag(ed, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        tool->onRelease(ed, in, 0);

        const Rect d = ed.doc.takeDirty();
        ed.doc.recomposite(d);
        const auto& px = ed.doc.compositeImage().pixels();
        int dark = 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (ie::Color::fromPacked(px[y * 64 + x]).r < 40) ++dark;
        check(dark >= 120, "shape: 线宽 1px 真的画出 1px 的线（不是空白）");
    }


    {
        Editor ed;
        ed.newDocument(64, 64, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.panX = ed.view.panY = 0.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        Layer& l = ed.doc.layers[0];
        l.style.strokeOn = true;
        l.style.strokeWidth = 6.0f;
        l.style.strokePos = StrokePos::Outside;
        l.style.strokeColor[0] = l.style.strokeColor[1] = l.style.strokeColor[2] = 0.0f;
        l.style.strokeColor[3] = 1.0f;

        auto tool = makeEraserTool();
        InputFrame in;
        in.mouse = Vec2f(32, 32);
        in.pressed[0] = true;
        tool->onPress(ed, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        for (int d = 1; d <= 20; ++d) {
            in.mouse = Vec2f(32.0f + (float)d, 32.0f + (float)d);
            tool->onDrag(ed, in, 0);
        }
        in.down[0] = false;
        in.released[0] = true;
        tool->onRelease(ed, in, 0);

        const Rect d3 = ed.doc.takeDirty();
        ed.doc.recomposite(d3);
        int ox = 0, oy = 0;
        const Image& st = styledPixels(ed.doc.layers[0], ox, oy);
        check(ox < 0 && st.width() > 64, "eraser: 带描边的层擦完仍能取出样式图");
    }

    {
        Editor ed;
        ed.newDocument(100, 100, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.panX = ed.view.panY = 0.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        auto tool = makeShapeTool();

        InputFrame in;
        in.mouse = Vec2f(20, 20);
        in.pressed[0] = true;
        tool->onPress(ed, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        in.mouse = Vec2f(60, 50);
        tool->onDrag(ed, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        tool->onRelease(ed, in, 0);

        const Rect d = ed.doc.takeDirty();
        ed.doc.recomposite(d);
        const Color edge = Color::fromPacked(ed.doc.compositeImage().pixels()[20 * 100 + 40]);
        const Color inside = Color::fromPacked(ed.doc.compositeImage().pixels()[35 * 100 + 40]);
        check(edge.a > 0 && inside.a == 255 && inside.r == 255, "tool: 形状描边沿边缘且不误填内部");
    }

    {
        Editor ed;
        ed.newDocument(100, 100, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.panX = ed.view.panY = 0.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        auto tool = makeShapeTool();

        Props props;
        tool->props(props, ed);
        for (auto& p : props)
            if (p.kind == PropKind::Combo) *p.sel = 2;

        auto drag = [&](float x0, float y0, float x1, float y1) {
            InputFrame in;
            in.mouse = Vec2f(x0, y0);
            in.pressed[0] = true;
            tool->onPress(ed, in, 0);
            in.pressed[0] = false;
            in.down[0] = true;
            in.mouse = Vec2f(x1, y1);
            tool->onDrag(ed, in, 0);
            in.down[0] = false;
            in.released[0] = true;
            tool->onRelease(ed, in, 0);
            const Rect d = ed.doc.takeDirty();
            ed.doc.recomposite(d);
        };
        auto px = [&](int x, int y) { return Color::fromPacked(ed.doc.compositeImage().pixels()[y * 100 + x]); };

        drag(20, 20, 60, 50);
        check(px(40, 20).r < 100 && px(30, 35).r < 100 && px(40, 50).r < 100 && px(40, 40).r == 255 &&
                  px(20, 20).r == 255,
              "tool: 三角形描边沿三边，不误填内部、不占矩形角");

        for (auto& p : props)
            if (p.kind == PropKind::Bool) *p.b = true;
        drag(20, 60, 60, 90);
        check(px(40, 80).r < 100 && px(20, 60).r == 255, "tool: 三角形填充覆盖内部、不占矩形角");
    }

    {
        Selection sel;
        sel.reset(64, 64);
        sel.setRect(Rect{20, 20, 24, 24});
        Image img(64, 64);
        blend::strokeMask(img, 0, 0, sel.maskPtr(), 64, 64, 4, Color(255, 0, 0, 255));
        const bool edgePainted = Color::fromPacked(img.pixels()[20 * 64 + 32]).a > 0;
        const bool centerEmpty = Color::fromPacked(img.pixels()[32 * 64 + 32]).a == 0;
        check(edgePainted && centerEmpty, "blend: 选区描边命中边缘且不填内部");
    }

    {
        Document doc;
        doc.createEmpty(64, 64, Color(0, 0, 0, 0));
        Image patch(10, 6);
        patch.fillAll(Color(255, 0, 0, 255).packed());
        doc.addLayerAt(std::move(patch), 20, 30, "补丁");
        const Layer* l = doc.activeLayer();
        const bool sizeOk = l && l->img.width() == 10 && l->img.height() == 6 && l->x == 20 && l->y == 30;
        const bool hit = sizeOk && Color::fromPacked(l->img.pixels()[5 * 10 + 5]).r == 255;
        const bool miss = sizeOk && (layerPixelAt(*l, 10, 30) >> 24) == 0;
        check(sizeOk && hit && miss, "document: addLayerAt 贴入指定原点，图层自带尺寸（PS 模型）");
    }

    {
        Document doc;
        doc.createEmpty(64, 64, Color(0, 0, 0, 0));
        Image big(100, 100);
        big.fillAll(Color(0, 128, 255, 255).packed());
        doc.addLayerAt(std::move(big), -18, -18, "大图");
        const Layer* l = doc.activeLayer();

        const bool sizeOk = l && l->img.width() == 100 && l->img.height() == 100 && l->x == -18 && l->y == -18;
        const bool inCanvas = sizeOk && (layerPixelAt(*l, 5, 5) >> 24) == 255;
        const bool outsideKept = sizeOk && (layerPixelAt(*l, -5, -5) >> 24) == 255;
        Image world(20, 20);
        doc.compositeInto(world.pixels(), 20, Rect{-10, -10, 20, 20}, -1);
        const bool worldOk = (world.pixels()[0] >> 24) == 255;
        const Image flat = doc.flatten();
        const bool flatBounded = flat.width() == 64 && flat.height() == 64;

        check(sizeOk && inCanvas && outsideKept && worldOk && flatBounded,
              "document: 越界像素保留在图层里，导出只取画布（PS 语义）");
    }

    {
        std::vector<uint32_t> buf(1000, 0xFF224466u);
        check(blend::allOpaque(buf.data(), buf.size()), "opaque: 整幅 alpha=255 判为不透明");
        bool tailOk = true;
        for (size_t n : {size_t(1), size_t(3), size_t(17), size_t(33), size_t(64), size_t(999)}) {
            buf[n - 1] = 0;
            if (blend::allOpaque(buf.data(), n)) tailOk = false;
            buf[n - 1] = 0xFF224466u;
        }
        check(tailOk, "opaque: 各长度下末尾单个透明像素都被发现（扫尾不漏）");
        check(!blend::allOpaque(nullptr, 10) && !blend::allOpaque(buf.data(), 0),
              "opaque: 看不见像素时答『有透明』，保守走慢路径");

        Document doc;
        doc.createEmpty(64, 64, Color(0, 0, 0, 0));
        Image bg(64, 64);
        bg.fillAll(Color(9, 9, 9, 255).packed());
        doc.addLayerAt(std::move(bg), 0, 0, "底");

        Image world(100, 100);
        OpaqueReport wide;
        doc.compositeInto(world.pixels(), 100, Rect{-18, -18, 100, 100}, -1, &wide);
        const bool widePix = (world.pixels()[0] >> 24) == 0 && (world.pixels()[18 * 100 + 18] >> 24) == 255;
        check(wide.doc && !wide.all && widePix, "opaque: 场景比画布大 —— 画布那块不透明，整幅有透明");

        OpaqueReport exact;
        Image canvasImg(64, 64);
        doc.compositeInto(canvasImg.pixels(), 64, Rect{0, 0, 64, 64}, -1, &exact);
        check(exact.all && exact.doc, "opaque: 合成矩形正好是画布且被盖满 —— 两问都是");

        doc.activeLayer()->img.fillRect(Rect{30, 30, 4, 4}, 0);
        OpaqueReport hole;
        doc.compositeInto(canvasImg.pixels(), 64, Rect{0, 0, 64, 64}, -1, &hole);
        check(!hole.all && !hole.doc && (canvasImg.pixels()[31 * 64 + 31] >> 24) == 0,
              "opaque: 画布内有洞 → 两问都否，棋盘格必须留着");

        doc.activeLayer()->img.fillRect(Rect{30, 30, 4, 4}, Color(9, 9, 9, 255).packed());
        doc.activeLayer()->opacity = 0.5f;
        OpaqueReport semi;
        doc.compositeInto(canvasImg.pixels(), 64, Rect{0, 0, 64, 64}, -1, &semi);
        check(!semi.all && !semi.doc && (canvasImg.pixels()[0] >> 24) == 128,
              "opaque: 图层不透明度 50% 时结果不算不透明");
    }

    {
        OverlayRecording ra, rb;
        OverlayRecorder a(&ra), b(&rb);
        const Color32 c = IE_COL32(255, 255, 255, 255);

        a.line(0, 0, 10, 10, c, 1.0f, true);
        check(overlayOpSame(ra, 0, ra, 0), "overlay: 同一张表和自己比一定算没变（不然每帧都在白重画）");
        b.line(0, 0, 11, 10, c, 1.0f, true);
        check(!overlayOpSame(ra, 0, rb, 0), "overlay: 端点动了就算变了");

        ra.reset(); rb.reset();
        const Vec2f p1[3] = {{0, 0}, {5, 5}, {10, 10}};
        const Vec2f p2[3] = {{0, 0}, {2, 8}, {10, 10}};
        a.polyline(p1, 3, c, 1.0f, false);
        b.polyline(p2, 3, c, 1.0f, false);
        check(!overlayOpSame(ra, 0, rb, 0), "overlay: 点池引用要真比内容（只比下标 = 有残影）");

        ra.reset(); rb.reset();
        a.text(5, 5, c, "ab");
        b.text(5, 5, c, "cd");
        check(!overlayOpSame(ra, 0, rb, 0), "overlay: 字池引用也一样要真比内容");

        ra.reset(); rb.reset();
        a.line(0, 0, 10, 0, c, 3.0f, true);
        b.line(0, 0, 10, 0, c, 1.0f, true);
        check(ra.ops[0].bounds.h > rb.ops[0].bounds.h,
              "overlay: 包围盒含住描边宽度（粗线的脏区要比细线的大）");

        Editor ed;
        ed.newDocument(200, 120, Color(255, 0, 0, 255));
        auto tools = createTools();
        InputFrame in;
        CanvasNode node;
        node.setEditor(&ed);
        node.setTool(tools[0].get());
        node.setInputSource(&in);
        node.place(Rect{0, 0, 400, 300}, Rect{0, 0, 400, 300});
        ed.fitIfPending();
        std::vector<Rect> placeRects;
        node.takeDirty(placeRects);

        const auto screenOf = [&](Rect img) {
            const Vec2f p = ed.view.toScreen((float)img.x, (float)img.y, ed.doc.width, ed.doc.height);
            const Vec2f q = ed.view.toScreen((float)img.right(), (float)img.bottom(), ed.doc.width, ed.doc.height);
            return Rect::fromLTRB((int)std::floor(p.x), (int)std::floor(p.y),
                                  (int)std::ceil(q.x), (int)std::ceil(q.y));
        };

        const Rect oldSel = screenOf(Rect{8, 8, 4, 4});
        ed.sel.setRect(Rect{8, 8, 4, 4});
        node.syncOverlay();
        std::vector<Rect> rects;
        rects.clear(); node.takeDirty(rects);
        check(!rects.empty(), "overlay: 轮廓第一次出现 → 画布自己宣布脏（改前没有这条通道，一块都不标）");

        node.syncOverlay();
        rects.clear(); node.takeDirty(rects);
        check(rects.empty(), "overlay: 输入没变就不该再标一次（每帧重记一张一模一样的表 = 白重画）");

        const Rect newSel = screenOf(Rect{120, 70, 4, 4});
        ed.sel.setRect(Rect{120, 70, 4, 4});
        node.syncOverlay();
        rects.clear(); node.takeDirty(rects);
        bool coversOld = false, coversNew = false;
        Rect u{};
        for (const Rect& r : rects) {
            if (!r.intersect(oldSel).empty()) coversOld = true;
            if (!r.intersect(newSel).empty()) coversNew = true;
            u = u.unite(r);
        }
        check(coversOld && coversNew, "overlay: 选区挪走 → 旧轮廓那块要擦、新轮廓那块要画（两块都标）");
        const Rect frame = node.frame();
        std::printf("       [diag] overlay 自报: 移动一次选区 → %zu 块脏, 并集 %d,%d %dx%d（画布 %dx%d）\n",
                    rects.size(), u.x, u.y, u.w, u.h, frame.w, frame.h);
        check(!rects.empty() && 3 * u.w < 2 * frame.w && 3 * u.h < 2 * frame.h,
              "overlay: 一次选区变化不许退化成整块画布脏（那条 hatch 就是干这个的）");
    }

    {
        Editor ed;
        ed.newDocument(80, 80, Color(0, 0, 0, 0));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.doc.activeLayer()->img.detach();
        ed.doc.activeLayer()->img.fillRect(Rect{30, 40, 8, 8}, Color(0, 255, 0, 255).packed());

        ed.sel.setRect(Rect{30, 40, 8, 8});
        ed.copySelection();
        ed.paste();
        const Layer* l = ed.doc.activeLayer();
        const bool nameOk = l && l->name == "粘贴" && l->x == 30 && l->y == 40 && l->img.width() == 8;
        const bool hit = nameOk && Color::fromPacked(layerPixelAt(*l, 34, 43)).g == 255;
        const bool behind = (layerPixelAt(ed.doc.layers[0], 34, 43) >> 24) != 0;
        check(nameOk && hit && behind, "editor: 复制→粘贴回原位且生成新图层");
    }

    {
        CanvasViewState v;
        bool roundtrip = true;
        const int W = 1000, H = 500;
        for (float z = 0.05f; z <= 8.0f; z *= 1.7f) {
            v.zoom = z;
            v.vpX = 60.0f;
            v.vpY = 52.0f;
            v.vpW = 800.0f;
            v.vpH = 600.0f;
            v.panX = 13.0f;
            v.panY = -7.0f;
            const Vec2f p = v.toImage(321.0f, 234.0f, W, H);
            const Vec2f s = v.toScreen(p.x, p.y, W, H);
            if (std::abs(s.x - 321.0f) > 0.01f || std::abs(s.y - 234.0f) > 0.01f) roundtrip = false;
        }
        check(roundtrip, "view: toImage/toScreen 在任意缩放下互逆");
    }

    {
        CanvasViewState v;
        const int W = 400, H = 200;
        v.vpX = 0.0f;
        v.vpY = 0.0f;
        v.vpW = 600.0f;
        v.vpH = 400.0f;
        v.zoom = 2.0f;
        v.rotationDeg = 90.0f;
        const Vec2f c0 = v.toScreen(0.0f, 0.0f, W, H);
        const Vec2f c1 = v.toScreen((float)W, 0.0f, W, H);
        const float len = std::sqrt((c1.x - c0.x) * (c1.x - c0.x) + (c1.y - c0.y) * (c1.y - c0.y));
        const bool isometry = std::abs(len - (float)W * v.zoom) < 0.01f;
        bool inverse = true;
        for (float y = 0.0f; y <= (float)H; y += 25.0f) {
            const Vec2f s = v.toScreen(0.0f, y, W, H);
            const Vec2f p = v.toImage(s.x, s.y, W, H);
            if (std::abs(p.x) > 0.01f || std::abs(p.y - y) > 0.01f) inverse = false;
        }
        const SkPoint m = v.matrix(W, H).mapXY(10.0f, 10.0f);
        const Vec2f s10 = v.toScreen(10.0f, 10.0f, W, H);
        const bool oneSource = std::abs(m.x() - s10.x) < 0.001f && std::abs(m.y() - s10.y) < 0.001f;
        CanvasViewState d;
        d.zoom = 2.0f;
        d.rotationDeg = 45.0f;
        d.scaleY = 1.5f;
        d.skewX = 0.2f;
        d.rotationDeg = 0.0f;
        d.scaleY = 1.0f;
        d.skewX = 0.0f;
        const bool resetOk = d.axisAligned() && d.zoom == 2.0f;
        check(isometry && inverse && oneSource && resetOk,
              "view: CTM 旋转 90° 后保长、逆矩阵互逆、toScreen 与 matrix() 同一份来源、复位归恒等");
    }

    {
        const std::string fontPath = textFontPath();
        if (!fontPath.empty()) {
            Editor ed;
            ed.newDocument(200, 200, Color(0, 0, 0, 0));
            ed.sel.reset(ed.doc.width, ed.doc.height);
            ed.view.zoom = 1.0f;
            ed.view.vpX = ed.view.vpY = 0.0f;
            ed.view.vpW = (float)ed.doc.width;
            ed.view.vpH = (float)ed.doc.height;
            auto tool = makeTextTool();
            InputFrame in;
            in.mouse = Vec2f(40, 60);
            in.pressed[0] = true;
            tool->onPress(ed, in, 0);



            check(tool->canvasTextActive(), "text: 点击后进入画布内编辑（会话已打开）");
            char* buf = tool->canvasTextBuffer();
            std::snprintf(buf, (size_t)tool->canvasTextCapacity(), "文字");
            ed.requestTextCommit();
            tool->update(ed, in);
            check(!tool->canvasTextActive(), "text: 提交后编辑会话关闭");

            const Rect d = ed.doc.takeDirty();
            ed.doc.recomposite(d);
            auto litCount = [&]() {
                long n = 0;
                const Image& c = ed.doc.compositeImage();
                for (int i = 0, N = 200 * 200; i < N; ++i)
                    if ((c.pixels()[i] >> 24) > 40) ++n;
                return n;
            };
            const long lit = litCount();
            check(lit > 100, "text: 文字栅格化写入真实笔画");


            const int ti = ed.doc.active;
            check(ti >= 0 && ed.doc.layers[(size_t)ti].kind == LayerKind::Text,
                  "text: 文字落成文本对象（可再编辑，不是烘焙图片）");

            ed.setTextContent(ti, "文字文字文字");
            ed.doc.recomposite(ed.doc.takeDirty());
            const long lit2 = litCount();
            check(lit2 > lit, "text: 改内容后重排，笔画随之增加");

            const float white[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            ed.applyTextStyle(ti, 32, 6.0f, 1.0f, white);
            ed.doc.recomposite(ed.doc.takeDirty());
            const long lit3 = litCount();
            check(lit3 > 0 && lit3 < lit2, "text: 改字号后重排，笔画范围随之变化");

            ed.undo();
            ed.doc.recomposite(ed.doc.takeDirty());
            check(litCount() == lit, "text: 文本编辑可撤销（回到上一版内容与样式）");





            {
                auto analyze = [](const Image& img, long& ring, long& adj, long& gap) {
                    ring = adj = gap = 0;
                    auto cls = [](uint32_t p) {
                        const int r = (int)(p & 0xffu), g = (int)((p >> 8) & 0xffu), b = (int)((p >> 16) & 0xffu);


                        if (r > 200 && g > 200 && b > 200) return 0;
                        if (r > 120 && g < 80 && b < 80) return 2;
                        if (r < 70 && g < 70 && b < 70) return 1;
                        return 3;
                    };
                    const int W = img.width(), H = img.height();
                    for (int y = 0; y < H; ++y)
                        for (int x = 0; x < W; ++x) {
                            const int c = cls(img.pixels()[(size_t)y * W + x]);
                            if (c == 2) ++ring;
                            if (c != 0) continue;

                            auto at = [&](int dx, int dy) {
                                const int nx = x + dx, ny = y + dy;
                                if (nx < 0 || ny < 0 || nx >= W || ny >= H) return 0;
                                return cls(img.pixels()[(size_t)ny * W + nx]);
                            };
                            const int l1 = at(-1, 0), r1 = at(1, 0), u1 = at(0, -1), d1 = at(0, 1);
                            if ((l1 == 1 && r1 == 2) || (l1 == 2 && r1 == 1) || (u1 == 1 && d1 == 2) ||
                                (u1 == 2 && d1 == 1))
                                ++gap;
                        }
                    for (int y = 0; y < H; ++y)
                        for (int x = 0; x < W; ++x) {
                            if (cls(img.pixels()[(size_t)y * W + x]) != 1) continue;
                            auto at = [&](int dx, int dy) {
                                const int nx = x + dx, ny = y + dy;
                                if (nx < 0 || ny < 0 || nx >= W || ny >= H) return 0;
                                return cls(img.pixels()[(size_t)ny * W + nx]);
                            };
                            if (at(-1, 0) == 2 || at(1, 0) == 2 || at(0, -1) == 2 || at(0, 1) == 2) ++adj;
                        }
                };
                auto build = [&](float ox, float oy, int size) {
                    Document g;
                    g.createEmpty(120, 96, Color(255, 255, 255, 255));
                    TextData gt;
                    gt.text = "Ag";
                    gt.size = size;
                    gt.x = 30.0f + ox;
                    gt.y = 26.0f + oy;
                    gt.color = Color(0, 0, 0, 255);
                    g.addTextLayer(gt, "gap");
                    LayerStyle& gs = g.layers[1].style;
                    gs.strokeOn = true;
                    gs.strokeWidth = 5.0f;
                    gs.strokePos = StrokePos::Outside;
                    gs.strokeColor[0] = 1.0f;
                    gs.strokeColor[1] = 0.0f;
                    gs.strokeColor[2] = 0.0f;
                    gs.strokeColor[3] = 1.0f;
                    return g.flatten();
                };
                {
                    const Image img = build(0.0f, 0.0f, 32);
                    long ring = 0, adj = 0, gap = 0;
                    analyze(img, ring, adj, gap);
                    std::printf("[gap] 整数落笔: 描边=%ld 相邻=%ld 夹白=%ld\n", ring, adj, gap);
                    check(ring > 0, "text: 描边真的画出来了（成品里有描边色像素）");
                    check(adj > 0, "text: 墨与描边在成品里相邻（不是被一条白缝隔开）");
                    check(gap == 0, "text: 墨与描边之间没有夹白像素（整数落笔时没有 1px 缝）");

                    {
                        Document sd;
                        sd.createEmpty(120, 96, Color(255, 255, 255, 255));
                        TextData st2;
                        st2.text = "Ag";
                        st2.size = 32;
                        st2.x = 30.0f;
                        st2.y = 26.0f;
                        st2.color = Color(0, 0, 0, 255);
                        sd.addTextLayer(st2, "same");
                        LayerStyle& ss = sd.layers[1].style;
                        ss.strokeOn = true;
                        ss.strokeWidth = 4.0f;
                        ss.strokePos = StrokePos::Outside;
                        ss.strokeColor[0] = 1.0f;
                        ss.strokeColor[3] = 1.0f;
                        ss.shadowOn = true;
                        ss.shadowDist = 4.0f;
                        ss.shadowSize = 6.0f;
                        ss.shadowColor[0] = 0.0f;
                        ss.shadowColor[3] = 1.0f;
                        const Image exp = sd.flatten();
                        Image scr(120, 96);
                        sd.compositeScreenInto(scr.pixels(), 120, Rect{0, 0, 120, 96}, nullptr);
                        sd.drawTextOnto(scr, SkMatrix::I());
                        long diff = 0;
                        for (int y = 0; y < 96; ++y)
                            for (int x = 0; x < 120; ++x) {
                                const uint32_t a = exp.pixels()[(size_t)y * 120 + (size_t)x];
                                const uint32_t b = scr.pixels()[(size_t)y * 120 + (size_t)x];
                                if (Color::fromPacked(a).r != Color::fromPacked(b).r ||
                                    Color::fromPacked(a).a != Color::fromPacked(b).a)
                                    ++diff;
                            }
                        char sl[200];
                        std::snprintf(sl, sizeof sl, "text-style: 屏幕上带描边投影的字与导出逐像素一致 不一致=%ld/11520",
                                      diff);
                        check(diff == 0, sl);
                    }

                    {
                        Document td2;
                        td2.createEmpty(160, 120, Color(0, 0, 0, 0));
                        TextData t2;
                        t2.text = "H";
                        t2.size = 72;
                        t2.x = 40.0f;
                        t2.y = 80.0f;
                        t2.color = Color(128, 128, 128, 255);
                        td2.addTextLayer(t2, "三色");
                        LayerChild tr;
                        tr.kind = ChildKind::Tri;
                        tr.id = 901;
                        tr.on = true;
                        tr.r = 2.0f;
                        tr.g = 1.0f;
                        tr.b = 0.5f;
                        td2.layers[1].children.push_back(tr);
                        td2.refreshTextLayers();
                        const Image tri = td2.flatten();
                        int mr = 0, mg = 0, mb = 0;
                        for (int y = 0; y < tri.height(); ++y)
                            for (int x = 0; x < tri.width(); ++x) {
                                const Color c = Color::fromPacked(tri.pixels()[(size_t)y * tri.width() + (size_t)x]);
                                if (c.a < 200) continue;
                                mr = std::max(mr, (int)c.r);
                                mg = std::max(mg, (int)c.g);
                                mb = std::max(mb, (int)c.b);
                            }
                        char tl2[200];
                        std::snprintf(tl2, sizeof tl2, "text-tri: 三色作用于字形填充 R=%d G=%d B=%d（期望约 255/128/64）",
                                      mr, mg, mb);
                        check(mr >= 250 && mg >= 120 && mg <= 136 && mb >= 56 && mb <= 72, tl2);

                        LayerChild tr2 = tr;
                        tr2.keepLuma = true;
                        td2.layers[1].children[0] = tr2;
                        td2.refreshTextLayers();
                        const Image tri2 = td2.flatten();
                        double ar = 0, ag = 0, ab = 0;
                        long cnt = 0;
                        for (int y = 0; y < tri2.height(); ++y)
                            for (int x = 0; x < tri2.width(); ++x) {
                                const Color c = Color::fromPacked(tri2.pixels()[(size_t)y * tri2.width() + (size_t)x]);
                                if (c.a < 200) continue;
                                ar += (double)c.r;
                                ag += (double)c.g;
                                ab += (double)c.b;
                                ++cnt;
                            }
                        const double yr = cnt ? ar / (double)cnt : 0.0;
                        const double yg = cnt ? ag / (double)cnt : 0.0;
                        const double yb = cnt ? ab / (double)cnt : 0.0;
                        const double yl = 0.299 * yr + 0.587 * yg + 0.114 * yb;
                        char tl3[220];
                        std::snprintf(tl3, sizeof tl3,
                                      "text-tri: 保留亮度时三色仍保住亮度 平均=(%.1f,%.1f,%.1f) 亮度=%.1f（原 128）", yr, yg, yb,
                                      yl);
                        check(cnt > 100 && yl >= 123.0 && yl <= 133.0, tl3);

                        td2.layers[1].children.clear();
                        LayerStyle& ov = td2.layers[1].style;
                        ov.overlayOn = true;
                        ov.overlayOpacity = 50.0f;
                        ov.overlayColor[0] = 1.0f;
                        ov.overlayColor[1] = 0.0f;
                        ov.overlayColor[2] = 0.0f;
                        td2.refreshTextLayers();
                        const Image ovImg = td2.flatten();
                        double orr = 0, ogg = 0, obb = 0;
                        long ocnt = 0;
                        for (int y = 0; y < ovImg.height(); ++y)
                            for (int x = 0; x < ovImg.width(); ++x) {
                                const Color c = Color::fromPacked(ovImg.pixels()[(size_t)y * ovImg.width() + (size_t)x]);
                                if (c.a < 200) continue;
                                orr += (double)c.r;
                                ogg += (double)c.g;
                                obb += (double)c.b;
                                ++ocnt;
                            }
                        const double avgR = ocnt ? orr / (double)ocnt : 0.0;
                        const double avgG = ocnt ? ogg / (double)ocnt : 0.0;
                        const double avgB = ocnt ? obb / (double)ocnt : 0.0;
                        char tl4[220];
                        std::snprintf(tl4, sizeof tl4,
                                      "text-overlay: 颜叠按半透明混到字形 (%.1f,%.1f,%.1f)（期望约 191/64/64）", avgR, avgG,
                                      avgB);
                        check(ocnt > 100 && avgR >= 185.0 && avgR <= 197.0 && avgG >= 58.0 && avgG <= 70.0 &&
                              avgB >= 58.0 && avgB <= 70.0,
                              tl4);
                    }



                    Document tg;
                    tg.createEmpty(120, 96, Color(255, 255, 255, 255));
                    TextData tt;
                    tt.text = "Ag";
                    tt.size = 32;
                    tt.color = Color(0, 0, 0, 255);
                    tg.addTextLayer(tt, "drag");
                    LayerStyle& ts = tg.layers[1].style;
                    ts.strokeOn = true;
                    ts.strokeWidth = 5.0f;
                    ts.strokePos = StrokePos::Outside;
                    ts.strokeColor[0] = 1.0f;
                    ts.strokeColor[1] = 0.0f;
                    ts.strokeColor[2] = 0.0f;
                    ts.strokeColor[3] = 1.0f;
                    ts.shadowOn = true;
                    ts.shadowDist = 8.0f;
                    ts.shadowAngle = 45.0f;
                    ts.shadowSize = 20.0f;
                    const Image before = tg.flatten();
                    for (int k = 0; k < 4; ++k) {


                        tg.layers[1].text.x += 5.0f;
                        tg.layers[1].text.y += 3.0f;
                        tg.invalidateTextLayers();
                        tg.refreshTextLayers();
                        tg.markAllDirty();
                        tg.recomposite(tg.takeDirty());
                    }
                    const Image moved = tg.flatten();
                    int sox = 0, soy = 0;
                    const Image& st = styledPixels(tg.layers[1], sox, soy);
                    const int cw = st.width(), ch = st.height();
                    long veil = 0, border = 0, veilMax = 0;
                    for (int y = 0; y < ch; ++y)
                        for (int x = 0; x < cw; ++x) {
                            if (!(x == 0 || y == 0 || x == cw - 1 || y == ch - 1)) continue;
                            ++border;
                            const uint32_t a = st.pixels()[(size_t)y * (size_t)cw + x] >> 24;
                            if (a) {
                                ++veil;
                                if ((long)a > veilMax) veilMax = (long)a;
                            }
                        }
                    imageio::save("selftest_textdrag.png", moved, 95);
                    std::printf("[veil] 拖动中样式缓存 %dx%d 边缘不透明=%ld/%ld 最大alpha=%ld\n", cw, ch, veil,
                                border, veilMax);
                    check(veilMax <= 2, "text: 拖动中样式缓存边缘无可见底（带阴影时允许 1-2 级模糊尾巴）");

                    auto inkBox = [](const Image& im) {
                        Rect b{};
                        const int W = im.width(), H = im.height();
                        for (int y = 0; y < H; ++y)
                            for (int x = 0; x < W; ++x) {
                                const uint32_t p = im.pixels()[(size_t)y * W + x];
                                const int r = (int)(p & 0xffu), g = (int)((p >> 8) & 0xffu),
                                          bl = (int)((p >> 16) & 0xffu);
                                if (r < 60 && g < 60 && bl < 60) b = b.unite(Rect{x, y, 1, 1});
                            }
                        return b;
                    };
                    const Rect cbB = inkBox(before), cbM = inkBox(moved);
                    const int shiftX = cbM.x - cbB.x, shiftY = cbM.y - cbB.y;
                    std::printf("[drag] 位移=%d,%d 墨框=%d,%d->%d,%d\n", shiftX, shiftY, cbB.x, cbB.y, cbM.x,
                                cbM.y);
                    check(std::abs(shiftX - 20) <= 1 && std::abs(shiftY - 12) <= 1,
                          "text: 拖动时字形跟着层原点走（缓存不错位）");


                    {
                        long r3 = 0, a3 = 0, gg3 = 0;
                        analyze(moved, r3, a3, gg3);
                        std::printf("[gap] 拖动帧: 描边=%ld 相邻=%ld 夹白=%ld\n", r3, a3, gg3);
                        check(r3 > 0 && a3 > 0 && gg3 == 0, "text: 拖动中的成品也没有 1px 缝");
                    }


                    {
                        Document zg;
                        zg.createEmpty(120, 96, Color(255, 255, 255, 255));
                        TextData zt2;
                        zt2.text = "Ag";
                        zt2.size = 32;
                        zt2.x = 30.0f;
                        zt2.y = 26.0f;
                        zt2.color = Color(0, 0, 0, 255);
                        zg.addTextLayer(zt2, "zoom");
                        LayerStyle& zs = zg.layers[1].style;
                        zs.strokeOn = true;
                        zs.strokeWidth = 5.0f;
                        zs.strokePos = StrokePos::Outside;
                        zs.strokeColor[0] = 1.0f;
                        zs.strokeColor[1] = 0.0f;
                        zs.strokeColor[2] = 0.0f;
                        zs.strokeColor[3] = 1.0f;
                        for (int k = 0; k < 3; ++k) {
                            const float sc = (k == 0) ? 2.0f : (k == 1) ? 4.0f : 8.0f;
                            const Image zi = zg.flattenScaled(sc);
                            long r4 = 0, a4 = 0, g4 = 0;
                            analyze(zi, r4, a4, g4);
                            std::printf("[gapz] s=%.0f 描边=%ld 相邻=%ld 夹白=%ld\n", sc, r4, a4, g4);
                            check(r4 > 0 && a4 > 0 && g4 == 0, "text: 放大导出后也没有 1px 缝");
                        }
                    }
                }
                const int skHits = textSkiaPathHits();
                check(skHits > 0, "text: 字形轮廓来自 SkScalerContext（SkFont::getPath），不是兜底路径");


                const long styleHits = textStyleHits();
                std::printf("[style] Skia 直绘次数=%ld\n", styleHits);
                check(styleHits > 0, "text: 文字样式真的走了 Skia 直绘（没静默退回掩码法）");



                {
                    const Image img = build(0.37f, 0.42f, 23);
                    long ring = 0, adj = 0, gap = 0;
                    analyze(img, ring, adj, gap);
                    check(ring > 0 && adj > 0 && gap == 0,
                          "text: 亚像素落笔时也没有 1px 缝（轮廓与栅格字形同格）");
                }




                {
                    Document zd;
                    zd.createEmpty(140, 70, Color(255, 255, 255, 255));
                    TextData zt;
                    zt.text = "Il|8";
                    zt.size = 24;
                    zt.x = 12.0f;
                    zt.y = 12.0f;
                    zt.color = Color(0, 0, 0, 255);
                    zd.addTextLayer(zt, "放大文字");
                    const Image a = zd.flatten();
                    const Image b = zd.flattenScaled(2.0f);
                    check(b.width() == a.width() * 2 && b.height() == a.height() * 2,
                          "text: 2 倍导出尺寸是 1 倍的整两倍");
                    int diff = 0, area = 0;
                    for (int y = 0; y < b.height(); ++y)
                        for (int x = 0; x < b.width(); ++x) {
                            const uint32_t up = a.pixels()[(size_t)(y / 2) * a.width() + (x / 2)];
                            if (b.pixels()[(size_t)y * b.width() + x] != up) ++diff;
                            ++area;
                        }
                    check(area > 0 && diff > area / 200, "text: 2 倍导出是重新栅格（不是把 1 倍位图拉大）");



                    const Rect rg{10, 8, 60, 40};
                    const Image full = zd.flattenScaled(2.0f);
                    const Image part = zd.flattenRegionScaled(2.0f, rg);
                    check(part.width() == rg.w * 2 && part.height() == rg.h * 2,
                          "text: 区域重烤尺寸 = 区域 × 2");
                    int mism = 0, cnt = 0;
                    for (int y = 0; y < part.height(); ++y)
                        for (int x = 0; x < part.width(); ++x) {
                            const uint32_t a1 = part.pixels()[(size_t)y * part.width() + x];
                            const uint32_t a2 = full.pixels()[(size_t)(rg.y * 2 + y) * full.width() +
                                                              (rg.x * 2 + x)];
                            if (a1 != a2) ++mism;
                            ++cnt;
                        }
                    {
                        char lb2[200];
                        std::snprintf(lb2, sizeof lb2, "text: 区域重烤与整张重烤逐像素一致 不一致=%d/%d 区域=%d,%d 层=%d,%d", mism, cnt, rg.x, rg.y, 
                                      zd.layers[1].x, zd.layers[1].y);
                        check(cnt > 0 && mism * 100 < cnt, lb2);
                    }



                    {
                        Document pd;
                        pd.createEmpty(768, 1024, Color(255, 255, 255, 255));
                        TextData pt2;
                        pt2.text = "Performance 性能 测试";
                        pt2.size = 32;
                        pt2.x = 100.0f;
                        pt2.y = 100.0f;
                        pt2.color = Color(0, 0, 0, 255);
                        pd.addTextLayer(pt2, "perf");
                        LayerStyle& ps = pd.layers[1].style;
                        ps.strokeOn = true;
                        ps.strokeWidth = 4.0f;
                        ps.shadowOn = true;
                        ps.shadowSize = 6.0f;
                        auto bench = [&](float z, const Rect& rg3, const char* tag) {
                            const auto t0 = std::chrono::steady_clock::now();
                            Image o;
                            for (int i = 0; i < 3; ++i) o = pd.flattenRegionScaled(z, rg3);
                            const auto ms = std::chrono::duration<double, std::milli>(
                                                std::chrono::steady_clock::now() - t0)
                                                .count() /
                                            3.0;
                            std::printf("[perf] %s: %.2f ms/次  %dx%d\n", tag, ms, o.width(), o.height());
                            return ms;
                        };
                        bench(2.0f, Rect{60, 60, 375, 280}, "zoom2 视口 375x280");
                        bench(8.0f, Rect{100, 90, 200, 120}, "zoom8 视口 200x120");
                        bench(64.0f, Rect{110, 96, 25, 14}, "zoom64 视口 25x14");

                        ps.strokeOn = false;
                        ps.shadowOn = false;
                        bench(8.0f, Rect{100, 90, 200, 120}, "zoom8 无样式");
                        bench(64.0f, Rect{110, 96, 25, 14}, "zoom64 无样式");
                        ps.strokeOn = true;
                        ps.shadowOn = true;
                        check(true, "perf: 区域重烤基线已记录");




                        {
                            Document gd;
                            gd.createEmpty(768, 1024, Color(255, 255, 255, 255));
                            TextData gt;
                            gt.text = "Ag";
                            gt.size = 32;
                            gt.x = 100.0f;
                            gt.y = 100.0f;
                            gt.color = Color(0, 0, 0, 255);
                            gd.addTextLayer(gt, "gap");
                            LayerStyle& gs = gd.layers[1].style;
                            gs.strokeOn = true;
                            gs.strokeWidth = 4.0f;
                            gs.strokePos = StrokePos::Outside;
                            gs.strokeColor[0] = 255;
                            gs.strokeColor[1] = 0;
                            gs.strokeColor[2] = 0;
                            gs.strokeColor[3] = 255;
                            const Rect gr{92, 92, 60, 40};
                            const float s = 8.0f;
                            const Image full = gd.flattenScaled(s);
                            const Image part = gd.flattenRegionScaled(s, gr);
                            int mism = 0, cnt = 0, fx = -1, fy = -1;
                            for (int y = 0; y < part.height(); ++y)
                                for (int x = 0; x < part.width(); ++x) {
                                    const uint32_t a1 = part.pixels()[(size_t)y * part.width() + x];
                                    const uint32_t a2 = full.pixels()[(size_t)(gr.y * 8 + y) * full.width() +
                                                                      (gr.x * 8 + x)];
                                    if (a1 != a2) {
                                        ++mism;
                                        if (fx < 0) { fx = x; fy = y; }
                                    }
                                    ++cnt;
                                }
                            std::printf("[cmp] 带样式 区域vs整张 s=8: 像素=%d 不一致=%d 首个=(%d,%d)\n", cnt, mism, fx,
                                        fy);
                            check(cnt > 0 && mism * 100 < cnt,
                                  "text: 带样式（描边+投影）时区域重烤与整张重烤一致");
                        }
                    }
                }
            }

        } else {
            std::printf("[skip]  text: 无系统中文字体，跳过\n");
        }
    }

    {
        const std::string fontPathM = textFontPath();
        if (!fontPathM.empty()) {
            Editor e3;
            e3.newDocument(220, 180, Color(0, 0, 0, 0));
            TextData t;
            t.text = "文字";
            t.x = 40.0f;
            t.y = 40.0f;
            t.size = 32;
            e3.doc.addTextLayer(t, "文字");
            e3.doc.refreshTextLayers();
            Layer& l = e3.doc.layers.back();
            const bool wasText = l.kind == LayerKind::Text && !l.img.empty();
            SkPath p0;
            const bool hasPath = textGlyphPath(l.text, p0) && !p0.isEmpty();
            float dx = -1.0f, dy = -1.0f, dw = -1.0f, dh = -1.0f;
            bool geoOk = false;
            bool stillText = false;
            if (hasPath) {
                const SkRect b0 = p0.getBounds();
                const float cx = b0.centerX(), cy = b0.centerY();
                SkMatrix want = SkMatrix::RotateDeg(20.0f, {cx, cy});
                l.textXf = want;
                e3.doc.refreshTextLayers();
                stillText = l.kind == LayerKind::Text && l.text.text == "文字" && !l.img.empty();
                SkPath p1;
                if (stillText && textGlyphPath(l.text, p1) && !p1.isEmpty()) {
                    SkPath tp;
                    p1.transform(want, &tp);
                    const SkRect b1 = tp.getBounds();
                    dx = std::fabs((float)l.x - b1.x());
                    dy = std::fabs((float)l.y - b1.y());
                    dw = std::fabs((float)l.img.width() - b1.width());
                    dh = std::fabs((float)l.img.height() - b1.height());
                    geoOk = dx <= 2.5f && dy <= 2.5f && dw <= 4.0f && dh <= 4.0f;
                }
            }
            char lbl[240];
            std::snprintf(lbl, sizeof lbl,
                          "text: 变形仍是文字层（was=%d still=%d），图层落在变换后字形包围盒上 dx=%.2f dy=%.2f dw=%.2f "
                          "dh=%.2f geo=%d",
                          (int)wasText, (int)stillText, dx, dy, dw, dh, (int)geoOk);
            check(wasText && stillText && geoOk, lbl);
        }
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(0, 0, 0, 0));
        Layer* l = e.doc.activeLayer();
        l->img.detach();
        l->img.pixels()[10 * 64 + 10] = Color(255, 0, 0, 255).packed();

        e.beginMove();
        e.updateMove(12, 7, false);
        e.endMove();

        const Layer& L = e.doc.layers[0];
        const bool offsetOk = L.x == 12 && L.y == 7;
        const Color moved = Color::fromPacked(layerPixelAt(L, 22, 17));
        const Color hole = Color::fromPacked(layerPixelAt(L, 10, 10));
        check(offsetOk && moved.r == 255 && moved.a == 255 && hole.a == 0,
              "move: 整层移动只改偏移（零像素搬运），源处透明、目标处拿到像素");
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(0, 0, 0, 0));
        Layer* l = e.doc.activeLayer();
        l->img.detach();
        l->img.pixels()[10 * 64 + 10] = Color(255, 0, 0, 255).packed();
        l->img.pixels()[40 * 64 + 40] = Color(0, 255, 0, 255).packed();
        e.sel.setRect(Rect{8, 8, 8, 8});

        e.beginMove();
        e.updateMove(20, 0, false);
        e.endMove();

        const Layer& L = e.doc.layers[0];
        const uint32_t* p = L.img.pixels();
        check(L.x == 20 && L.y == 0, "move: 有选区时移动也是整层搬走（选区不实时剪切）");
        check(Color::fromPacked(p[10 * 64 + 10]).r == 255 && Color::fromPacked(p[40 * 64 + 40]).g == 255,
              "move: 层内像素一个没少（选区没挖洞）");
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(0, 0, 0, 0));
        Layer* l = e.doc.activeLayer();
        l->img.detach();
        l->img.pixels()[20 * 64 + 20] = Color(0, 0, 255, 255).packed();

        e.beginMove();
        e.updateMove(5, 5, true);
        e.endMove();

        const bool twoLayers = e.doc.layers.size() == 2;
        const bool kept = twoLayers && (layerPixelAt(e.doc.layers[0], 20, 20) >> 24) == 255;
        const bool copyMoved = twoLayers && (layerPixelAt(e.doc.layers[1], 25, 25) >> 24) == 255;
        check(twoLayers && kept && copyMoved, "move: Alt 拖动整层 = 复制图层再移动副本，源像素保留");
    }

    {
        Document doc;
        doc.createEmpty(4, 4, Color(128, 128, 128, 255));
        doc.addLayerAt(Image(4, 4), 0, 0, "上");
        Layer& top = doc.layers[1];
        top.img.detach();
        top.img.fillAll(Color(128, 128, 128, 255).packed());
        top.blend = BlendMode::Multiply;
        doc.markAllDirty();
        const Rect all = doc.takeDirty();
        doc.recomposite(all);
        const Color c = Color::fromPacked(doc.compositeImage().pixels()[0]);
        check(std::abs((int)c.r - 64) <= 2, "blend: 正片叠底 128×128 约为 64");
    }

    {
        const uint32_t dstPx = Color(77, 150, 210, 255).packed();
        const uint32_t srcPx = Color(200, 90, 60, 255).packed();
        int distinct = 0;
        uint32_t seen[ie::kBlendModeCount] = {};
        for (int m = 0; m < ie::kBlendModeCount; ++m) {
            const uint32_t v = ie::blend::compositePixel(dstPx, srcPx, 1.0f, (ie::BlendMode)m);
            bool dup = false;
            for (int k = 0; k < distinct; ++k)
                if (seen[k] == v) dup = true;
            if (!dup) seen[distinct++] = v;
        }
        const std::string allMsg = std::string("blend all: 18 个模式各算各的（不同结果 ") +
                                   std::to_string(distinct) + "/" + std::to_string(ie::kBlendModeCount) + "）";
        check(distinct >= 15, allMsg.c_str());

        const uint32_t sWhite = Color(255, 255, 255, 255).packed();
        const uint32_t sGray = Color(128, 128, 128, 255).packed();
        const uint32_t sBlack = Color(0, 0, 0, 255).packed();
        const auto pick = [](uint32_t px) { return Color::fromPacked(px); };
        check(pick(ie::blend::compositePixel(dstPx, sWhite, 1.0f, ie::BlendMode::ColorDodge)).r >= 250,
              "blend all: 白叠在灰上用颜色减淡 → 提亮到接近白");
        check(pick(ie::blend::compositePixel(dstPx, sBlack, 1.0f, ie::BlendMode::ColorBurn)).r <= 4,
              "blend all: 黑叠在灰上用颜色加深 → 压到接近黑");
        check(pick(ie::blend::compositePixel(dstPx, sWhite, 1.0f, ie::BlendMode::Difference)).r >= 126,
              "blend all: 白叠在灰上用差值 → 128 左右的反相");
        check(pick(ie::blend::compositePixel(Color(64, 64, 64, 255).packed(), Color(240, 240, 240, 255).packed(),
                                             1.0f, ie::BlendMode::SoftLight)).r > 100,
              "blend all: 近白叠在暗灰上用柔光 → 明显提亮");
        {
            const Color cc = pick(ie::blend::compositePixel(Color(0, 0, 255, 255).packed(),
                                                           Color(255, 0, 0, 255).packed(), 1.0f,
                                                           ie::BlendMode::Color));
            check(cc.r > cc.b, "blend all: 红盖蓝用颜色 → 取上层的色相饱和，红仍偏红");
        }
        check(pick(ie::blend::compositePixel(Color(200, 120, 60, 255).packed(), Color(60, 200, 120, 255).packed(),
                                             1.0f, ie::BlendMode::Modulate)).r < 60,
              "blend all: 调制 → 两层的值相乘，结果明显变暗");
    }

    {
        Image img(2, 1);
        img.pixels()[0] = 0;
        img.pixels()[1] = Color(10, 10, 10, 128).packed();
        const uint32_t src = Color(255, 0, 0, 255).packed();
        img.pixels()[0] = blend::paintPixel(img.pixels()[0], src, 1.0f, true);
        img.pixels()[1] = blend::paintPixel(img.pixels()[1], src, 1.0f, true);
        const Color a = Color::fromPacked(img.pixels()[0]);
        const Color b = Color::fromPacked(img.pixels()[1]);
        check(a.a == 0 && b.a == 128 && b.r > 10, "paint: 锁定透明像素时透明处不可写、半透明处保留 alpha");
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(0, 0, 0, 0));
        e.doc.layers[0].img.detach();
        e.doc.layers[0].img.pixels()[10 * 64 + 10] = Color(255, 0, 0, 255).packed();
        e.view.vpW = 800;
        e.view.vpH = 600;
        e.view.fit(64, 64);

        auto tools = createTools();
        Tool* mv = tools.front().get();
        check(mv->id() == ToolId::Move, "tool: 工具栏第一个是移动工具（PS 顺序）");
        check(tools.size() == 12, "tool: 12 个工具（自动裁切改成快捷键动作，不占栏）");
        Tool* ct = nullptr;
        for (auto& t : tools)
            if (t->id() == ToolId::Crop) ct = t.get();
        check(ct != nullptr, "tool: 裁剪工具在表里");
        if (ct) {
            e.sel.selectAll();
            ct->onEnter(e);
            int lw = 0, lh = 0;
            const bool placed = ct->liveSize(lw, lh);
            check(placed && lw == 64 && lh == 64, "tool: 有选区时进裁剪，框已按选区就位（不必再拖一次）");
        }

    {
        Editor ge;
        ge.doc.createEmpty(64, 64, Color(0, 0, 0, 0));
        std::unique_ptr<Tool> gt = makeGradientTool();
        Props gp;
        gt->props(gp, ge);
        const float* startCol = nullptr;
        const float* endCol = nullptr;
        for (const Prop& pr : gp) {
            if (pr.kind != PropKind::Color || !pr.label) continue;
            if (std::string(pr.label) == "起点色") startCol = pr.col;
            if (std::string(pr.label) == "终点色") endCol = pr.col;
        }
        check(startCol && endCol, "grad: 起/终点色都在工具属性里");
        check(startCol && startCol[0] == 0.0f && startCol[1] == 0.0f && startCol[2] == 0.0f &&
                  std::abs(startCol[3] - 1.0f) < 1e-6f,
              "grad: 初始起点色 = 黑（不透明）");
        check(endCol && endCol[0] == 0.0f && endCol[1] == 0.0f && endCol[2] == 0.0f && endCol[3] == 0.0f,
              "grad: 初始终点色 = 黑、透明（不是白）");
        const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        bool hasSetter = false;
        for (const Prop& pr : gp) {
            if (pr.kind == PropKind::Color && pr.label && std::string(pr.label) == "终点色" && pr.setColor) {
                pr.setColor(white, pr.setUd);
                hasSetter = true;
            }
        }
        check(hasSetter && std::abs(ge.palette.bg[0] - 1.0f) < 1e-6f && std::abs(ge.palette.bg[3] - 1.0f) < 1e-6f,
              "grad: 改渐变终点色不动全局背景色（单一持有者）");
    }

    {
        Editor se;
        se.doc.createEmpty(200, 160, Color(0, 0, 0, 0));
        auto st = makeSelectTool();
        Props sp;
        st->props(sp, se);
        int comboCount = 0;
        bool shapeDefaultRect = false;
        for (const Prop& pr : sp) {
            if (pr.kind != PropKind::Combo) continue;
            ++comboCount;
            if (pr.label && std::string(pr.label) == "形状" && pr.items && pr.itemCount == 2 && pr.sel &&
                *pr.sel == 0 && std::string(pr.items[0]) == "矩形" && std::string(pr.items[1]) == "椭圆")
                shapeDefaultRect = true;
        }
        check(comboCount >= 1 && shapeDefaultRect, "sel-shape: 选区工具给出 形状=矩形/椭圆，默认矩形");
    }

    {
        Editor se;
        se.doc.createEmpty(200, 160, Color(0, 0, 0, 0));
        se.sel.reset(se.doc.width, se.doc.height);
        se.view.vpX = 0; se.view.vpY = 0; se.view.vpW = (float)se.doc.width; se.view.vpH = (float)se.doc.height;
        se.view.zoom = 1.0f; se.view.panX = 0; se.view.panY = 0;
        auto st = makeSelectTool();
        InputFrame in;
        in.shift = true;
        in.mouse = Vec2f(20, 20);
        in.pressed[0] = true;
        st->onPress(se, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        in.mouse = Vec2f(120, 40);
        st->onDrag(se, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        st->onRelease(se, in, 0);
        const Rect b = se.sel.bounds();
        check(b.w == b.h && b.w >= 99 && b.w <= 101, "shift-ratio: 无选区时 Shift 拖出正方形（宽 = 高）");

        const Rect before = se.sel.bounds();
        in.shift = true;
        in.mouse = Vec2f(120, 120);
        in.pressed[0] = true;
        st->onPress(se, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        in.mouse = Vec2f(150, 140);
        st->onDrag(se, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        st->onRelease(se, in, 0);
        const Rect after = se.sel.bounds();
        check(after.w == before.w + 30 && after.h == before.h + 20,
              "shift-ratio: 已有选区时 Shift 回到加选，新框不被强行拉成正方形");
    }

    {
        Editor se;
        se.doc.createEmpty(120, 120, Color(0, 0, 0, 0));
        auto st = makeShapeTool();
        Props p1, p2;
        st->props(p1, se);
        const Prop* fillProp = nullptr;
        for (const Prop& pr : p1)
            if (pr.kind == PropKind::Bool && pr.label && std::string(pr.label) == "填充") fillProp = &pr;
        check(fillProp && fillProp->setV, "shape-fill: 填充有意图口");
        if (fillProp && fillProp->setV) fillProp->setV(1.0, fillProp->setUd);
        st->props(p2, se);
        int widthRows = 0;
        for (const Prop& pr : p2)
            if (pr.label && std::string(pr.label) == "线宽") ++widthRows;
        check(widthRows == 0, "shape-fill: 勾上填充后线宽行消失（工具真的收到了这个值）");
        bool fillOn = false;
        for (const Prop& pr : p2)
            if (pr.kind == PropKind::Bool && pr.label && std::string(pr.label) == "填充" && pr.b) fillOn = *pr.b;
        check(fillOn, "shape-fill: 第一下点击让填充变成 true");
    }
        for (const auto& t : tools)
            if (t->id() == ToolId::Lasso)
                check(ie::shortcuts::resolve(ie::KeyA, ie::keymap::None) == ie::KeyAct::ToolLasso,
                      "tool: 多边形套索快捷键 A（来自快捷键表）");

        const Vec2f a = e.view.toScreen(10.5f, 10.5f, 64, 64);
        const Vec2f b = e.view.toScreen(30.5f, 10.5f, 64, 64);
        InputFrame in;
        in.mouse = Vec2f(a.x, a.y);
        in.down[0] = in.pressed[0] = true;
        mv->onPress(e, in, 0);
        in.pressed[0] = false;
        in.mouse = Vec2f(b.x, b.y);
        mv->onDrag(e, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        mv->onRelease(e, in, 0);

        const bool moved = Color::fromPacked(layerPixelAt(e.doc.layers[0], 30, 10)).r == 255;
        e.undo();
        const bool undone = e.doc.layers[0].x == 0 && Color::fromPacked(layerPixelAt(e.doc.layers[0], 10, 10)).r == 255;
        check(moved && undone, "tool: 移动工具拖动生效且一次撤销即可还原");
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(0, 0, 0, 0));
        e.doc.layers[0].img.detach();
        e.doc.layers[0].img.pixels()[10 * 64 + 10] = Color(255, 0, 0, 255).packed();

        e.beginMove();
        e.updateMove(-30, 0, false);
        e.endMove();

        const bool goneFromCanvas = Color::fromPacked(layerPixelAt(e.doc.layers[0], 10, 10)).a == 0;
        const bool keptInLayer = (layerPixelAt(e.doc.layers[0], -20, 10) >> 24) == 255;
        const bool flatEmpty = [&] {
            const Image flat = e.doc.flatten();
            for (int i = 0; i < 64 * 64; ++i)
                if (flat.pixels()[i] >> 24) return false;
            return true;
        }();
        check(goneFromCanvas && keptInLayer && flatEmpty,
              "move: 移出画布的像素保留在图层里，导出不留残影（PS 语义）");
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(0, 0, 0, 0));
        e.doc.layers[0].img.detach();
        e.doc.layers[0].img.pixels()[10 * 64 + 10] = Color(255, 0, 0, 255).packed();

        e.beginMove();
        e.updateMove(-30, 0, false);
        e.endMove();
        e.beginMove();
        e.updateMove(30, 0, false);
        e.endMove();

        const Color back = Color::fromPacked(layerPixelAt(e.doc.layers[0], 10, 10));
        check(back.r == 255 && back.a == 255, "move: 移回画布的内容完整重现（像素从未丢失）");
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(0, 0, 0, 0));
        e.doc.layers[0].img.detach();
        e.doc.layers[0].img.pixels()[10 * 64 + 10] = Color(255, 0, 0, 255).packed();
        const int rev0 = e.doc.layers[0].pixRev;

        e.beginMove();
        e.updateMove(20, 0, false);
        const bool liveDuring = Color::fromPacked(layerPixelAt(e.doc.layers[0], 30, 10)).r == 255;
        const bool pixelsUntouched = Color::fromPacked(e.doc.layers[0].img.pixels()[10 * 64 + 10]).r == 255;
        const bool revStable = e.doc.layers[0].pixRev == rev0;
        e.endMove();

        const bool landed = e.doc.layers[0].x == 20 && Color::fromPacked(layerPixelAt(e.doc.layers[0], 30, 10)).r == 255;

        check(liveDuring && pixelsUntouched && revStable && landed,
              "move: 拖动中数据实时生效，像素零搬运（pixRev 不变 = 纹理零重传）");

        e.undo();
        const bool restored = e.doc.layers[0].x == 0 && Color::fromPacked(layerPixelAt(e.doc.layers[0], 10, 10)).r == 255;
        check(restored, "move: 偏移式移动一次撤销即可还原");
    }

    {
        Editor e;
        e.newDocument(48, 48, Color(0, 0, 0, 0));
        const int n = 48 * 48;
        e.doc.layers[0].img.detach();
        for (int i = 0; i < n; ++i) e.doc.layers[0].img.pixels()[i] = 0xff2040c0u;
        e.doc.addEmptyLayer("中");
        e.doc.addEmptyLayer("上");
        e.doc.layers[1].img.detach();
        e.doc.layers[2].img.detach();
        for (int y = 20; y < 44; ++y)
            for (int x = 20; x < 44; ++x) {
                e.doc.layers[1].img.pixels()[(size_t)y * 48 + x] = 0xff20c020u;
                e.doc.layers[2].img.pixels()[(size_t)y * 48 + x] = 0x8000b0ffu;
            }
        e.doc.markAllDirty();
        e.doc.recomposite(e.doc.takeDirty());

        e.sel.setRect(Rect{18, 18, 28, 28});
        e.doc.active = 1;
        e.beginMove();
        e.updateMove(9, 4, false);

        Image during(48, 48);
        e.doc.compositeInto(during.pixels(), 48, Rect{0, 0, 48, 48}, -1);
        e.endMove();
        Image after(48, 48);
        e.doc.compositeInto(after.pixels(), 48, Rect{0, 0, 48, 48}, -1);

        int diff = 0;
        for (int i = 0; i < n; ++i) {
            const uint32_t a = during.pixels()[i], b = after.pixels()[i];
            if (a != b) ++diff;
        }
        check(diff == 0, "move: 选区拖动中合成 == 落地后合成（逐像素，数据即权威）");
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(0, 0, 0, 0));
        auto mk = [](Rect r, Color c) {
            Image img(64, 64);
            img.fillRect(r, c.packed());
            return img;
        };
        e.doc.layers[0].img = mk(Rect{50, 50, 10, 10}, Color(255, 0, 0, 255));
        e.doc.addEmptyLayer("中");
        e.doc.layers[1].img = mk(Rect{10, 10, 10, 10}, Color(0, 0, 255, 255));
        e.doc.addEmptyLayer("上");
        e.doc.layers[2].img = mk(Rect{30, 30, 10, 10}, Color(0, 255, 0, 255));
        e.doc.markAllDirty();
        e.doc.active = 0;

        const Rect b1 = e.doc.layerContentBounds(1);
        const bool bboxOk = b1.x == 10 && b1.y == 10 && b1.w == 10 && b1.h == 10;
        e.doc.layers[1].img.detach();
        e.doc.layers[1].img.pixels()[0] = 0xffffffffu;
        e.doc.markDirty(Rect{0, 0, 1, 1});
        const Rect b2 = e.doc.layerContentBounds(1);
        check(bboxOk && b2.x == 0 && b2.y == 0 && b2.w == 20 && b2.h == 20,
              "bbox: 内容框按 alpha 精确计算，像素变化后立即失效");

        const bool hitMid = e.layerIndexAt(15, 15) == 1;
        const bool hitTop = e.layerIndexAt(35, 35) == 2;
        const bool hitBot = e.layerIndexAt(55, 55) == 0;
        const bool hitNone = e.layerIndexAt(25, 25) == -1;
        e.doc.layers[2].visible = false;
        const bool skipHidden = e.layerIndexAt(35, 35) == -1;
        e.doc.layers[2].visible = true;
        check(hitMid && hitTop && hitBot && hitNone && skipHidden,
              "pick: 像素命中从顶层往下找，透明穿透、隐藏层跳过");

        e.doc.active = 2;
        const bool noGrab = !e.beginMoveAt(25, 25, false) && !e.moving() && e.doc.active == 2;
        const bool grabbedLower = e.beginMoveAt(15, 15, false) && e.moving() && e.doc.active == 1;
        e.endMove();
        e.doc.active = 2;
        const bool keepSel = e.beginMoveAt(15, 15, true) && e.moving() && e.doc.active == 2;
        e.endMove();
        check(noGrab && grabbedLower && keepSel,
              "pick: 透明点击不抓层，有像素处抓该层，Ctrl 强制当前层");

        e.doc.active = 1;
        e.beginMove();
        e.updateMove(5, 7, false);
        const Rect mb = e.activeLayerBounds();
        const bool bboxFollows = mb.x == 5 && mb.y == 7 && mb.w == 20 && mb.h == 20;
        e.endMove();
        check(bboxFollows, "bbox: 移动中跟随位移（显示层与数据层同一坐标）");
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(0, 0, 0, 0));
        auto put = [&](int li, int x, int y, uint32_t c) {
            e.doc.layers[(size_t)li].img.detach();
            e.doc.layers[(size_t)li].img.pixels()[(size_t)y * 64 + x] = c;
        };
        put(0, 10, 10, 0xffffffffu);
        e.doc.addEmptyLayer("B");
        put(1, 30, 30, 0xff0000ffu);
        e.doc.markAllDirty();
        const int revA0 = e.doc.layers[0].pixRev, revB0 = e.doc.layers[1].pixRev;

        put(0, 20, 20, 0xff00ff00u);
        e.doc.markDirty(Rect{20, 20, 1, 1}, 0);
        const bool hintOnly = e.doc.layers[0].pixRev == revA0 + 1 && e.doc.layers[1].pixRev == revB0;
        const Rect ba = e.doc.layerContentBounds(0);
        const Rect cb = e.doc.layerCanvasRect(1);
        check(hintOnly && ba.x == 10 && ba.y == 10 && ba.w == 11 && ba.h == 11 && cb.x == 30 && cb.y == 30 &&
                  cb.w == 1 && cb.h == 1,
              "layer: 层提示只失效该层（pixRev），内容框/画布内区域按层精确");
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(0, 0, 0, 0));
        Image blk(64, 64);
        blk.fillRect(Rect{10, 10, 10, 10}, 0xff0000ffu);
        e.doc.addLayerAt(std::move(blk), 0, 0, "块");
        e.doc.markAllDirty();
        e.doc.takeDirty();
        const int li = e.doc.active;
        e.doc.layers[(size_t)li].opacity = 0.5f;
        e.beginMove();
        e.updateMove(70, 0, false);
        const Rect during = e.doc.layerContentBounds(li);
        e.endMove();
        const Rect after = e.doc.layerContentBounds(li);
        const bool kept = after.x == 80 && after.w == 10 && after.h == 10 && !after.empty();
        std::printf("    [diag] during=%d,%d %dx%d  after=%d,%d %dx%d\n", during.x, during.y, during.w, during.h,
                    after.x, after.y, after.w, after.h);
        (void)during;
        check(kept, "move: 拖动出画布内容保留在图层（边界跟到画布外，不裁剪）");
    }

    {
        Editor e2;
        e2.newDocument(200, 120, Color(255, 255, 255, 255));
        Image small(40, 30);
        small.fillAll(0xffff0000u);
        e2.doc.addLayerAt(std::move(small), 60, 40, "小块");
        e2.doc.markAllDirty();
        e2.doc.recomposite(e2.doc.takeDirty());

        e2.setLayerOpacity((int)e2.doc.layers.size() - 1, 0.5f);
        const Rect dz = e2.doc.takeDirty();
        const bool dirtyExact = dz.x == 60 && dz.y == 40 && dz.w == 40 && dz.h == 30;
        e2.doc.recomposite(dz);
        const uint32_t c = e2.doc.compositeImage().pixels()[(size_t)60 * 200 + 70];
        const int ca = (int)((c >> 24) & 0xff), cr = (int)(c & 0xff);
        const bool blendOk = ca == 255 && cr > 100 && cr < 160;
        check(dirtyExact && blendOk, "layer: 改不透明度只重算该层内容边界（不是整画布），且混合正确");
    }

    {
        Selection sel;
        sel.reset(40, 40);
        std::vector<Vec2f> diamond = {{5, 20}, {20, 5}, {35, 20}, {20, 35}};
        sel.selectPolygon(diamond);
        check(sel.active && sel.bounds().w > 0 && sel.bounds().h > 0, "lasso: 多边形选区建立");
        check(sel.contains(20, 20) && sel.contains(20, 8) && sel.contains(10, 20),
              "lasso: 圈内像素选中");
        check(!sel.contains(2, 2) && !sel.contains(38, 38) && !sel.contains(20, 2),
              "lasso: 圈外像素不选中");

        sel.addPolygon({{32, 32}, {37, 32}, {37, 37}, {32, 37}});
        check(sel.contains(34, 34) && sel.contains(20, 20), "lasso: 加选合并");
        sel.subtractPolygon({{30, 30}, {38, 30}, {38, 38}, {30, 38}});
        check(!sel.contains(34, 34) && sel.contains(20, 20), "lasso: 减选生效");

        sel.selectPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        check(sel.contains(5, 5) && !sel.contains(20, 20), "lasso: 替换模式清掉旧选区");
    }

    {
        Editor ed;
        ed.newDocument(60, 60, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.panX = ed.view.panY = 0.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        auto tool = makeLassoTool();

        InputFrame in;
        auto click = [&](float x, float y) {
            in.mouse = Vec2f(x, y);
            in.pressed[0] = true;
            tool->onPress(ed, in, 0);
            in.pressed[0] = false;
            in.released[0] = true;
            tool->onRelease(ed, in, 0);
            in.released[0] = false;
        };
        click(10, 10);
        click(40, 10);
        click(40, 40);
        in.key = ie::KeyDelete;
        tool->onKey(ed, in);
        click(10, 40);
        in.doubleClick = true;
        in.mouse = Vec2f(10, 10);
        in.pressed[0] = true;
        tool->onPress(ed, in, 0);
        in.pressed[0] = false;
        in.released[0] = true;
        tool->onRelease(ed, in, 0);

        check(ed.sel.contains(20, 20) && !ed.sel.contains(35, 35),
              "lasso: 多边形形态点击锁点 + Delete 退点 + 双击闭合");
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(0, 0, 0, 0));
        auto mk = [](Rect r, Color c) {
            Image img(64, 64);
            img.fillRect(r, c.packed());
            return img;
        };
        e.doc.layers[0].img = mk(Rect{10, 10, 4, 4}, Color(255, 0, 0, 255));
        e.doc.addEmptyLayer("中");
        e.doc.layers[1].img = mk(Rect{20, 10, 4, 4}, Color(0, 255, 0, 255));
        e.doc.addEmptyLayer("上");
        e.doc.layers[2].img = mk(Rect{40, 40, 4, 4}, Color(0, 0, 255, 255));
        e.doc.markAllDirty();
        const int idBottom = e.doc.layers[0].id;
        const int idMid = e.doc.layers[1].id;
        const int idTop = e.doc.layers[2].id;

        e.setActiveLayer(0);
        e.toggleLayerSelection(1);
        check(e.doc.layerSelected(0) && e.doc.layerSelected(1) && !e.doc.layerSelected(2),
              "layers: Ctrl 点击多选（底+中），未选上层");

        e.moveSelectedLayers(2);
        check(e.doc.layers[0].id == idTop && e.doc.layers[1].id == idBottom && e.doc.layers[2].id == idMid,
              "layers: 多选整组拖到目标行的位置（组保持相对次序）");

        e.mergeSelectedLayers();
        check(e.doc.layers.size() == 2 && e.doc.active == 1 && e.doc.layers[0].id == idTop,
              "layers: 合并选中图层后只剩合并层 + 未选层");
        const Layer& merged = e.doc.layers[1];
        check(layerPixelAt(merged, 10, 10) == Color(255, 0, 0, 255).packed() &&
                  layerPixelAt(merged, 20, 10) == Color(0, 255, 0, 255).packed(),
              "layers: 合并层保留两层像素");
        check(layerPixelAt(e.doc.layers[0], 40, 40) == Color(0, 0, 255, 255).packed(),
              "layers: 未选图层像素不受合并影响");
    }

    if (!std::string(textFontPath()).empty()) {
        {
            Editor f;
            f.newDocument(220, 160, Color(255, 255, 255, 255));
            f.doc.activeLayer()->img.detach();
            f.doc.activeLayer()->img.fillRect(Rect{16, 20, 70, 70}, Color(200, 60, 60, 255).packed());
            LayerChild ac;
            ac.kind = ChildKind::Tri;
            ac.id = 71;
            ac.on = true;
            ac.r = 1.5f;
            f.doc.layers[0].children.push_back(ac);
            TextData ft;
            ft.text = "特效";
            ft.size = 44;
            ft.x = 100.0f;
            ft.y = 70.0f;
            ft.color = Color(40, 40, 40, 255);
            const int fi = f.addTextLayer(ft);
            LayerStyle& ls = f.doc.layers[(size_t)fi].style;
            ls.strokeOn = true;
            ls.strokeWidth = 4.0f;
            ls.strokeColor[0] = 1.0f;
            ls.strokeColor[3] = 1.0f;
            ls.shadowOn = true;
            ls.shadowDist = 5.0f;
            ls.shadowSize = 6.0f;
            ls.shadowColor[2] = 1.0f;
            ls.shadowColor[3] = 1.0f;
            f.doc.refreshTextLayers();
            const Image before = f.doc.flatten();
            f.mergeLayerDown(fi);
            check(f.doc.layers.size() == 1, "merge-order: 两层合成一层");
            const Layer& baked = f.doc.layers[0];
            check(baked.kind == LayerKind::Pixel && baked.children.empty() &&
                      !styleActive(baked.style) && baked.text.text.empty(),
                  "merge-order: 合成层是纯像素，三色/描边/投影都已各自成图后再合并");
            const Image after = f.doc.flatten();
            long diff = 0;
            for (int y = 0; y < before.height(); ++y)
                for (int x = 0; x < before.width(); ++x) {
                    const uint32_t a = before.pixels()[(size_t)y * before.width() + (size_t)x];
                    const uint32_t b = after.pixels()[(size_t)y * after.width() + (size_t)x];
                    const int da = std::abs((int)(a >> 24) - (int)(b >> 24));
                    const int dr = std::abs((int)(a & 255u) - (int)(b & 255u));
                    if (da > 2 || dr > 2) ++diff;
                }
            char mfxb[220];
            std::snprintf(mfxb, sizeof mfxb, "merge-order: 先各自成图再合并，画面不变 差异=%ld/%d", diff,
                          before.width() * before.height());
            check(diff <= (long)(before.width() * before.height() / 100), mfxb);
        }

        {
            Editor m;
            m.newDocument(200, 150, Color(0, 0, 0, 0));
            m.doc.activeLayer()->img.detach();
            m.doc.activeLayer()->img.fillRect(Rect{10, 20, 60, 60}, Color(255, 0, 0, 255).packed());
            TextData mt;
            mt.text = "并";
            mt.size = 40;
            mt.x = 90.0f;
            mt.y = 80.0f;
            m.addTextLayer(mt);
            {
                LayerStyle& ms = m.doc.layers.back().style;
                ms.strokeOn = true;
                ms.strokeWidth = 5.0f;
                ms.strokeColor[0] = 1.0f;
                ms.strokeColor[3] = 1.0f;
                ms.shadowOn = true;
                ms.shadowDist = 4.0f;
                ms.shadowSize = 5.0f;
            }
            m.doc.refreshTextLayers();
            const int topIdx = (int)m.doc.layers.size() - 1;
            m.mergeLayerDown(topIdx);
            check(m.doc.layers.size() == 1, "merge-fix: 图像层与文字层合成一层");
            const Layer& ml = m.doc.layers[0];
            check(ml.kind == LayerKind::Pixel, "merge-fix: 合成层是像素层（不再挂着文字身份）");
            check(ml.text.text.empty() && ml.textXf.isIdentity(),
                  "merge-fix: 合成层没有文字数据与变形矩阵（缩放不会再每帧重排字形）");
            const bool entered = m.beginFreeTransform(0);
            check(entered, "merge-fix: 合成层能进入变形");
            m.transformBeginGrab(3, Vec2f(60.0f, 60.0f));
            m.transformUpdateGrab(Vec2f(90.0f, 90.0f), false);
            m.transformEndGrab();
            check(m.doc.layers[0].textXf.isIdentity() && m.doc.layers[0].kind == LayerKind::Pixel,
                  "merge-fix: 缩放走的是像素路径（字形矩阵仍是单位阵）");
            check(!m.doc.layers[0].img.empty(), "merge-fix: 缩放后图层仍有像素");
            check(!ie::styleActive(m.doc.layers[0].style) && m.doc.layers[0].styleKey == ie::StyleCompositeKey{},
                  "merge-fix: 合并层不留样式参数（缩放时不再每帧重算描边/投影掩膜）");
            if (m.transforming()) m.cancelFreeTransform();
        }
    }

    {
        Editor e;
        e.newDocument(40, 40, Color(255, 0, 0, 255));
        e.doc.activeLayer()->img.detach();
        e.doc.activeLayer()->img.fillRect(Rect{2, 2, 36, 36}, Color(0, 0, 255, 255).packed());
        e.doc.markAllDirty();
        e.trimCanvas();
        check(e.doc.width == 40 && e.doc.height == 40, "trim: 纯色边不裁（红边也是像素）");

        Editor e3;
        e3.newDocument(40, 40, Color(255, 255, 255, 255));
        e3.doc.activeLayer()->img.detach();
        e3.doc.activeLayer()->img.fillRect(Rect{20, 0, 20, 40}, Color(255, 0, 0, 255).packed());
        e3.doc.markAllDirty();
        e3.trimCanvas();
        check(e3.doc.width == 40 && e3.doc.height == 40, "trim: 左白右红的图不会被裁掉白色半边");

        Editor e2;
        e2.newDocument(40, 40, Color(0, 0, 0, 0));
        e2.doc.activeLayer()->img.detach();
        e2.doc.activeLayer()->img.fillRect(Rect{4, 4, 32, 32}, Color(0, 255, 0, 255).packed());
        e2.doc.markAllDirty();
        e2.trimCanvas();
        check(e2.doc.width == 32 && e2.doc.height == 32, "trim: 透明边自动裁切");
    }

    {
        Editor ed;
        ed.newDocument(100, 100, Color(255, 255, 255, 255));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.panX = ed.view.panY = 0.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        auto tool = makeCropTool();

        InputFrame in;
        in.mouse = Vec2f(20, 20);
        in.pressed[0] = true;
        tool->onPress(ed, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        in.mouse = Vec2f(40, 40);
        tool->onDrag(ed, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        tool->onRelease(ed, in, 0);

        in.mouse = Vec2f(40, 40);
        in.pressed[0] = true;
        tool->onPress(ed, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        in.mouse = Vec2f(60, 60);
        tool->onDrag(ed, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        tool->onRelease(ed, in, 0);

        in.key = ie::KeyEnter;
        tool->onKey(ed, in);
        check(ed.doc.width == 40 && ed.doc.height == 40, "crop: 手柄调整后应用生效（PS 行为）");
    }

    {
        Editor ce;
        ce.doc.createEmpty(400, 300, Color(0, 0, 0, 0));
        ce.view.vpX = 0; ce.view.vpY = 0; ce.view.vpW = (float)ce.doc.width; ce.view.vpH = (float)ce.doc.height;
        ce.view.zoom = 1.0f; ce.view.panX = 0; ce.view.panY = 0;
        auto ct = makeCropTool();
        InputFrame in;
        in.mouse = Vec2f(20, 20);
        in.pressed[0] = true;
        ct->onPress(ce, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        in.mouse = Vec2f(120, 70);
        ct->onDrag(ce, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        ct->onRelease(ce, in, 0);
        in.released[0] = false;
        in.pressed[0] = true;
        in.mouse = Vec2f(120, 70);
        ct->onPress(ce, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        in.shift = true;
        in.mouse = Vec2f(200, 200);
        ct->onDrag(ce, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        ct->onRelease(ce, in, 0);
        in.key = ie::KeyEnter;
        ct->onKey(ce, in);
        check(ce.doc.width == 360 && ce.doc.height == 180,
              "crop-shift: 拖角手柄时 Shift 保持原框 2:1（100×50 → 360×180，左上角固定）");
    }

    {
        Editor ce;
        ce.doc.createEmpty(200, 160, Color(0, 0, 0, 0));
        ce.view.vpX = 0; ce.view.vpY = 0; ce.view.vpW = (float)ce.doc.width; ce.view.vpH = (float)ce.doc.height;
        ce.view.zoom = 1.0f; ce.view.panX = 0; ce.view.panY = 0;
        auto ct = makeCropTool();
        InputFrame in;
        in.shift = true;
        in.mouse = Vec2f(10, 10);
        in.pressed[0] = true;
        ct->onPress(ce, in, 0);
        in.pressed[0] = false;
        in.down[0] = true;
        in.mouse = Vec2f(100, 40);
        ct->onDrag(ce, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        ct->onRelease(ce, in, 0);
        in.key = ie::KeyEnter;
        ct->onKey(ce, in);
        check(ce.doc.width == ce.doc.height && ce.doc.width >= 89 && ce.doc.width <= 91,
              "crop-shift: Shift 拖出的裁剪框是正方形（应用后画布 90×90）");
    }


    {
        Editor pe;
        pe.doc.createEmpty(40, 40, Color(0, 0, 0, 0));
        Image red(40, 40);
        uint32_t* rp = red.pixels();
        for (int i = 0; i < 40 * 40; ++i) rp[(size_t)i] = 0xff0000ffu;
        pe.doc.addLayerAt(red, 0, 0, "红");
        Color got;
        check(pe.colorAtPixel(5, 5, got) && got.r == 255 && got.g == 0 && got.b == 0 && got.a == 255,
              "pick: 吸管读合成后的画布色（红层不透明红）");
        check(!pe.colorAtPixel(-1, 5, got) && !pe.colorAtPixel(40, 5, got),
              "pick: 画布外不取色（不越界读）");
    }

    {
        Image flat(50, 50);
        uint32_t* fp = flat.pixels();
        for (int y = 0; y < 50; ++y) {
            for (int x = 0; x < 50; ++x) {
                uint32_t v;
                if (x < 20)
                    v = 0xff0000ffu;
                else if (x < 24)
                    v = 0xff00ff00u;
                else
                    v = 0xffff0000u;
                fp[(size_t)y * 50 + (size_t)x] = v;
            }
        }
        for (int y = 40; y < 45; ++y)
            for (int x = 40; x < 45; ++x) fp[(size_t)y * 50 + (size_t)x] = 0xff0000ffu;
        for (int x = 0; x < 50; ++x) fp[(size_t)49 * 50 + (size_t)x] = 0;

        Selection sel;
        sel.reset(50, 50);
        sel.selectFlood(fp, 5, 5, 0, false);
        check(sel.contains(5, 10) && sel.contains(19, 25) && sel.contains(19, 48),
              "wand: 连通域选择覆盖整块红区");
        check(!sel.contains(20, 5) && !sel.contains(42, 42) && !sel.contains(49, 49),
              "wand: 不跨界、不跨连通、透明不误选");

        sel.selectFlood(fp, 42, 42, 0, true);
        check(sel.contains(42, 42) && sel.contains(5, 5), "wand: 加选合并");
        sel.subtractFlood(fp, 5, 5, 0);
        check(!sel.contains(5, 5) && sel.contains(42, 42), "wand: 减选保留其他区域");

        sel.clear();
        sel.selectFlood(fp, 21, 5, 0, false);
        check(sel.contains(21, 5) && sel.contains(23, 40) && !sel.contains(5, 5),
              "wand: 不同颜色互不误选");
        sel.selectFlood(fp, 21, 5, 32, true);
        check(sel.bounds().w <= 4, "wand: 容差不足不扩散到异色区域");
    }

    {
        const std::string fpG = textFontPath();
        if (!fpG.empty()) {
            Editor e;
            e.newDocument(400, 300, Color(255, 255, 255, 255));
            TextData t;
            t.text = "文字";
            t.x = 60.0f;
            t.y = 80.0f;
            t.size = 36;
            t.color = Color(0, 0, 0, 255);
            e.doc.addTextLayer(t, "文字");
            int idx = -1;
            for (int i = 0; i < (int)e.doc.layers.size(); ++i)
                if (e.doc.layers[(size_t)i].kind == LayerKind::Text) idx = i;
            e.doc.refreshTextLayers();
            const Rect in0 = e.doc.layerContentBounds(idx);
            const Vec2f c0{(float)in0.x + (float)in0.w * 0.5f, (float)in0.y + (float)in0.h * 0.5f};
            const float rad = (float)in0.h * 0.5f + 22.0f;
            e.beginFreeTransform(idx);
            e.transformBeginGrab(9, Vec2f{c0.x, c0.y - rad});
            e.transformUpdateGrab(Vec2f{c0.x + rad, c0.y}, false);
            const float rot = e.transformRotation();
            const Vec2f cPre = e.transformCenter();
            const Rect preB = e.doc.layerVisualBounds(idx);
            e.commitFreeTransform();
            const Rect in1 = e.doc.layerContentBounds(idx);
            const Vec2f c1{(float)in1.x + (float)in1.w * 0.5f, (float)in1.y + (float)in1.h * 0.5f};
            const float dcx = std::fabs(c1.x - cPre.x);
            const float dcy = std::fabs(c1.y - cPre.y);
            const Rect vb1 = e.doc.layerVisualBounds(idx);
            const float dPre = std::fabs((float)vb1.x - (float)preB.x) + std::fabs((float)vb1.y - (float)preB.y);
            char lbl[300];
            std::snprintf(lbl, sizeof lbl,
                          "text-gesture: 回车后字落在框上 rot=%.3f 中心差=(%.2f,%.2f) 预览框差=%.2f 提交后墨迹=%d,%d,%d,%d",
                          rot, dcx, dcy, dPre, in1.x, in1.y, in1.w, in1.h);
            check(std::fabs(rot - 1.5708f) < 0.05f && dcx <= 2.0f && dcy <= 2.0f && dPre <= 2.0f, lbl);
        }
    }

    {
        const std::string fpS = textFontPath();
        if (!fpS.empty()) {
            Editor e;
            e.newDocument(400, 300, Color(255, 255, 255, 255));
            TextData t;
            t.text = "文字";
            t.x = 60.0f;
            t.y = 80.0f;
            t.size = 36;
            t.color = Color(0, 0, 0, 255);
            e.doc.addTextLayer(t, "文字");
            int idx = -1;
            for (int i = 0; i < (int)e.doc.layers.size(); ++i)
                if (e.doc.layers[(size_t)i].kind == LayerKind::Text) idx = i;
            e.doc.refreshTextLayers();
            const Rect in0 = e.doc.layerContentBounds(idx);
            const Vec2f c0{(float)in0.x + (float)in0.w * 0.5f, (float)in0.y + (float)in0.h * 0.5f};
            const Vec2f corner{(float)in0.right(), (float)in0.bottom()};
            e.beginFreeTransform(idx);
            e.transformBeginGrab(3, corner);
            e.transformUpdateGrab(Vec2f{c0.x + (corner.x - c0.x) * 1.5f, c0.y + (corner.y - c0.y) * 1.5f}, false);
            const Vec2f cPre = e.transformCenter();
            e.commitFreeTransform();
            const Rect in1 = e.doc.layerContentBounds(idx);
            const Vec2f c1{(float)in1.x + (float)in1.w * 0.5f, (float)in1.y + (float)in1.h * 0.5f};
            const float dcx2 = std::fabs(c1.x - cPre.x);
            const float dcy2 = std::fabs(c1.y - cPre.y);
            const float dw = std::fabs((float)in1.w - (float)in0.w * 1.5f);
            const float dh = std::fabs((float)in1.h - (float)in0.h * 1.5f);
            char ls[280];
            std::snprintf(ls, sizeof ls,
                          "text-stretch: 拉大 1.5 倍后字仍在框内 中心差=(%.2f,%.2f) 尺寸差=(%.2f,%.2f) 墨迹=%d,%d,%d,%d",
                          dcx2, dcy2, dw, dh, in1.x, in1.y, in1.w, in1.h);
            check(dcx2 <= 2.0f && dcy2 <= 2.0f && dw <= 2.5f && dh <= 2.5f, ls);
        }
    }

    {
        const std::string fpZ = textFontPath();
        if (!fpZ.empty()) {
            Editor e;
            e.newDocument(400, 300, Color(255, 255, 255, 255));
            TextData t;
            t.text = "文字";
            t.x = 60.0f;
            t.y = 80.0f;
            t.size = 36;
            t.color = Color(0, 0, 0, 255);
            e.doc.addTextLayer(t, "文字");
            int idx = -1;
            for (int i = 0; i < (int)e.doc.layers.size(); ++i)
                if (e.doc.layers[(size_t)i].kind == LayerKind::Text) idx = i;
            e.doc.refreshTextLayers();
            const float zooms[5] = {1.0f, 1.2f, 1.44f, 1.73f, 2.0f};
            char lz[300];
            float worstX = 0.0f, worstY = 0.0f;
            for (int zi = 0; zi < 5; ++zi) {
                e.view.zoom = zooms[zi];
                e.view.vpX = 0;
                e.view.vpY = 0;
                e.view.vpW = 900;
                e.view.vpH = 700;
                e.view.panX = 0;
                e.view.panY = 0;
                const SkMatrix ctm = e.view.matrix(e.doc.width, e.doc.height);
                const bool entered = e.beginFreeTransform(idx);
                const SkRect fdoc = e.transformFrameRect();
                SkRect expect;
                ctm.mapRect(&expect, fdoc);
                const Image fl = e.doc.flattenRegionScaled(1.0f, Rect{0, 0, e.doc.width, e.doc.height}, false);
                const int W = 1000, H = 800;
                SkImageInfo sinfo = SkImageInfo::Make(W, H, kN32_SkColorType, kPremul_SkAlphaType);
                sk_sp<SkSurface> surf = SkSurfaces::Raster(sinfo);
                if (!surf) continue;
                SkCanvas* cvp = surf->getCanvas();
                cvp->clear(SK_ColorWHITE);
                cvp->concat(ctm);
                SkImageInfo fi = SkImageInfo::Make(fl.width(), fl.height(), kN32_SkColorType,
                                                   kPremul_SkAlphaType);
                sk_sp<SkImage> im = SkImages::RasterFromData(
                    fi, SkData::MakeWithoutCopy(fl.data(), (size_t)fl.width() * (size_t)fl.height() * 4),
                    (size_t)fl.width() * 4);
                if (im) {
                    SkRect src = SkRect::MakeWH((float)fl.width(), (float)fl.height());
                    SkRect dst = SkRect::MakeWH((float)fl.width(), (float)fl.height());
                    const SkFilterMode fmm =
                        (zooms[zi] >= 1.0f && e.view.axisAligned()) ? SkFilterMode::kNearest : SkFilterMode::kLinear;
                    cvp->drawImageRect(im.get(), src, dst, SkSamplingOptions(fmm), nullptr,
                                     SkCanvas::kFast_SrcRectConstraint);
                }
                e.doc.drawTextLayers(*cvp, ctm, e.doc.width, e.doc.height);
                int ix0 = W, iy0 = H, ix1 = -1, iy1 = -1;
                SkPixmap pm;
                if (!surf->peekPixels(&pm)) continue;
                const uint32_t* px = (const uint32_t*)pm.addr();
                const int rb = (int)(pm.rowBytes() / 4);
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < W; ++x) {
                        const uint32_t v = px[(size_t)y * (size_t)rb + (size_t)x];
                        if ((v & 0x00ffffffu) != 0x00ffffffu) {
                            if (x < ix0) ix0 = x;
                            if (x > ix1) ix1 = x;
                            if (y < iy0) iy0 = y;
                            if (y > iy1) iy1 = y;
                        }
                    }
                if (ix1 >= ix0 && iy1 >= iy0) {
                    const float dx = std::fabs((float)ix0 - expect.left());
                    const float dy = std::fabs((float)iy0 - expect.top());
                    if (dx > worstX) worstX = dx;
                    if (dy > worstY) worstY = dy;
                }
                std::snprintf(lz, sizeof lz,
                              "zoom-hit: 屏幕墨迹 vs 框 zoom=%.2f 框屏=(%.1f,%.1f) 墨屏=(%d,%d,%d,%d) 偏差=(%.1f,%.1f)",
                              zooms[zi], expect.left(), expect.top(), ix0, iy0, ix1 - ix0 + 1, iy1 - iy0 + 1,
                              std::fabs((float)ix0 - expect.left()), std::fabs((float)iy0 - expect.top()));
                check(entered && std::fabs((float)ix0 - expect.left()) <= 1.5f &&
                          std::fabs((float)iy0 - expect.top()) <= 1.5f,
                      lz);
                if (entered) e.cancelFreeTransform();
            }
        }
    }

    {
        const std::string fpH = textFontPath();
        if (!fpH.empty()) {
            Editor e;
            e.newDocument(400, 300, Color(255, 255, 255, 255));
            TextData t;
            t.text = "文字";
            t.x = 60.0f;
            t.y = 80.0f;
            t.size = 36;
            t.color = Color(0, 0, 0, 255);
            e.doc.addTextLayer(t, "文字");
            int idx = -1;
            for (int i = 0; i < (int)e.doc.layers.size(); ++i)
                if (e.doc.layers[(size_t)i].kind == LayerKind::Text) idx = i;
            e.doc.refreshTextLayers();
            const Rect in0 = e.doc.layerContentBounds(idx);
            const bool hitBefore = e.layerIndexAt(in0.x + in0.w / 2, in0.y + in0.h / 2) == idx;
            e.beginFreeTransform(idx);
            const Vec2f c0{(float)in0.x + (float)in0.w * 0.5f, (float)in0.y + (float)in0.h * 0.5f};
            const float rad = (float)in0.h * 0.5f + 22.0f;
            e.transformBeginGrab(9, Vec2f{c0.x, c0.y - rad});
            e.transformUpdateGrab(Vec2f{c0.x + rad, c0.y}, false);
            e.commitFreeTransform();
            const Rect in1 = e.doc.layerContentBounds(idx);
            const bool hitAfter = e.layerIndexAt(in1.x + in1.w / 2, in1.y + 3) == idx;
            const bool missOld = e.layerIndexAt(in1.x - 24, in1.y - 24) != idx;
            char lh[240];
            std::snprintf(lh, sizeof lh,
                          "text-hit: 变形后点中画出的字 rot=%.3f 前=%d 后=%d 远处不误中=%d 新墨迹=%d,%d,%d,%d",
                          e.transformRotation(), (int)hitBefore, (int)hitAfter, (int)missOld, in1.x, in1.y, in1.w,
                          in1.h);
            check(hitBefore && hitAfter && missOld, lh);
        }
    }

    {
        const std::string fpM = textFontPath();
        if (!fpM.empty()) {
            Editor e;
            e.newDocument(400, 300, Color(255, 255, 255, 255));
            TextData t;
            t.text = "文字";
            t.x = 60.0f;
            t.y = 80.0f;
            t.size = 36;
            t.color = Color(0, 0, 0, 255);
            e.doc.addTextLayer(t, "文字");
            int idx = -1;
            for (int i = 0; i < (int)e.doc.layers.size(); ++i)
                if (e.doc.layers[(size_t)i].kind == LayerKind::Text) idx = i;
            e.doc.refreshTextLayers();
            const Rect in0 = e.doc.layerContentBounds(idx);
            const Vec2f c0{(float)in0.x + (float)in0.w * 0.5f, (float)in0.y + (float)in0.h * 0.5f};
            const float rad = (float)in0.h * 0.5f + 22.0f;
            e.beginFreeTransform(idx);
            e.transformBeginGrab(9, Vec2f{c0.x, c0.y - rad});
            e.transformUpdateGrab(Vec2f{c0.x + rad, c0.y}, false);
            e.commitFreeTransform();
            const Rect b0 = e.doc.layerContentBounds(idx);
            e.doc.active = idx;
            e.beginMove();
            e.updateMove(0, -10, false);
            e.endMove();
            const Rect b1 = e.doc.layerContentBounds(idx);
            const float mvx = (float)(b1.x - b0.x);
            const float mvy = (float)(b1.y - b0.y);
            char lm[220];
            std::snprintf(lm, sizeof lm, "text-move: 旋转后向上拖 10px，字应只向上 实际=(%.1f,%.1f)", mvx, mvy);
            check(std::fabs(mvx) <= 1.0f && std::fabs(mvy + 10.0f) <= 2.0f, lm);
        }
    }

    {
        const std::string fpE = textFontPath();
        if (!fpE.empty()) {
            Editor e;
            e.newDocument(400, 300, Color(255, 255, 255, 255));
            TextData t;
            t.text = "导出";
            t.x = 80.0f;
            t.y = 100.0f;
            t.size = 40;
            t.color = Color(0, 0, 0, 255);
            e.doc.addTextLayer(t, "文字");
            int idx = -1;
            for (int i = 0; i < (int)e.doc.layers.size(); ++i)
                if (e.doc.layers[(size_t)i].kind == LayerKind::Text) idx = i;
            e.doc.refreshTextLayers();
            const Rect in0 = e.doc.layerContentBounds(idx);
            const Vec2f c0{(float)in0.x + (float)in0.w * 0.5f, (float)in0.y + (float)in0.h * 0.5f};
            const float rad = (float)in0.h * 0.5f + 24.0f;
            e.beginFreeTransform(idx);
            e.transformBeginGrab(9, Vec2f{c0.x, c0.y - rad});
            e.transformUpdateGrab(Vec2f{c0.x + rad, c0.y}, false);
            e.commitFreeTransform();
            const SkRect fd = e.doc.layerDrawBounds(idx);
            const Image f2 = e.doc.flattenScaled(2.0f);
            Rect ink;
            if (!f2.empty()) {
                int x0 = f2.width(), y0 = f2.height(), x1 = -1, y1 = -1;
                for (int y = 0; y < f2.height(); ++y) {
                    const uint32_t* row = f2.pixels() + (size_t)y * (size_t)f2.width();
                    for (int x = 0; x < f2.width(); ++x) {
                        const Color c = Color::fromPacked(row[x]);
                        if (c.r == 255 && c.g == 255 && c.b == 255) continue;
                        x0 = std::min(x0, x);
                        x1 = std::max(x1, x);
                        y0 = std::min(y0, y);
                        y1 = std::max(y1, y);
                    }
                }
                if (x1 >= x0) ink = Rect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
            }
            const float ex = std::fabs((float)ink.x - fd.left() * 2.0f);
            const float ey = std::fabs((float)ink.y - fd.top() * 2.0f);
            const float ew = std::fabs((float)ink.w - fd.width() * 2.0f);
            const float eh = std::fabs((float)ink.h - fd.height() * 2.0f);
            char le[240];
            std::snprintf(le, sizeof le,
                          "text-export: 旋转的字在 2× 导出里仍是转过的 墨迹=%d,%d,%d,%d 框2×=%.1f,%.1f,%.1f,%.1f 差=(%.1f,%.1f,%.1f,%.1f)",
                          ink.x, ink.y, ink.w, ink.h, fd.left() * 2.0f, fd.top() * 2.0f, fd.width() * 2.0f,
                          fd.height() * 2.0f, ex, ey, ew, eh);
            check(ex <= 2.0f && ey <= 2.0f && ew <= 3.0f && eh <= 3.0f, le);

            const Image f1 = e.doc.flatten();
            int dark = 0, soft = 0;
            for (int y = 0; y < f1.height(); ++y) {
                const uint32_t* row = f1.pixels() + (size_t)y * (size_t)f1.width();
                for (int x = 0; x < f1.width(); ++x) {
                    const Color c = Color::fromPacked(row[x]);
                    if (c.r < 245) ++dark;
                    if (c.r > 25 && c.r < 235) ++soft;
                }
            }
            const float softRatio = dark > 0 ? (float)soft / (float)dark : 99.0f;
            char lq[200];
            std::snprintf(lq, sizeof lq, "text-export: 1× 导出的字边是硬的 墨=%d 半调=%d 比=%.2f", dark, soft, softRatio);
            check(softRatio < 0.75f, lq);
        }
    }

    {
        const std::string fpF = textFontPath();
        if (!fpF.empty()) {
            Editor e;
            e.newDocument(300, 200, Color(255, 255, 255, 255));
            TextData t;
            t.text = "文字";
            t.x = 50.0f;
            t.y = 60.0f;
            t.size = 32;
            t.color = Color(0, 0, 0, 255);
            e.doc.addTextLayer(t, "文字");
            int idx = -1;
            for (int i = 0; i < (int)e.doc.layers.size(); ++i)
                if (e.doc.layers[(size_t)i].kind == LayerKind::Text) idx = i;
            e.doc.refreshTextLayer(idx);
            const bool entered = e.beginFreeTransform(idx);
            const SkRect fr = e.transformFrameRect();
            const Layer& lf = e.doc.layers[(size_t)idx];
            SkPath gp;
            bool ok = false;
            float fx = -1.0f, fy = -1.0f, px2 = -1.0f, py2 = -1.0f;
            if (entered && textGlyphPath(lf.text, gp) && !gp.isEmpty()) {
                const SkRect b = gp.getBounds();
                fx = std::fabs(fr.left() - b.left());
                fy = std::fabs(fr.top() - b.top());
                const Rect ib = lf.img.contentBounds();
                px2 = std::fabs(fr.left() - (float)(lf.x + ib.x));
                py2 = std::fabs(fr.top() - (float)(lf.y + ib.y));
                ok = fx <= 0.05f && fy <= 0.05f && std::fabs(fr.width() - b.width()) <= 0.05f &&
                     std::fabs(fr.height() - b.height()) <= 0.05f && px2 <= 1.0f && py2 <= 1.0f;
            }
            char lbl[260];
            std::snprintf(lbl, sizeof lbl,
                          "text-frame: 框取自这一层的几何（文字取字形、像素取像素） 框=(%.2f,%.2f,%.2f,%.2f) 对字形=(%.2f,%.2f) 对墨迹=(%.2f,%.2f) ok=%d",
                          fr.left(), fr.top(), fr.width(), fr.height(), fx, fy, px2, py2, (int)ok);
            check(entered && ok, lbl);
            e.cancelFreeTransform();
        }
    }

    {
        const Rect r{10, 20, 30, 16};
        bool ok = true;
        float worst = 0.0f;
        const float rots[3] = {0.0f, 0.35f, 1.5707963f};
        const float scls[2] = {1.0f, 2.0f};
        for (int ri = 0; ri < 3; ++ri)
            for (int si = 0; si < 2; ++si) {
                XForm xf{(float)r.x + (float)r.w * 0.5f + 5.0f, (float)r.y + (float)r.h * 0.5f - 3.0f, scls[si],
                         scls[si] * 0.5f, rots[ri]};
                Vec2f q[4];
                xformQuad(r, xf, q);
                const SkMatrix m = xformMatrix(r, xf);
                const SkPoint corner[4] = {{(float)r.x, (float)r.y},
                                           {(float)r.right(), (float)r.y},
                                           {(float)r.right(), (float)r.bottom()},
                                           {(float)r.x, (float)r.bottom()}};
                for (int i = 0; i < 4; ++i) {
                    const SkPoint p = m.mapXY(corner[i].x(), corner[i].y());
                    worst = std::max(worst, std::max(std::fabs(p.x() - q[i].x), std::fabs(p.y() - q[i].y)));
                }
            }
        ok = worst <= 0.05f;
        char lbl[160];
        std::snprintf(lbl, sizeof lbl,
                      "transform: 提交用的矩阵与手柄四角公式同源（最大偏差 %.3f px）", worst);
        check(ok, lbl);
    }

    {
        Editor e;
        e.newDocument(128, 128, Color(255, 255, 255, 255));
        e.doc.addEmptyLayer("变换");
        Layer* l = e.doc.activeLayer();
        l->img.fillAll(0);
        l->img.fillRect(Rect{10, 10, 20, 10}, 0xff0000ffu);
        e.doc.markAllDirty();

        auto countRed = [](const Image& img) {
            const uint32_t* p = img.pixels();
            int n = 0;
            for (size_t i = 0; i < (size_t)img.width() * (size_t)img.height(); ++i)
                if ((p[i] >> 24) > 10 && (p[i] & 0xff) > 120 && ((p[i] >> 8) & 0xff) < 60) ++n;
            return n;
        };

    {
        Editor e;
        e.newDocument(80, 60, Color(0, 0, 0, 0));
        Layer* L = e.doc.activeLayer();
        L->img.detach();
        for (int y = 12; y < 40; y += 2)
            for (int x = 12; x < 60; x += 2)
                L->img.fillRect(Rect{x, y, 2, 2}, Color(255, 255, 255, 255).packed());
        const Image sharp0 = e.doc.layers[0].img;
        for (int k = 0; k < 5; ++k) {
            e.beginMoveAt(30, 30, false);
            e.endMove();
        }
        const Image after0 = e.doc.layers[0].img;
        int acc0 = 0;
        for (int y = 0; y < std::min(sharp0.height(), after0.height()); ++y)
            for (int x = 0; x < std::min(sharp0.width(), after0.width()); ++x)
                acc0 += std::abs((int)(sharp0.pixels()[(size_t)y * sharp0.width() + (size_t)x] & 255u) -
                                 (int)(after0.pixels()[(size_t)y * after0.width() + (size_t)x] & 255u));
        check(acc0 == 0 && after0.width() == sharp0.width(),
              "click: 连续单击图片 5 次，图层像素一点不变");
    }

    {
        Editor e;
        e.newDocument(200, 200, Color(255, 255, 255, 255));
        e.doc.addEmptyLayer("单角");
        Layer* l = e.doc.activeLayer();
        l->img.fillAll(0);
        l->img.fillRect(Rect{20, 30, 90, 60}, Color(200, 80, 40, 255).packed());
        e.doc.markAllDirty();
        e.beginFreeTransform(e.doc.active);
        Vec2f q0[4];
        e.transformQuad(q0);
        const Vec2f aim{q0[2].x + 26.0f, q0[2].y + 18.0f};
        e.transformBeginGrab(3, Vec2f{q0[2].x, q0[2].y}, true);
        e.transformUpdateGrab(aim, false);
        Vec2f q1[4];
        e.transformQuad(q1);
        bool others = true;
        for (int i : {0, 1, 3})
            if (std::abs(q1[i].x - q0[i].x) > 0.6f || std::abs(q1[i].y - q0[i].y) > 0.6f) others = false;
        const bool dragged = std::abs(q1[2].x - aim.x) < 0.6f && std::abs(q1[2].y - aim.y) < 0.6f;
        char lb[200];
        std::snprintf(lb, sizeof lb,
                      "transform: Ctrl+角只拖该角，另外三角不动（角2 %.1f,%.1f 目标 %.1f,%.1f）", q1[2].x, q1[2].y,
                      aim.x, aim.y);
        check(others && dragged, lb);
        e.commitFreeTransform();
        const Rect after = e.doc.layerContentBounds(e.doc.active);
        check(!after.empty(), "transform: 单角透视写出非空像素");
    }

    if (!std::string(textFontPath()).empty()) {
        Editor e;
        e.newDocument(400, 300, Color(255, 255, 255, 255));
        TextData t;
        t.text = "文字";
        t.x = 60.0f;
        t.y = 80.0f;
        t.size = 36;
        t.color = Color(0, 0, 0, 255);
        e.doc.addTextLayer(t, "文字");
        int idx = -1;
        for (int i = 0; i < (int)e.doc.layers.size(); ++i)
            if (e.doc.layers[(size_t)i].kind == LayerKind::Text) idx = i;
        e.doc.refreshTextLayers();
        e.beginFreeTransform(idx);
        Vec2f q0[4];
        e.transformQuad(q0);
        e.transformBeginGrab(3, Vec2f{q0[2].x, q0[2].y}, true);
        e.transformUpdateGrab(Vec2f{q0[2].x + 40.0f, q0[2].y + 30.0f}, false);
        Vec2f q1[4];
        e.transformQuad(q1);
        const bool corner0Moved = std::abs(q1[0].x - q0[0].x) > 1.0f || std::abs(q1[0].y - q0[0].y) > 1.0f;
        check(corner0Moved, "transform: 文字层不接单角透视，仍按等比四角缩放");
    }

    {
        Editor e;
        e.newDocument(80, 60, Color(0, 0, 0, 0));
        Layer* L = e.doc.activeLayer();
        L->img.detach();
        L->img.fillRect(Rect{0, 0, 80, 60}, Color(0, 0, 0, 0).packed());
        for (int y = 12; y < 40; y += 2)
            for (int x = 12; x < 60; x += 2)
                L->img.fillRect(Rect{x, y, 2, 2}, Color(255, 255, 255, 255).packed());
        e.doc.markAllDirty();
        const Image sharp = e.doc.layers[0].img;
        int acc = 0;
        for (int k = 0; k < 5; ++k) {
            if (!e.beginFreeTransform(0)) break;
            const SkRect gb = e.doc.layerDrawBounds(0);
            e.transformBeginGrab(0, Vec2f(gb.centerX(), gb.centerY()));
            e.transformUpdateGrab(Vec2f(gb.centerX() + 0.6f, gb.centerY() + 0.6f), false);
            e.transformEndGrab();
            e.commitFreeTransform();
        }
        const Image afterClicks = e.doc.layers[0].img;
        for (int y = 0; y < std::min(sharp.height(), afterClicks.height()); ++y)
            for (int x = 0; x < std::min(sharp.width(), afterClicks.width()); ++x) {
                const uint32_t a = sharp.pixels()[(size_t)y * sharp.width() + (size_t)x];
                const uint32_t b = afterClicks.pixels()[(size_t)y * afterClicks.width() + (size_t)x];
                acc += std::abs((int)(a & 255u) - (int)(b & 255u));
            }
        check(afterClicks.width() == sharp.width() && afterClicks.height() == sharp.height() && acc == 0,
              "transform: 连续点击进出变形，像素一个都不糊（纯平移不重采样）");
        int accTiny = 0;
        for (int k = 0; k < 5; ++k) {
            if (!e.beginFreeTransform(0)) break;
            const SkRect gb = e.doc.layerDrawBounds(0);
            e.transformBeginGrab(0, Vec2f(gb.centerX(), gb.centerY()));
            e.transformUpdateGrab(Vec2f(gb.centerX() + 0.4f, gb.centerY() + 0.4f), false);
            e.transformEndGrab();
            e.commitFreeTransform();
        }
        const Image afterTiny = e.doc.layers[0].img;
        for (int y = 0; y < std::min(sharp.height(), afterTiny.height()); ++y)
            for (int x = 0; x < std::min(sharp.width(), afterTiny.width()); ++x)
                accTiny += std::abs((int)(sharp.pixels()[(size_t)y * sharp.width() + (size_t)x] & 255u) -
                                    (int)(afterTiny.pixels()[(size_t)y * afterTiny.width() + (size_t)x] & 255u));
        check(accTiny == 0 && afterTiny.width() == sharp.width(),
              "transform: 点击时手抖小于半像素，预览不会被当成成品留下（松手按成品重算）");
        {
            Editor d2;
            d2.newDocument(60, 40, Color(0, 0, 0, 0));
            d2.doc.activeLayer()->img.detach();
            d2.doc.activeLayer()->img.fillRect(Rect{0, 0, 20, 20}, Color(255, 0, 0, 255).packed());
            const int revBefore = d2.doc.revision();
            d2.beginMoveAt(5, 5, false);
            d2.updateMove(7, 3, false);
            d2.endMove();
            check(d2.doc.revision() != revBefore, "move: 拖动后文档修订号推进，画布据此整帧重画");
        }
    }
        check(e.beginFreeTransform(e.doc.active), "transform: 进入自由变换");
        const Rect b0 = e.doc.layerContentBounds(e.doc.active);
        check(b0.w == 20 && b0.h == 10, "transform: 变换源 = 内容边界");

        e.transformBeginGrab(3, Vec2f{40, 25});
        e.transformUpdateGrab(Vec2f{40, 25}, false);
        check(e.transformMoved(), "transform: 拖动后参数已变");
        const Rect b1 = e.doc.layerContentBounds(e.doc.active);
        check(b1.w >= 39 && b1.w <= 41 && b1.h >= 19 && b1.h <= 21 && countRed(e.doc.activeLayer()->img) == 800,
              "transform: 2× 缩放后尺寸与像素数正确");

        e.transformBeginGrab(9, Vec2f{40, 15});
        e.transformUpdateGrab(Vec2f{20, 35}, false);
        const Rect b2 = e.doc.layerContentBounds(e.doc.active);
        const bool rotOk = b2.w <= 21 && b2.h >= 39 && countRed(e.doc.activeLayer()->img) == 800;
        char lrot[200];
        std::snprintf(lrot, sizeof lrot, "transform: 旋转 90° 后宽高互换、像素数不变 框=%d,%d,%d,%d 红=%d rot=%.3f",
                      b2.x, b2.y, b2.w, b2.h, countRed(e.doc.activeLayer()->img), e.transformRotation());
        check(rotOk, lrot);

        e.commitFreeTransform();
        check(e.hist.canUndo(), "transform: 提交后记入历史");
        e.undo();
        const Rect b3 = e.doc.layerContentBounds(e.doc.active);
        check(b3.w == 20 && b3.h == 10, "transform: 撤销一步回到变换前");

        check(e.beginFreeTransform(e.doc.active), "transform: 再次进入");
        e.transformBeginGrab(3, Vec2f{40, 25});
        e.transformUpdateGrab(Vec2f{40, 25}, false);
        e.cancelFreeTransform();
        const Rect b4 = e.doc.layerContentBounds(e.doc.active);
        check(b4.w == 20 && b4.h == 10 && !e.hist.canUndo(), "transform: 取消恢复原状、撤销栈零污染");

        check(e.beginFreeTransform(e.doc.active), "transform: 进入以验证移动");
        const Rect m0 = e.doc.layerContentBounds(e.doc.active);
        e.transformBeginGrab(0, Vec2f{20, 15});
        e.transformUpdateGrab(Vec2f{40, 35}, false);
        const Rect m1 = e.doc.layerContentBounds(e.doc.active);
        check(m1.x == m0.x + 20 && m1.y == m0.y + 20 && m1.w == m0.w && m1.h == m0.h &&
                  countRed(e.doc.activeLayer()->img) == 200,
              "transform: 框内移动内容跟随");
        e.cancelFreeTransform();
    }

    {
        auto cornerScale = [](bool shift, Rect& out) {
            Editor ze;
            ze.newDocument(200, 200, Color(255, 255, 255, 255));
            ze.doc.addEmptyLayer("角");
            Layer* zl = ze.doc.activeLayer();
            zl->img.fillAll(0);
            zl->img.fillRect(Rect{30, 20, 20, 10}, Color(255, 0, 0, 255).packed());
            ze.beginFreeTransform(ze.doc.active);
            const Rect b0 = ze.doc.layerContentBounds(ze.doc.active);
            const float cX = (float)b0.x + (float)b0.w * 0.5f, cY = (float)b0.y + (float)b0.h * 0.5f;
            const Vec2f corner{(float)b0.right(), (float)b0.bottom()};
            ze.transformBeginGrab(3, corner);
            ze.transformUpdateGrab(Vec2f{cX + (float)b0.w * 1.0f, cY + (float)b0.h * 1.5f}, shift);
            out = ze.doc.layerContentBounds(ze.doc.active);
            ze.commitFreeTransform();
        };
        Rect freeR{}, propR{};
        cornerScale(false, propR);
        cornerScale(true, freeR);
        char lb[240];
        std::snprintf(lb, sizeof lb, "transform-ratio: 默认保持比例 %dx%d，Shift 不保持比例 %dx%d（源 20x10）", propR.w,
                      propR.h, freeR.w, freeR.h);
        check(propR.w >= 59 && propR.w <= 61 && propR.h >= 29 && propR.h <= 31, lb);
        check(freeR.w >= 39 && freeR.w <= 41 && freeR.h >= 29 && freeR.h <= 31, lb);
    }

    {
        Editor lw;
        lw.newDocument(200, 200, Color(255, 255, 255, 255));
        lw.doc.addEmptyLayer("局部");
        Layer* ll = lw.doc.activeLayer();
        ll->img.fillAll(0);
        ll->img.fillRect(Rect{40, 40, 120, 120}, Color(200, 120, 60, 255).packed());
        lw.doc.markAllDirty();
        const Rect b0 = lw.doc.layerContentBounds(lw.doc.active);

        check(lw.beginLocalWarp(lw.doc.active), "localwarp: 像素层能进局部变形");
        {
            auto mv = makeMoveTool();
            mv->onLeave(lw);
            check(lw.localWarpActive(), "localwarp: 鼠标离开画布不提交（只有确定才提交）");
            mv->onLeave(lw);
            check(lw.localWarpActive(), "localwarp: 反复离开仍保持会话");
        }
        check(lw.beginFreeTransform(lw.doc.active), "transform: 图像层能进变形");
        {
            auto mv2 = makeMoveTool();
            mv2->onLeave(lw);
            check(lw.transforming(), "transform: 鼠标离开画布不取消变形（调数值后仍能按确定）");
            lw.cancelFreeTransform();
        }
        const LocalWarpParams lp = lw.localWarpRegion();
        check(lw.localWarpActive() && std::abs(lp.radius.x - 150.0f) < 1e-3 && std::abs(lp.radius.y - 100.0f) < 1e-3,
              "localwarp: 椭圆默认 300×200");
        check(lw.localWarpField(4) == 2.0 && std::abs(lw.localWarpField(3) - 0.35) < 1e-6,
              "localwarp: 默认谐波 2 / 泊松 0.35");
        lw.localWarpSetField(4, 20.0);
        check(std::abs(lw.localWarpField(4) - 8.0) < 1e-6, "localwarp: 谐波上限夹到 8");
        lw.localWarpSetField(4, 2.0);
        lw.localWarpSetField(5, 0.0);
        check(std::abs(lw.localWarpField(5) - 0.2) < 1e-6, "localwarp: 衰减下限夹到 0.2");
        lw.localWarpSetField(5, 1.0);

        LocalWarpParams q;
        q.center = Vec2f{100.0f, 100.0f};
        q.radius = Vec2f{40.0f, 40.0f};
        q.amp = Vec2f{10.0f, 10.0f};
        q.poisson = 0.0f;
        q.harmonic = 0;
        check(localWarpHasEffect(q), "localwarp: 有幅度即算有作用");
        check(localWarpField(q, Vec2f{140.0f, 100.0f}).x == 0.0f, "localwarp: 半径边界位移归零");
        check(std::fabs(localWarpField(q, Vec2f{120.0f, 100.0f}).x) > 1.0f, "localwarp: 半径内确实有位移");
        check(localWarpField(q, Vec2f{120.0f, 100.0f}).y == 0.0f, "localwarp: 轴上位移不串轴");
        q.poisson = 0.35f;
        q.harmonic = 2;
        check(std::fabs(localWarpField(q, Vec2f{130.0f, 115.0f}).y) > 0.01f, "localwarp: 泊松切向项产生横向位移");
        q.poisson = 0.0f;
        q.harmonic = 0;

        LocalWarpParams rot;
        rot.center = Vec2f{100.0f, 100.0f};
        rot.radius = Vec2f{40.0f, 10.0f};
        rot.rotDeg = 90.0f;
        rot.amp = Vec2f{10.0f, 10.0f};
        rot.poisson = 0.0f;
        rot.harmonic = 0;
        check(std::fabs(localWarpField(rot, Vec2f{100.0f, 130.0f}).y) > 1.0f, "localwarp: 旋转后长轴转到纵向");
        check(localWarpField(rot, Vec2f{130.0f, 100.0f}).x == 0.0f, "localwarp: 旋转后短轴方向无位移");

        int ox = 0, oy = 0;
        Image out = localWarpApply(ll->img, ll->x, ll->y, q, ox, oy);
        check(!out.empty() && out.width() > 80, "localwarp: 输出非空且按幅度外扩");
        check(ox <= 72 && oy <= 72, "localwarp: 输出的原点向外挪");

        const int revA = lw.doc.revision();
        lw.commitLocalWarp();
        check(!lw.localWarpActive() && lw.hist.canUndo(), "localwarp: 提交退出并入撤销栈");
        check(lw.doc.revision() != revA, "localwarp: 提交推进修订号");
        lw.undo();
        const Rect b2 = lw.doc.layerContentBounds(lw.doc.active);
        check(b2.w <= b0.w + 2, "localwarp: 撤销一步回到原尺寸");

        lw.beginLocalWarp(lw.doc.active);
        lw.localWarpSetField(0, -30.0);
        lw.cancelLocalWarp();
        const Rect b3 = lw.doc.layerContentBounds(lw.doc.active);
        check(!lw.localWarpActive() && b3.w == b0.w && b3.h == b0.h, "localwarp: 取消回到原状");

        lw.beginLocalWarp(lw.doc.active);
        const int n0 = lw.localWarpRegionCount();
        lw.localWarpPress(Vec2f{300.0f, 100.0f});
        lw.localWarpDrag(Vec2f{400.0f, 200.0f});
        lw.localWarpRelease();
        const LocalWarpParams rp = lw.localWarpRegion();
        check(std::abs(rp.center.x - 350.0f) < 1.0f && std::abs(rp.center.y - 150.0f) < 1.0f &&
                  std::abs(rp.radius.x - 50.0f) < 1.0f && std::abs(rp.radius.y - 50.0f) < 1.0f,
              "localwarp: 线外拖拽定义椭圆区域");
        check(lw.localWarpRegionCount() == n0 + 1, "localwarp: 线外拖拽新增一个椭圆");
        lw.localWarpCopyRegion();
        const Vec2f cp = lw.localWarpRegion().center;
        lw.localWarpDeleteRegion();
        check(lw.localWarpRegionCount() == n0, "localwarp: Delete 删掉当前椭圆");
        lw.localWarpPasteRegion();
        check(lw.localWarpRegionCount() == n0 + 1, "localwarp: 粘贴多出一个椭圆");
        check(std::abs(lw.localWarpRegion().center.x - (cp.x + 24.0f)) < 0.6f &&
                  std::abs(lw.localWarpRegion().center.y - (cp.y + 24.0f)) < 0.6f,
              "localwarp: 粘贴出的椭圆错开 24px");
        lw.localWarpPasteRegion();
        check(lw.localWarpRegionCount() == n0 + 2, "localwarp: 再粘贴再多一个");
        const Vec2f c0 = lw.localWarpRegion().center;
        lw.localWarpSetField(0, 37.0);
        check(std::abs(lw.localWarpField(0) - 37.0) < 0.01, "localwarp: 改数值只动当前椭圆");
        check(lw.localWarpRegionAt(0).amp.x != lw.localWarpRegionAt(lw.localWarpRegionCount() - 1).amp.x,
              "localwarp: 两个椭圆的幅度各自独立");
        check(lw.localWarpContains(Vec2f{c0.x, c0.y}), "localwarp: 命中椭圆内一点");
        check(!lw.localWarpContains(Vec2f{c0.x + lw.localWarpRegion().radius.x + 40.0f, c0.y}),
              "localwarp: 椭圆外不算命中");
        std::vector<LocalWarpParams> two;
        two.push_back(lw.localWarpRegionAt(0));
        two.push_back(lw.localWarpRegionAt(1));
        int ax = 0, ay = 0;
        Image img2 = localWarpApplyAll(ll->img, ll->x, ll->y, two, ax, ay);
        check(!img2.empty(), "localwarp: 两个椭圆一起出图");
        lw.cancelLocalWarp();
        check(!lw.localWarpActive(), "localwarp: 取消退出后状态清空");
    }

    {
        Editor lw2;
        lw2.newDocument(200, 200, Color(255, 255, 255, 255));
        lw2.doc.addEmptyLayer("局部2");
        Layer* a2 = lw2.doc.activeLayer();
        a2->img.fillAll(0);
        a2->img.fillRect(Rect{60, 60, 80, 80}, Color(240, 200, 80, 255).packed());
        lw2.doc.markAllDirty();
        lw2.beginLocalWarp(lw2.doc.active);
        const LocalWarpParams p2 = lw2.localWarpRegion();
        const Vec2f c2 = p2.center;
        lw2.localWarpPress(Vec2f{100.0f, 100.0f});
        lw2.localWarpDrag(Vec2f{120.0f, 110.0f});
        lw2.localWarpRelease();
        check(std::abs(lw2.localWarpRegion().center.x - 120.0f) < 1.5f &&
                  std::abs(lw2.localWarpRegion().center.y - 110.0f) < 1.5f,
              "localwarp: 椭圆内拖动即移动区域");
        check(lw2.localWarpRegion().radius.x == p2.radius.x, "localwarp: 移动不改半径");

        const float rx0 = lw2.localWarpRegion().radius.x;
        const Vec2f c3 = lw2.localWarpRegion().center;
        const float rxNow = lw2.localWarpRegion().radius.x;
        lw2.localWarpPress(Vec2f{c3.x + rxNow, c3.y});
        lw2.localWarpDrag(Vec2f{c3.x + rxNow + 30.0f, c3.y});
        lw2.localWarpRelease();
        check(lw2.localWarpRegion().radius.x > rx0 + 20.0f, "localwarp: 右侧把手拉长横半径");

        const float ry0 = lw2.localWarpRegion().radius.y;
        lw2.localWarpPress(Vec2f{c3.x, c3.y + ry0});
        lw2.localWarpDrag(Vec2f{c3.x, c3.y + ry0 + 25.0f});
        lw2.localWarpRelease();
        check(lw2.localWarpRegion().radius.y > ry0 + 15.0f, "localwarp: 下侧把手拉长纵半径");

        const int rev0 = lw2.doc.revision();
        const float rot0 = lw2.localWarpRegion().rotDeg;
        const LocalWarpParams pp = lw2.localWarpRegion();
        lw2.localWarpPress(Vec2f{pp.center.x + pp.radius.x * 0.75f, pp.center.y + pp.radius.y * 0.75f});
        lw2.localWarpDrag(Vec2f{pp.center.x - pp.radius.x * 0.75f, pp.center.y + pp.radius.y * 0.75f});
        lw2.localWarpRelease();
        check(std::fabs(lw2.localWarpRegion().rotDeg - rot0) > 45.0f, "localwarp: 线外拖拽旋转区域");
        lw2.cancelLocalWarp();

        Editor lw3;
        lw3.newDocument(200, 200, Color(255, 255, 255, 255));
        lw3.doc.addEmptyLayer("裁剪");
        Layer* a3 = lw3.doc.activeLayer();
        a3->img.fillAll(0);
        a3->img.fillRect(Rect{10, 10, 180, 180}, Color(90, 170, 230, 255).packed());
        lw3.doc.markAllDirty();
        const Rect cb0 = lw3.doc.layerContentBounds(lw3.doc.active);
        const int a3BaseW = a3->img.width();
        lw3.beginLocalWarp(lw3.doc.active);
        lw3.localWarpSetField(0, 40.0);
        lw3.localWarpSetField(1, 40.0);
        const Rect cb1 = lw3.doc.layerContentBounds(lw3.doc.active);
        check(cb1.w >= cb0.w - 1 && cb1.h >= cb0.h - 1, "localwarp: 预览不裁剪图层内容");
        check((layerPixelAt(*lw3.doc.activeLayer(), 15, 15) >> 24) == 255, "localwarp: 角落像素仍在");
        const Rect pr = lw3.localWarpPreviewRect();
        int pvx = 0, pvy = 0;
        const Image& pv = lw3.localWarpPreviewImage(pvx, pvy);
        check(!pv.empty() && pr.w > cb0.w - 1 && pr.h > cb0.h - 1, "localwarp: 预览覆盖层单独出图且盖住内容");
        check(lw3.doc.activeLayer()->img.width() == a3BaseW, "localwarp: 预览不改图层尺寸");
        lw3.cancelLocalWarp();
    }

    {
        Editor lw4;
        lw4.newDocument(400, 400, Color(255, 255, 255, 255));
        lw4.doc.addEmptyLayer("缩放");
        Layer* a4 = lw4.doc.activeLayer();
        a4->img.fillAll(0);
        a4->img.fillRect(Rect{140, 140, 120, 120}, Color(120, 200, 140, 255).packed());
        lw4.doc.markAllDirty();
        lw4.beginLocalWarp(lw4.doc.active);
        const Vec2f c4 = lw4.localWarpRegion().center;
        const float rx4 = lw4.localWarpRegion().radius.x;
        const float ry4 = lw4.localWarpRegion().radius.y;
        check(lw4.localWarpScale(1.5f), "localwarp: 滚轮缩放接口生效");
        check(lw4.localWarpRegion().radius.x > rx4 + 50.0f && lw4.localWarpRegion().radius.y > ry4 + 30.0f,
              "localwarp: 椭圆内滚轮即等比缩放");
        check(std::abs(lw4.localWarpRegion().center.x - c4.x) < 1.0f &&
                  std::abs(lw4.localWarpRegion().center.y - c4.y) < 1.0f,
              "localwarp: 缩放时中心不动");
        check(lw4.localWarpRegion().rotDeg == 0.0f, "localwarp: 缩放不改旋转");
    }

    {
        Editor lwd;
        lwd.newDocument(200, 200, Color(255, 255, 255, 255));
        lwd.doc.addEmptyLayer("拖动");
        Layer* ld = lwd.doc.activeLayer();
        ld->img.fillAll(0);
        ld->img.fillRect(Rect{40, 40, 120, 120}, Color(200, 120, 60, 255).packed());
        lwd.doc.markAllDirty();
        lwd.beginLocalWarp(lwd.doc.active);
        const LocalWarpParams pd = lwd.localWarpRegion();
        lwd.localWarpPress(Vec2f{pd.center.x, pd.center.y});
        lwd.localWarpDrag(Vec2f{pd.center.x + 560.0f, pd.center.y + 560.0f});
        lwd.localWarpRelease();
        const Layer* L2 = lwd.doc.activeLayer();
        const int lx2 = 60 - L2->x, ly2 = 60 - L2->y;
        const uint32_t v2 = (lx2 >= 0 && ly2 >= 0 && lx2 < L2->img.width() && ly2 < L2->img.height())
                                ? L2->img.pixels()[(size_t)ly2 * (size_t)L2->img.width() + (size_t)lx2]
                                : 0;
        check(v2 == Color(200, 120, 60, 255).packed(), "localwarp: 椭圆拖走后原位像素复原");

        const int wOnce = L2->img.width();
        const Vec2f cNow = lwd.localWarpRegion().center;
        lwd.localWarpPress(cNow);
        for (int i = 0; i < 10; ++i) {
            lwd.localWarpDrag(Vec2f{cNow.x + (float)(i % 2 ? -4 : 4), cNow.y + (float)(i % 2 ? -4 : 4)});
            lwd.localWarpRelease();
            lwd.localWarpPress(lwd.localWarpRegion().center);
        }
        lwd.localWarpRelease();
        check(lwd.doc.activeLayer()->img.width() == wOnce, "localwarp: 小幅来回拖动不再扩图");
        lwd.cancelLocalWarp();
    }

    {
        Editor lpm;
        lpm.newDocument(1200, 900, Color(255, 255, 255, 255));
        lpm.doc.addEmptyLayer("性能");
        Layer* lp = lpm.doc.activeLayer();
        lp->img.fillAll(Color(90, 120, 200, 255).packed());
        lpm.doc.markAllDirty();
        lpm.beginLocalWarp(lpm.doc.active);
        const LocalWarpParams pp = lpm.localWarpRegion();
        lpm.localWarpPress(Vec2f{pp.center.x, pp.center.y});
        LARGE_INTEGER fq, t0, t1;
        QueryPerformanceFrequency(&fq);
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < 60; ++i)
            lpm.localWarpDrag(Vec2f{pp.center.x + (float)(i % 2 ? -1 : 1), pp.center.y});
        QueryPerformanceCounter(&t1);
        const double per = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart / 60.0;
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < 20; ++i)
            lpm.localWarpDrag(Vec2f{pp.center.x + 300.0f * (float)(i + 1), pp.center.y});
        QueryPerformanceCounter(&t1);
        const double perRebuild = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart / 20.0;
        std::fprintf(stderr, "[info] localwarp 拖动 300x200 于 1200x900 图层: 脏矩形增量 %.2f ms/帧, 越界整体重建 %.2f ms/帧\n",
                     per, perRebuild);
        lpm.cancelLocalWarp();

        Editor lps;
        lps.newDocument(1200, 900, Color(255, 255, 255, 255));
        lps.doc.addEmptyLayer("缩放");
        lps.doc.activeLayer()->img.fillAll(Color(90, 120, 200, 255).packed());
        lps.doc.markAllDirty();
        lps.beginLocalWarp(lps.doc.active);
        const LocalWarpParams ps0 = lps.localWarpRegion();
        lps.localWarpPress(Vec2f{ps0.center.x + ps0.radius.x, ps0.center.y});
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < 40; ++i)
            lps.localWarpDrag(Vec2f{ps0.center.x + ps0.radius.x + 8.0f * (float)(i + 1), ps0.center.y});
        QueryPerformanceCounter(&t1);
        const double perScale = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart / 40.0;
        std::fprintf(stderr, "[info] localwarp 缩放拖把手: %.2f ms/帧, 半径 %.0f -> %.0f, 输出 %dX%d\n", perScale,
                     (double)ps0.radius.x, (double)lps.localWarpRegion().radius.x,
                     lps.doc.activeLayer()->img.width(), lps.doc.activeLayer()->img.height());
        lps.cancelLocalWarp();

        Editor l3;
        l3.newDocument(1200, 900, Color(255, 255, 255, 255));
        l3.doc.addEmptyLayer("三椭圆");
        l3.doc.activeLayer()->img.fillAll(Color(90, 120, 200, 255).packed());
        l3.doc.markAllDirty();
        l3.beginLocalWarp(l3.doc.active);
        l3.localWarpCopyRegion();
        l3.localWarpPasteRegion();
        l3.localWarpPasteRegion();
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < 20; ++i) l3.localWarpScale(1.03f);
        QueryPerformanceCounter(&t1);
        const double per3 = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart / 20.0;
        std::fprintf(stderr, "[info] 三个椭圆同时缩放: %.2f ms/帧, 椭圆数 %d, 预览 %dx%d\n", per3,
                     l3.localWarpRegionCount(), l3.localWarpPreviewRect().w, l3.localWarpPreviewRect().h);
        l3.cancelLocalWarp();
        lpm.cancelLocalWarp();
    }


    {
        Editor mw;
        mw.newDocument(200, 200, Color(255, 255, 255, 255));
        mw.doc.addEmptyLayer("网格");
        Layer* ml = mw.doc.activeLayer();
        ml->img.fillAll(0);
        ml->img.fillRect(Rect{20, 20, 120, 80}, Color(30, 160, 240, 255).packed());
        mw.doc.markAllDirty();
        mw.setMeshDensity(8);
        check(mw.meshDensity == 8, "mesh: 网格密度默认 8");
        mw.setMeshDensity(1);
        check(mw.meshDensity == 2, "mesh: 密度下限夹到 2");
        mw.setMeshDensity(20);
        check(mw.meshDensity == 16, "mesh: 密度上限夹到 16");
        mw.setMeshDensity(8);

        check(mw.beginMeshWarp(mw.doc.active), "mesh: 像素层能进网格变形");
        check(mw.meshWarpActive() && mw.meshWarpCells() == 8, "mesh: 8×8 格");
        check(mw.meshPointCount() == 81, "mesh: 9×9 = 81 个格点");
        const Rect mb0 = mw.doc.layerContentBounds(mw.doc.active);
        const Vec2f p00 = mw.meshPoint(0);
        const Vec2f pnn = mw.meshPoint(80);
        check(std::abs(p00.x - (float)mb0.x) < 0.6f && std::abs(p00.y - (float)mb0.y) < 0.6f &&
                  std::abs(pnn.x - (float)mb0.right()) < 0.6f && std::abs(pnn.y - (float)mb0.bottom()) < 0.6f,
              "mesh: 网格四角贴着图层内容边界");

        const Vec2f corner = mw.meshPoint(80);
        mw.meshPress(corner, 4.0f);
        check(mw.meshDragIndex() == 80, "mesh: 点中右下角格点");
        mw.meshDrag(Vec2f{corner.x + 20.0f, corner.y + 10.0f});
        mw.meshRelease();
        const Rect mb1 = mw.doc.layerContentBounds(mw.doc.active);
        check(mb1.w >= mb0.w + 15 && mb1.h >= mb0.h + 5, "mesh: 拖格点后图层内容跟着变形");
        const int revBefore = mw.doc.revision();
        mw.commitMeshWarp();
        check(!mw.meshWarpActive(), "mesh: 提交后退出网格变形");
        check(mw.hist.canUndo(), "mesh: 提交进撤销栈");
        const Rect mb2 = mw.doc.layerContentBounds(mw.doc.active);
        check(mb2.w == mb1.w || std::abs(mb2.w - mb1.w) <= 2, "mesh: 提交后保留变形结果");
        mw.undo();
        const Rect mb3 = mw.doc.layerContentBounds(mw.doc.active);
        check(mb3.w == mb0.w && mb3.h == mb0.h, "mesh: 撤销一步回到变形前尺寸");
        check(mw.doc.revision() != revBefore, "mesh: 撤销推进修订号");

        mw.beginMeshWarp(mw.doc.active);
        mw.meshPress(mw.meshPoint(6), 4.0f);
        mw.meshDrag(Vec2f{mw.meshPoint(6).x + 30.0f, mw.meshPoint(6).y});
        mw.meshRelease();
        mw.cancelMeshWarp();
        const Rect mb4 = mw.doc.layerContentBounds(mw.doc.active);
        check(!mw.meshWarpActive() && mb4.w == mb0.w && mb4.h == mb0.h, "mesh: 取消回到原状");

        {
            Editor mu;
            mu.newDocument(200, 200, Color(255, 255, 255, 255));
            mu.doc.addEmptyLayer("步");
            Layer* ul = mu.doc.activeLayer();
            ul->img.fillAll(0);
            ul->img.fillRect(Rect{20, 20, 120, 80}, Color(60, 170, 90, 255).packed());
            mu.doc.markAllDirty();
            const Rect u0 = mu.doc.layerContentBounds(mu.doc.active);
            mu.beginMeshWarp(mu.doc.active);
            const Vec2f a0 = mu.meshPoint(16);
            const Vec2f b0 = mu.meshPoint(80);
            mu.meshPress(a0, 4.0f);
            mu.meshDrag(Vec2f{a0.x, a0.y + 14.0f});
            mu.meshRelease();
            mu.meshPress(b0, 4.0f);
            mu.meshDrag(Vec2f{b0.x + 18.0f, b0.y});
            mu.meshRelease();
            check(mu.meshStepCount() == 2, "mesh-undo: 两个点各记一步");
            mu.undo();
            check(mu.meshWarpActive() && mu.meshStepCount() == 1, "mesh-undo: 撤销一步仍在变形中");
            const Vec2f b1 = mu.meshPoint(80);
            check(std::abs(b1.x - b0.x) < 0.2f && std::abs(b1.y - b0.y) < 0.2f,
                  "mesh-undo: 撤销回到上一个点的原位（后拖的点复原）");
            const Vec2f a1 = mu.meshPoint(16);
            check(std::abs(a1.y - (a0.y + 14.0f)) < 0.2f, "mesh-undo: 先拖的点仍在变形位置");
            mu.undo();
            check(mu.meshStepCount() == 0, "mesh-undo: 再撤销一步回到零步");
            const Rect u1 = mu.doc.layerContentBounds(mu.doc.active);
            check(u1.w == u0.w && u1.h == u0.h, "mesh-undo: 步数清光后图层回到原尺寸");
            mu.redo();
            check(mu.meshStepCount() == 1, "mesh-redo: 恢复被撤的那一步");
            mu.cancelMeshWarp();
            const Rect u2 = mu.doc.layerContentBounds(mu.doc.active);
            check(u2.w == u0.w && u2.h == u0.h, "mesh-undo: 取消仍是回到原状");
        }

        mw.doc.addTextLayer(TextData{}, "字");
        int tIdx = (int)mw.doc.layers.size() - 1;
        mw.doc.active = tIdx;
        check(!mw.beginMeshWarp(tIdx), "mesh: 文字层不接网格变形");
    }

    {
        Editor mp;
        mp.newDocument(200, 200, Color(255, 255, 255, 255));
        mp.doc.addEmptyLayer("钉");
        Layer* pl = mp.doc.activeLayer();
        pl->img.fillAll(0);
        pl->img.fillRect(Rect{20, 20, 120, 80}, Color(40, 150, 90, 255).packed());
        mp.doc.markAllDirty();
        mp.setMeshDensity(8);
        const Image snap = mp.doc.activeLayer()->img;
        mp.beginMeshWarp(mp.doc.active);
        const Rect q0 = mp.doc.layerContentBounds(mp.doc.active);
        const Vec2f g0 = mp.meshPoint(0);
        const Vec2f g80 = mp.meshPoint(80);
        check(mp.meshPinCount() == 81 && mp.meshExtraCount() == 0, "网络: 进入是 81 个网点、0 个新增网点");
        mp.meshPress(Vec2f{100.0f, 60.0f}, 4.0f);
        check(mp.meshExtraCount() == 1, "网络: 点击新增一个网点");
        {
            std::vector<Vec2f> ep, ed;
            mp.meshSurfaceData(ep, ed);
            float worst = 0.0f;
            for (size_t k = 0; k < ed.size(); ++k) worst = std::max(worst, std::hypot(ed[k].x, ed[k].y));
            check(worst < 0.05f, "网络: 刚点下的网点自身零位移（图像不被拉）");
            std::printf("[info] mesh first-click extra disp = %.4f px\n", (double)worst);
        }
        {
            const int lx0 = mp.doc.activeLayer()->x, ly0 = mp.doc.activeLayer()->y;
            mp.meshDrag(Vec2f{101.5f, 60.5f});
            mp.meshRelease();
            const Image& cur = mp.doc.activeLayer()->img;
            const bool same = mp.doc.activeLayer()->x == lx0 && mp.doc.activeLayer()->y == ly0 &&
                              cur.width() == snap.width() && cur.height() == snap.height() &&
                              std::memcmp(cur.pixels(), snap.pixels(),
                                          (size_t)cur.width() * (size_t)cur.height() * 4u) == 0;
            check(same, "网络: 点击时的轻微抖动不被当成拖动（图像不动）");
        }
        mp.meshRelease();
        const Rect q1 = mp.doc.layerContentBounds(mp.doc.active);
        check(q1.x == q0.x && q1.y == q0.y && q1.w == q0.w && q1.h == q0.h, "网络: 只加网点不拖图像不动");
        {
            const Image& cur = mp.doc.activeLayer()->img;
            const bool same = cur.width() == snap.width() && cur.height() == snap.height() &&
                              std::memcmp(cur.pixels(), snap.pixels(),
                                          (size_t)cur.width() * (size_t)cur.height() * 4u) == 0;
            check(same, "网络: 新增网点后重采样结果与原图逐像素一致");
        }
        mp.meshPress(Vec2f{100.0f, 60.0f}, 4.0f);
        check(mp.meshExtraCount() == 1, "网络: 再点同一个网点不会重复新增");
        mp.meshDrag(Vec2f{120.0f, 60.0f});
        mp.meshRelease();
        check(mp.meshPoint(0).x == g0.x && mp.meshPoint(0).y == g0.y && mp.meshPoint(80).x == g80.x &&
                  mp.meshPoint(80).y == g80.y,
              "网络: 拖新网点不移动网格点（不拉网络）");
        check(mp.meshWarpMoved(), "网络: 拖新网点确实改了图像");
        check(mp.meshDeletePinAt(Vec2f{120.0f, 60.0f}, 6.0f), "网络: 右键删掉这个新增网点");
        check(mp.meshExtraCount() == 0, "网络: 新增网点已移除");
        const Rect q2 = mp.doc.layerContentBounds(mp.doc.active);
        check(q2.x == q0.x && q2.y == q0.y && q2.w == q0.w && q2.h == q0.h, "网络: 删网点后图像回原状");
        check(!mp.meshDeletePinAt(Vec2f{10.0f, 10.0f}, 6.0f), "网络: 空白处右键不删任何东西");
        mp.cancelMeshWarp();
        mp.doc.activeLayer()->img.fillRect(Rect{25, 20, 1, 80}, Color(0, 0, 0, 255).packed());
        mp.doc.activeLayer()->img.fillRect(Rect{20, 25, 120, 1}, Color(0, 0, 0, 255).packed());
        mp.doc.markAllDirty();
        const Image snap2 = mp.doc.activeLayer()->img;
        const int snap2x = mp.doc.activeLayer()->x, snap2y = mp.doc.activeLayer()->y;
        mp.beginMeshWarp(mp.doc.active);
        {
            const Rect e0 = mp.doc.layerContentBounds(mp.doc.active);
            mp.meshPress(Vec2f{80.0f, 80.0f}, 6.0f);
            mp.meshDrag(Vec2f{110.0f, 80.0f});
            mp.meshRelease();
            const Rect e1 = mp.doc.layerContentBounds(mp.doc.active);
            std::printf("[info] mesh edge before %d,%d,%d,%d after %d,%d,%d,%d\n", e0.x, e0.y, e0.w, e0.h, e1.x,
                        e1.y, e1.w, e1.h);
            check(e1.x >= e0.x - 1 && e1.y >= e0.y - 1 && e1.x + e1.w <= e0.x + e0.w + 1 &&
                      e1.y + e1.h <= e0.y + e0.h + 1,
                  "网络: 拖中间网点时图像四边不外扩");
            const auto darkCol = [&](const Image& im, int x0, int yLocal) {
                int hit = -1;
                for (int x = 0; x < im.width(); ++x) {
                    const uint32_t v = im.pixels()[(size_t)yLocal * (size_t)im.width() + (size_t)x];
                    if ((v >> 24) > 200u && (v & 255u) < 30u && ((v >> 8) & 255u) < 30u &&
                        ((v >> 16) & 255u) < 30u) {
                        hit = x0 + x;
                        break;
                    }
                }
                return hit;
            };
            const auto darkRow = [&](const Image& im, int y0, int xLocal) {
                int hit = -1;
                for (int y = 0; y < im.height(); ++y) {
                    const uint32_t v = im.pixels()[(size_t)y * (size_t)im.width() + (size_t)xLocal];
                    if ((v >> 24) > 200u && (v & 255u) < 30u && ((v >> 8) & 255u) < 30u &&
                        ((v >> 16) & 255u) < 30u) {
                        hit = y0 + y;
                        break;
                    }
                }
                return hit;
            };
            const int colBefore = darkCol(snap2, snap2x, 40);
            const int rowBefore = darkRow(snap2, snap2y, 40);
            const int colAfter = darkCol(mp.doc.activeLayer()->img, mp.doc.activeLayer()->x, 40);
            const int rowAfter = darkRow(mp.doc.activeLayer()->img, mp.doc.activeLayer()->y, 40);
            const int localCol = darkCol(mp.doc.activeLayer()->img, 0, 40);
            const int localRow = darkRow(mp.doc.activeLayer()->img, 0, 40);
            std::printf("[info] mesh marker x %d->%d y %d->%d layerXY %d,%d local %d,%d size %dx%d\n", colBefore,
                        colAfter, rowBefore, rowAfter, mp.doc.activeLayer()->x, mp.doc.activeLayer()->y, localCol,
                        localRow, mp.doc.activeLayer()->img.width(), mp.doc.activeLayer()->img.height());
            check(colAfter >= colBefore - 1 && colAfter <= colBefore + 1 && rowAfter >= rowBefore - 1 &&
                      rowAfter <= rowBefore + 1,
                  "网络: 拖中间网点时边缘标记线不动");
        }
        mp.cancelMeshWarp();
    }
    {
        const int n = 4;
        std::vector<Vec2f> gp((size_t)(n + 1) * (size_t)(n + 1));
        std::vector<Vec2f> gp0((size_t)(n + 1) * (size_t)(n + 1));
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i) {
                gp[(size_t)j * (size_t)(n + 1) + (size_t)i] = Vec2f{20.0f * (float)i, 20.0f * (float)j};
                gp0[(size_t)j * (size_t)(n + 1) + (size_t)i] = Vec2f{20.0f * (float)i, 20.0f * (float)j};
            }
        gp[(size_t)2 * (size_t)(n + 1) + 2].x += 9.0f;
        gp[(size_t)2 * (size_t)(n + 1) + 2].y += 5.0f;
        float worstLin = 0.0f;
        for (int j = 0; j <= n * 4; ++j)
            for (int i = 0; i <= n * 4; ++i) {
                const float gu = (float)i / 4.0f, gv = (float)j / 4.0f;
                const float lx = 20.0f * gu;
                const float ly = 20.0f * gv;
                const Vec2f q = meshC1Surface(gp0, n, {}, {}, gu, gv);
                worstLin = std::max(worstLin, std::hypot(q.x - lx, q.y - ly));
            }
        check(worstLin < 0.02f, "网络: 静止网格上 C1 曲面等同线性映射（边缘不压缩）");
        std::printf("[info] mesh c1 linear-map worst error = %.4f px\n", (double)worstLin);
        check(std::hypot(meshC1Surface(gp, n, {}, {}, 2.0f, 2.0f).x - gp[(size_t)2 * (size_t)(n + 1) + 2].x,
                         meshC1Surface(gp, n, {}, {}, 2.0f, 2.0f).y - gp[(size_t)2 * (size_t)(n + 1) + 2].y) < 1e-3f,
              "网络: C1 曲面经过每一个网点");
        const auto tang = [&](float u, float v) {
            const float h = 0.01f;
            const Vec2f a = meshC1Surface(gp, n, {}, {}, u, v);
            const Vec2f b = meshC1Surface(gp, n, {}, {}, u + h, v);
            return Vec2f{(b.x - a.x) / h, (b.y - a.y) / h};
        };
        const Vec2f tl = tang(3.0f - 0.02f, 1.4f);
        const Vec2f tr = tang(3.0f, 1.4f);
        check(std::hypot(tl.x - tr.x, tl.y - tr.y) < 0.6f, "网络: 跨格边界切向连续（不出现折面）");
        float worstInv = 0.0f;
        float worstBack = 0.0f;
        for (int j = 1; j < n; ++j) {
            for (int i = 1; i < n; ++i) {
                for (int k = 1; k <= 3; ++k) {
                    const float cu = (float)i + (float)k * 0.25f;
                    const float cv = (float)j + (float)k * 0.2f;
                    const Vec2f q = meshC1Surface(gp, n, {}, {}, cu, cv);
                    float ru = 0.0f, rv = 0.0f;
                    meshC1Inverse(gp, n, q, ru, rv);
                    worstInv = std::max(worstInv, std::hypot(ru - cu, rv - cv));
                    const Vec2f back = meshC1Surface(gp, n, {}, {}, ru, rv);
                    worstBack = std::max(worstBack, std::hypot(back.x - q.x, back.y - q.y));
                }
            }
        }
        check(worstInv < 0.05f, "网络: 网格反解误差 < 0.05 格");
        check(worstBack < 0.05f, "网络: 已变形网格上点击新增网点不产生错位位移");
        std::printf("[info] mesh c1 inverse worst error = %.4f / click offset = %.4f px\n", (double)worstInv,
                    (double)worstBack);
    }


    {
        Editor be;
        be.newDocument(20, 10, Color(255, 255, 255, 255));
        be.doc.layers[0].img.detach();
        uint32_t* bp = be.doc.layers[0].img.pixels();
        for (int y = 4; y < 7; ++y)
            for (int x = 5; x < 8; ++x) bp[(size_t)y * 20 + x] = 0xff000000u;
        be.palette.fg[0] = 1.0f;
        be.palette.fg[1] = 0.0f;
        be.palette.fg[2] = 0.0f;
        be.palette.fg[3] = 1.0f;
        check(be.bucketFill(0, 0, 30, Editor::BucketFill::Foreground), "bucket: 前景色填充");
        bp = be.doc.layers[0].img.pixels();
        check((bp[0] & 0xff) == 255 && ((bp[0] >> 8) & 0xff) == 0 && bp[(size_t)9 * 20 + 19] == bp[0],
              "bucket: 连通区域全部落色");
        check(bp[(size_t)5 * 20 + 5] == 0xff000000u, "bucket: 容差外的像素不变");
        check(be.bucketFill(0, 0, 30, Editor::BucketFill::Pattern), "bucket: 图案填充");
        bp = be.doc.layers[0].img.pixels();
        check(bp[0] == 0xffffffffu && bp[8] == 0xff0000ffu && bp[0] != bp[8],
              "bucket: 图案按 8px 棋盘交替");
    }

    {
        Editor le;
        le.newDocument(10, 10, Color(255, 255, 255, 255));
        le.addEmptyLayer();
        le.palette.fg[0] = 1.0f;
        le.palette.fg[1] = 0.0f;
        le.palette.fg[2] = 0.0f;
        le.palette.fg[3] = 1.0f;
        check(le.bucketFill(0, 0, 30, Editor::BucketFill::Foreground), "bucket: 透明上层可填充");
        const Layer& top = le.doc.layers[1];
        const Layer& bottom = le.doc.layers[0];
        check((top.img.pixels()[0] & 0xff) == 255 && (top.img.pixels()[0] >> 24) == 255, "bucket: 落色到当前层");
        check(bottom.img.pixels()[0] == 0xffffffffu, "bucket: 不穿透到底层");
    }

    {
        Image tr;
        tr.reset(8, 8);
        clipboard::setImage(tr);
        Image rb;
        check(clipboard::getImage(rb) && rb.width() == 8 && (rb.pixels()[0] >> 24) == 0,
              "clipboard: 全透明往返保持透明");
    }

    {
        Document d;
        d.createEmpty(16, 12, Color(0, 0, 0, 0));
        d.layers[0].img.fillAll(Color(255, 0, 0, 255).packed());
        d.addEmptyLayer("上层");
        Layer& top = d.layers[1];
        top.img.fillAll(Color(0, 0, 255, 255).packed());
        top.opacity = 0.5f;
        top.blend = BlendMode::Screen;

        const std::string p = "selftest_tmp.ora";
        check(ora::save(p, d), "ora: 保存成功");
        Document r;
        check(ora::load(p, r), "ora: 读取成功");
        check(r.width == 16 && r.height == 12 && r.layers.size() == 2, "ora: 画布与层数还原");
        check(r.layers[0].img.pixels()[0] == 0xff0000ffu, "ora: 底层像素还原");
        check(r.layers[1].blend == BlendMode::Screen && std::abs(r.layers[1].opacity - 0.5f) < 0.01f,
              "ora: 混合模式/不透明度还原");
        std::remove(p.c_str());

        {
            Document md;
            md.createEmpty(32, 24, Color(0, 0, 0, 0));
            md.layers[0].img.fillAll(Color(255, 255, 255, 255).packed());
            Layer& ml = md.layers[0];
            ml.uMask.assign((size_t)32 * 24, 255u);
            ml.uMaskW = 32;
            ml.uMaskH = 24;
            for (int y = 0; y < 12; ++y)
                for (int x = 0; x < 16; ++x) ml.uMask[(size_t)y * 32 + (size_t)x] = 0;
            ml.uMask[(size_t)20 * 32 + 20] = 128;
            const std::string mp = "selftest_mask.ora";
            check(ora::save(mp, md), "ora-mask: 带蒙版保存");
            Document mr;
            check(ora::load(mp, mr), "ora-mask: 读回");
            check(!mr.layers[0].uMask.empty() && mr.layers[0].uMaskW == 32 && mr.layers[0].uMaskH == 24,
                  "ora-mask: 蒙版尺寸恢复");
            check(mr.layers[0].uMask[0] == 0 && mr.layers[0].uMask[(size_t)23 * 32 + 31] == 255,
                  "ora-mask: 蒙版逐像素还原（内外各验一点）");
            check(mr.layers[0].uMask[(size_t)20 * 32 + 20] == 128,
                  "ora-mask: 半透明笔触的灰阶不被压成黑白");
            check(mr.layers[0].img.width() == 32 && mr.layers[0].img.height() == 24,
                  "ora-mask: 图层像素本身没被蒙版图顶替");
            std::remove(mp.c_str());

            Document nm;
            nm.createEmpty(8, 8, Color(0, 0, 0, 0));
            const std::string np = "selftest_nomask.ora";
            check(ora::save(np, nm) && ora::load(np, nm), "ora-mask: 无蒙版文档往返");
            check(nm.layers[0].uMask.empty(), "ora-mask: 无蒙版读回为空（不凭空造蒙版）");
            std::remove(np.c_str());
        }

        {
            Document sd;
            sd.createEmpty(32, 24, Color(0, 0, 0, 0));
            sd.layers[0].img.fillAll(Color(0, 255, 0, 255).packed());
            sd.addEmptyLayer("第二层");
            sd.layers[1].img.fillAll(Color(0, 0, 255, 128).packed());
            sd.layers[1].opacity = 0.5f;
            const std::string sp = "selftest_place.ora";
            check(ora::save(sp, sd), "ora-place: 造样本成功");

            Editor oe;
            oe.doc.createEmpty(200, 150, Color(0, 0, 0, 0));
            oe.sel.reset(oe.doc.width, oe.doc.height);
            const int before = (int)oe.doc.layers.size();
            const bool ok = oe.placeOraAsLayers(sp);
            check(ok && (int)oe.doc.layers.size() == before + 2, "ora-place: ORA 的每一层都成为当前文档的一层");
            const Layer& n0 = oe.doc.layers[(size_t)before];
            const Layer& n1 = oe.doc.layers[(size_t)before + 1];
            check(n0.img.width() == 32 && n0.img.height() == 24, "ora-place: 层像素尺寸原样");
            check(n0.x == (oe.doc.width - 32) / 2 && n1.x == n0.x && n1.y == n0.y,
                  "ora-place: 整份按画布居中，层间相对位置不变");
            check(n1.opacity > 0.49f && n1.opacity < 0.51f, "ora-place: 层不透明度跟着搬过来");
            oe.undo();
            check((int)oe.doc.layers.size() == before, "ora-place: 一步撤销收回整份 ORA");

            Editor oe2;
            oe2.doc.createEmpty(100, 80, Color(0, 0, 0, 0));
            const std::string bad = "selftest_notora.png";
            const int beforeBad = (int)oe2.doc.layers.size();
            check(!oe2.placeOraAsLayers(bad), "ora-place: 读不了的文件返回失败而不是静默建空层");
            check((int)oe2.doc.layers.size() == beforeBad,
                  "ora-place: 读不了的文件返回失败，且不静默建空层");
            std::remove(sp.c_str());
        }

        Document big;
        big.createEmpty(1600, 1000, Color(0, 0, 0, 0));
        big.layers[0].img.fillAll(Color(255, 255, 255, 255).packed());
        big.addEmptyLayer("画面");
        big.layers[1].img.fillAll(Color(0, 128, 255, 200).packed());
        big.addEmptyLayer("顶层");
        big.layers[2].img.fillAll(Color(10, 10, 10, 120).packed());
        const std::string bp = "selftest_big.ora";
        check(ora::save(bp, big), "ora: 1600x1000 三层保存");
        Document br;
        check(ora::load(bp, br) && br.layers.size() == 3 && br.width == 1600, "ora: 大文档读回");
        std::remove(bp.c_str());

        Document td;
        td.createEmpty(400, 200, Color(255, 255, 255, 255));
        TextData t;
        t.text = "文本层 A";
        t.x = 20.0f;
        t.y = 20.0f;
        t.size = 40;
        t.color = Color(0, 0, 0, 255);
        td.addTextLayer(t, "文字");
        const std::string tp = "selftest_text.ora";
        check(ora::save(tp, td), "ora: 含文本层保存");
        Document tr;
        check(ora::load(tp, tr) && tr.layers.size() == 2, "ora: 文本层栅格化还原");
        std::remove(tp.c_str());

        {
            Document xd;
            xd.createEmpty(160, 120, Color(255, 255, 255, 255));
            TextData xt;
            xt.text = "变形字";
            xt.size = 36;
            xt.x = 24.0f;
            xt.y = 40.0f;
            xd.addTextLayer(xt, "变形文字");
            SkMatrix want = SkMatrix::RotateDeg(28.0f, SkPoint::Make(60.0f, 50.0f));
            want.postScale(1.4f, 0.8f);
            xd.layers[1].textXf = want;
            xd.refreshTextLayers();
            const std::string xp = "selftest_textxf.ora";
            check(ora::save(xp, xd), "ora-xf: 含变形文字保存");
            Document xr;
            check(ora::load(xp, xr) && xr.layers.size() == 2, "ora-xf: 读回");
            std::remove(xp.c_str());
            const SkMatrix got = xr.layers[1].textXf;
            bool same = true;
            for (int i = 0; i < 9; ++i) {
                SkScalar a[9], b[9];
                want.get9(a);
                got.get9(b);
                if (std::fabs((double)a[i] - (double)b[i]) > 1e-4) same = false;
            }
            check(same, "ora-xf: 文字变形矩阵原样还原（9 个数逐个比对）");
            const SkRect b1 = xd.layerDrawBounds(1);
            const SkRect b2 = xr.layerDrawBounds(1);
            check(std::fabs((double)b1.left() - (double)b2.left()) < 0.01 &&
                  std::fabs((double)b1.top() - (double)b2.top()) < 0.01 &&
                  std::fabs((double)b1.right() - (double)b2.right()) < 0.01 &&
                  std::fabs((double)b1.bottom() - (double)b2.bottom()) < 0.01,
                  "ora-xf: 读回后字形外框与保存前一致（不是只在 XML 里躺着）");
        }

        {
            Document sd;
            sd.createEmpty(200, 120, Color(255, 255, 255, 255));
            TextData st;
            st.text = "描边投影";
            st.size = 40;
            sd.addTextLayer(st, "样式文字");
            LayerStyle& sl = sd.layers[1].style;
            sl.strokeOn = true;
            sl.strokeWidth = 5.5f;
            sl.strokePos = StrokePos::Center;
            sl.strokeOpacity = 80.0f;
            sl.strokeColor[0] = 0.0f; sl.strokeColor[1] = 0.0f; sl.strokeColor[2] = 1.0f; sl.strokeColor[3] = 1.0f;
            sl.shadowOn = true;
            sl.shadowDist = 9.0f;
            sl.shadowSize = 6.0f;
            sl.shadowAngle = 45.0f;
            sl.shadowOpacity = 70.0f;
            sl.shadowColor[0] = 0.5f; sl.shadowColor[1] = 0.0f; sl.shadowColor[2] = 0.0f;
            sl.overlayOn = true;
            sl.overlayOpacity = 60.0f;
            sl.overlayColor[0] = 0.0f; sl.overlayColor[1] = 0.8f; sl.overlayColor[2] = 0.2f;
            sd.layers[1].lockAlpha = true;
            sd.layers[0].style.strokeOn = true;
            sd.layers[0].style.strokeWidth = 2.0f;
            const std::string sp = "selftest_style.ora";
            check(ora::save(sp, sd), "ora-style: 含样式保存");
            Document sr;
            check(ora::load(sp, sr), "ora-style: 读回");
            const LayerStyle& b0 = sr.layers[0].style;
            const LayerStyle& b1 = sr.layers[1].style;
            check(b0.strokeOn && std::abs(b0.strokeWidth - 2.0f) < 0.01f, "ora-style: 像素层描边还原");
            check(b1.strokeOn && std::abs(b1.strokeWidth - 5.5f) < 0.01f && b1.strokePos == StrokePos::Center &&
                      std::abs(b1.strokeOpacity - 80.0f) < 0.01f && b1.strokeColor[2] > 0.99f,
                  "ora-style: 描边开关/宽度/位置/不透明度/颜色还原");
            check(b1.shadowOn && std::abs(b1.shadowDist - 9.0f) < 0.01f && std::abs(b1.shadowSize - 6.0f) < 0.01f &&
                      std::abs(b1.shadowAngle - 45.0f) < 0.01f && std::abs(b1.shadowOpacity - 70.0f) < 0.01f &&
                      b1.shadowColor[0] > 0.49f && b1.shadowColor[2] < 0.01f,
                  "ora-style: 投影的位移/模糊/角度/不透明度/颜色还原");
            check(b1.overlayOn && std::abs(b1.overlayOpacity - 60.0f) < 0.01f && b1.overlayColor[1] > 0.79f &&
                      b1.overlayColor[2] < 0.21f,
                  "ora-style: 颜色叠加的开关/不透明度/颜色还原");
            check(sr.layers[1].lockAlpha, "ora-style: 锁定透明像素还原");
            check(sr.layers[1].kind == LayerKind::Text, "ora-style: 带样式的文字层仍是文字层");
            std::remove(sp.c_str());

            Document plain;
            plain.createEmpty(200, 120, Color(255, 255, 255, 255));
            TextData pt;
            pt.text = "描边投影";
            pt.size = 40;
            plain.addTextLayer(pt, "样式文字");
            const std::string pp = "selftest_style_plain.ora";
            check(ora::save(pp, plain), "ora-style: 无样式对照保存");
            Document pr;
            check(ora::load(pp, pr), "ora-style: 无样式对照读回");
            std::remove(pp.c_str());
            Document styledR;
            check(ora::load("selftest_style_gone.ora", styledR) == false, "ora-style: 已删除的文件读不到（对照不串）");
            const Image a = sr.flatten();
            const Image b = pr.flatten();
            bool differ = a.width() == b.width() && a.height() == b.height();
            int diff = 0;
            if (differ) {
                for (size_t i = 0; i < (size_t)a.width() * a.height(); ++i) {
                    if (a.pixels()[i] != b.pixels()[i]) ++diff;
                }
            }
            check(differ && diff > 0, "ora-style: 描边/投影确实改变了合成结果（读回后效果真的被重算）");
        }

        {
            Document wd;
            wd.createEmpty(300, 160, Color(255, 255, 255, 255));
            TextData text;
            text.text = "第一行 \"引号\" & <标签>\n第二行";
            text.x = 12.5f;
            text.y = 30.0f;
            text.size = 36;
            text.lineSpacing = 9.0f;
            text.color = Color(10, 120, 200, 200);
            text.opacity = 0.75f;
            wd.addTextLayer(text, "标题");
            const std::string wp = "selftest_textedit.ora";
            check(ora::save(wp, wd), "ora-text: 含文字层保存");
            Document rd;
            check(ora::load(wp, rd), "ora-text: 读回");
            check(rd.layers.size() == 2 && rd.layers[1].kind == LayerKind::Text,
                  "ora-text: 文字层读回来仍是文字层（不是像素层）");
            const TextData& back = rd.layers[1].text;
            check(back.text == text.text, "ora-text: 文字内容含换行/引号/实体字符原样还原");
            check(std::abs(back.x - text.x) < 0.01f && std::abs(back.y - text.y) < 0.01f &&
                      back.size == text.size && std::abs(back.lineSpacing - text.lineSpacing) < 0.01f,
                  "ora-text: 位置/字号/行距还原");
            check(back.color.r == 10 && back.color.g == 120 && back.color.b == 200 && back.color.a == 200 &&
                      std::abs(back.opacity - 0.75f) < 0.01f,
                  "ora-text: 颜色与不透明度还原");
            check(!rd.layers[1].img.empty(), "ora-text: 文字层的渲染像素同时存在（别的软件能看见）");

            Editor oex;
            oex.doc.createEmpty(400, 300, Color(255, 255, 255, 255));
            check(oex.placeOraAsLayers(wp), "ora-text: 置入含文字层的 ORA");
            const int last = (int)oex.doc.layers.size() - 1;
            check(oex.doc.layers[(size_t)last].kind == LayerKind::Text,
                  "ora-text: 置入后仍是文字层（没退化成像素层）");
            check(oex.doc.layers[(size_t)last].text.text == text.text &&
                      std::abs(oex.doc.layers[(size_t)last].text.x - (text.x + (400 - 300) / 2)) < 0.01f,
                  "ora-text: 置入后文字内容与居中平移都正确");
            std::remove(wp.c_str());
        }
    }


    {
        Editor se;
        se.newDocument(16, 16, Color(0, 0, 0, 0));
        Layer& l = se.doc.layers[0];
        l.img.fillAll(0xff00ff00u);

        l.style.overlayOn = true;
        l.style.overlayColor[0] = 0.0f;
        l.style.overlayColor[1] = 0.0f;
        l.style.overlayColor[2] = 1.0f;
        l.style.overlayColor[3] = 1.0f;
        int ox = 0, oy = 0;
        const Image& ov = styledPixels(l, ox, oy);
        const uint32_t oc = ov.pixels()[(size_t)(8 - oy) * (size_t)ov.width() + (size_t)(8 - ox)];
        check(((oc >> 16) & 0xff) > 200 && (oc & 0xff) < 60 && (oc >> 24) == 255, "style: 颜色叠加换 RGB 保留 alpha");

        l.style.overlayOn = false;
        l.style.strokeOn = true;
        l.style.strokeWidth = 2.0f;
        l.style.strokeColor[0] = l.style.strokeColor[1] = l.style.strokeColor[2] = 0.0f;
        l.style.strokeColor[3] = 1.0f;

        const Image& st = styledPixels(l, ox, oy);
        check(ox < 0 && st.width() > 16, "style: 描边向外扩");
        const uint32_t sc = st.pixels()[(size_t)8 * (size_t)st.width() + 1];
        check((sc >> 24) > 200 && (sc & 0xff) < 40, "style: 描边像素生效");

        l.style.strokePos = StrokePos::Inside;
        const Image& in = styledPixels(l, ox, oy);
        check(ox == 0 && in.width() == 16, "style: 内部描边不外扩");
        const uint32_t ic = in.pixels()[(size_t)8 * (size_t)in.width()];
        check((ic >> 24) == 255 && (ic & 0xff) < 40, "style: 内部描边压在原图上");
        auto at = [&](int x, int y) { return in.pixels()[(size_t)y * (size_t)in.width() + (size_t)x]; };
        auto isBlack = [](uint32_t c) {
            return (c >> 24) == 255 && ((c >> 16) & 0xff) < 40 && ((c >> 8) & 0xff) < 40 && (c & 0xff) < 40;
        };
        check(isBlack(at(0, 8)) && isBlack(at(15, 8)) && isBlack(at(8, 0)) && isBlack(at(8, 15)),
              "style: 内部描边四边生效");
        const uint32_t mid = at(8, 8);
        check(((mid >> 16) & 0xff) < 40 && ((mid >> 8) & 0xff) > 200 && (mid & 0xff) < 40,
              "style: 内部描边不吞中心像素");

        l.style.strokePos = StrokePos::Outside;
        l.style.strokeOpacity = 50.0f;
        const Image& half = styledPixels(l, ox, oy);
        const uint32_t hc = half.pixels()[(size_t)8 * (size_t)half.width() + 1];
        check((hc >> 24) > 100 && (hc >> 24) < 160, "style: 描边透明 50%");

        const uint32_t before50 = half.pixels()[(size_t)8 * (size_t)half.width() + 1];
        l.style.strokeOpacity = 100.0f;
        const Image& full = styledPixels(l, ox, oy);
        const uint32_t fullPx = full.pixels()[(size_t)8 * (size_t)full.width() + 1];
        check(fullPx != before50 && (fullPx >> 24) > 200, "style: 改不透明度后缓存失效并重算");
        l.style.strokeOpacity = 50.0f;
        const Image& again = styledPixels(l, ox, oy);
        check(again.pixels()[(size_t)8 * (size_t)again.width() + 1] == before50, "style: 参数改回后结果等价");
    }

    {
        Prop probe;
        check(probe.setV == nullptr && probe.setColor == nullptr, "V1: 未注册意图口的 Prop 默认不可写");
        struct Owner { float v = 1.0f; int n = 2; bool b = false; int sel = 0; float col[4] = {0, 0, 0, 1}; };
        Owner ow;
        Props ps;
        ps.sliderF("f", &ow.v, 0.0f, 10.0f, [](double x, void* ud) { static_cast<Owner*>(ud)->v = (float)x; }, &ow);
        ps.sliderI("i", &ow.n, 0, 100, [](double x, void* ud) { static_cast<Owner*>(ud)->n = (int)x; }, &ow);
        ps.check("b", &ow.b, [](double x, void* ud) { static_cast<Owner*>(ud)->b = x != 0.0; }, &ow);
        static const char* const kItems[2] = {"a", "b"};
        ps.combo("c", &ow.sel, kItems, 2, [](double x, void* ud) { static_cast<Owner*>(ud)->sel = (int)x; }, &ow);
        ps.color("k", ow.col, [](const float* c, void* ud) {
            float* d = static_cast<Owner*>(ud)->col;
            for (int k = 0; k < 4; ++k) d[k] = c[k];
        }, &ow);
        check(ps.size() == 5 && ps[0].setV && ps[1].setV && ps[2].setV && ps[3].setV && ps[4].setColor,
              "V1: 值控件都注册了意图口（setV/setColor）");
        ps[0].setV(7.5, ps[0].setUd); ps[1].setV(42, ps[1].setUd); ps[2].setV(1, ps[2].setUd);
        ps[3].setV(1, ps[3].setUd);
        const float nc[4] = {0.25f, 0.5f, 0.75f, 1.0f};
        ps[4].setColor(nc, ps[4].setUd);
        check(ow.v == 7.5f && ow.n == 42 && ow.b && ow.sel == 1 && ow.col[1] == 0.5f && *ps[0].f == 7.5f,
              "V1: 意图口写=所有者字段更新，显示别名同步可见");
        bool untouched = (ow.v == 7.5f);
        Prop readonlyNoSetter;
        readonlyNoSetter.kind = PropKind::Float;
        readonlyNoSetter.f = &ow.v;
        readonlyNoSetter.setV = nullptr;
        check(readonlyNoSetter.setV == nullptr && untouched, "V1: 无意图口的条目无法被视图写（拒绝路径存在）");
    }

    {
        struct Owner { double v = 50.0; };
        Owner ow;
        auto getFn = [](const void* ud) { return static_cast<const Owner*>(ud)->v; };
        auto setFn = [](void* ud, double v) { static_cast<Owner*>(ud)->v = v; };
        NumField f;
        f.get = +getFn;
        f.set = +setFn;
        f.readUd = &ow;
        f.writeUd = &ow;
        f.lo = 0.0;
        f.hi = 100.0;
        f.step = 1.0;
        const Rect box{0, 0, 168, 22};
        PointerEvent pe;
        pe.pos = Vec2f{60.0f, 11.0f};
        pe.pressed = true;
        check(f.pointer(pe, box) && f.drag && !f.editing, "NumField: 按下先记起点，不立刻进输入");
        pe.pressed = false;
        pe.down = true;
        pe.pos = Vec2f{62.0f, 11.0f};
        f.pointer(pe, box);
        check(!f.dragged && ow.v == 50.0, "NumField: 3px 死区内不动值");
        pe.pos = Vec2f{70.0f, 11.0f};
        f.pointer(pe, box);
        check(f.dragged && ow.v != 50.0, "NumField: 超过死区后按位移改值");
        pe.pos = Vec2f{-20.0f, 40.0f};
        check(f.pointer(pe, box) && ow.v == 0.0, "NumField: 拖出框外仍继续且夹到下限");
        pe.down = false;
        pe.released = true;
        f.pointer(pe, box);
        check(!f.editing && !f.drag, "NumField: 拖过之后松手不进输入态");
        pe.pos = Vec2f{60.0f, 11.0f};
        pe.pressed = true;
        pe.down = pe.released = false;
        f.pointer(pe, box);
        pe.pressed = false;
        pe.released = true;
        f.pointer(pe, box);
        check(!f.editing && std::abs(ow.v - 36.0) < 0.51,
              "NumField: 单击把值调到点击的位置，仍不是输入态");
        pe.pos = Vec2f{60.0f, 11.0f};
        pe.pressed = true;
        pe.doubleClick = true;
        pe.down = pe.released = false;
        check(f.pointer(pe, box) && f.editing, "NumField: 双击进入输入态（滑块与输入框是同一控件）");
        pe.doubleClick = false;
        f.buf = "120";
        KeyEvent ke;
        ke.scancode = KeyEnter;
        f.key(ke);
        check(!f.editing && ow.v == 100.0, "NumField: 回车提交并按上界收敛");
        f.beginEdit();
        f.buf = "999";
        ke.scancode = KeyEsc;
        f.key(ke);
        check(!f.editing && ow.v == 100.0, "NumField: Esc 取消不写");
        f.beginEdit();
        f.buf = "37";
        pe.pos = Vec2f{-5.0f, -5.0f};
        pe.pressed = true;
        pe.down = pe.released = false;
        check(!f.pointer(pe, box) && !f.editing && ow.v == 100.0,
              "NumField: 输入态点别处作废，值保持原样并退回进度条显示");

        {
            ie::NumField a, b;
            ie::NumSession s;
            s.take(0, &a);
            a.beginEdit();
            s.take(1, &b);
            b.beginEdit();
            check(!a.editing && b.editing,
                  "NumSession: 切到下一行时上一行的输入态已被收回（同一时刻只有一行在编辑）");
            check(s.cancelUnless(0.0f, 0.0f, ie::Rect{10, 10, 60, 20}),
                  "NumSession: 按下落在框外 → 作废这次输入");
            check(!b.editing && !s.editing(), "NumSession: 作废后没有输入态");
            check(!s.cancelUnless(20.0f, 15.0f, ie::Rect{10, 10, 60, 20}),
                  "NumSession: 没有输入态时框内按下不作废任何东西（交给命中）");
        }


        {
            ie::Host dh;
            const int top = 26, bottom = 836, tabH = 26;
            const ie::Host::PanelLayout ly = dh.computePanelRects(1360, top, bottom, tabH);
            check(ly.tools.x == 0 && ly.tools.y == top && ly.tools.h == bottom - top &&
                      ly.tools.w == ie::ui::dims::railW,
                  "面板: 第一面板贴左边、宽 56、顶到菜单底底到状态栏");
            check(ly.options.empty(),
                  "面板: 浮条位置由 arrange 计算，computePanelRects 不再给出第二面板列");
            {
                const ie::Rect railFrame{ly.tools.x, ly.tools.y, ly.tools.w, ly.tools.h};
                const ie::ui::RailGrid rg = ie::ui::railGrid(railFrame, dh.toolsCount());
                check(rg.cols == 1 && rg.rows == dh.toolsCount(), "工具面板: 图标只排一列（从上到下）");
                check(rg.area.x == railFrame.x + (railFrame.w - rg.cell) / 2 && rg.area.y == railFrame.y + 6,
                      "工具面板: 单列居中、顶部留 6px");
                const ie::Rect swb = ie::ui::railSwatch(railFrame);
                ie::Rect fg{}, bg{}, sw{};
                ie::ui::swatchCellsIn(swb, fg, bg, sw);
                check(fg.y < bg.y && bg.y < sw.y && fg.x == bg.x && sw.w == fg.w,
                      "色板: 前景在上、背景在中、切换在下（竖排三格）");
                check(swb.bottom() + 10 == railFrame.bottom(), "色板: 底距面板底 10px");
            }
            check(ly.image.w == dh.settings.colR && ly.image.y == top && ly.layers.bottom() == bottom,
                  "面板: 右列上「图像」下「图层」");
            check(ly.tabbar.x == ie::ui::dims::railW &&
                      ly.tabbar.w == 1360 - ie::ui::dims::railW - dh.settings.colR - ie::Host::kSplitT &&
                      ly.tabbar.h == tabH && ly.tabbar.y == top,
                  "面板: 标签栏从左面板右侧起、右让出右栏竖条");
            check(ly.canvas.x == ie::ui::dims::railW && ly.canvas.y == top + tabH &&
                      ly.canvas.right() == 1360 - dh.settings.colR - ie::Host::kSplitT,
                  "面板: 画布左贴第一面板、从标签栏下方开始，右边贴竖分隔条");
            check(ly.splitRH.y == ly.image.bottom(), "面板: 右列只有一条横分隔条");
            {
                const int span = bottom - top - ie::Host::kSplitT;
                check(ly.image.h == (int)std::lround((float)span * 2.0f / 6.0f) ||
                          std::abs(ly.image.h - span * 2 / 6) <= 1,
                      "面板: 右侧上「图像」默认占右列 2/6");
                check(ly.layers.h == span - ly.image.h, "面板: 右侧下「图层」拿剩下的 4/6");
            }
            const int cw = ly.canvas.w, ch = ly.canvas.h;
            check(cw > 0 && ch == bottom - top - tabH &&
                      ly.image.h + ly.layers.h + ie::Host::kSplitT == bottom - top,
                  "面板: 右列从菜单顶到底铺满，画布扣掉标签栏高度");
            const ie::Rect all[3] = {ly.tools, ly.image, ly.layers};
            bool clash = false;
            for (int a = 0; a < 3; ++a)
                for (int b = a + 1; b < 3; ++b) {
                    const ie::Rect h = all[a].intersect(all[b]);
                    if (h.w > 0 && h.h > 0) clash = true;
                }
            check(!clash, "面板: 三个固定面板互不重叠");
            {
                const ie::Rect dock = dh.optionsRect(ly.canvas, 240, 34);
                check(dock.x == ly.canvas.x + 8 && dock.y == ly.canvas.y + 8 &&
                          dock.w == 240 && dock.h == 34,
                      "浮条: 默认钉在画布左上角内侧、留 8px 边距");
                dh.settings.optX = 500;
                dh.settings.optY = 400;
                const ie::Rect fl = dh.optionsRect(ly.canvas, 240, 34);
                check(fl.x == 500 && fl.y == 400, "浮条: 拖过的位置被记住并原样复用");
            }
            dh.settings.colR = 60;
            const ie::Host::PanelLayout cl = dh.computePanelRects(1360, top, bottom, 0);
            check(cl.tools.w == ie::ui::dims::railW && cl.image.w == 180 &&
                      cl.canvas.x == ie::ui::dims::railW,
                  "面板: 左列只有第一面板 56；右列最小 180");
            dh.panelRects_ = cl;
            check(dh.splitHit((float)cl.splitRH.x + 40.0f, (float)cl.splitRH.y + 3.0f) == 2 &&
                      dh.splitHit((float)cl.splitRW.x + 3.0f, (float)top + 40.0f) == 1 &&
                      dh.splitHit((float)cl.tools.right() - 1.0f, (float)top + 40.0f) < 0,
                  "面板: 只剩右栏竖条与右列横条可抓，左列竖条已移除");
        {
            using namespace ie::ui;
            const ie::Rect tall{0, 0, 56, 810};
            const RailGrid g0 = railGrid(tall, 14);
            const ie::Rect sw0 = railSwatch(tall);
            check(g0.cols == 1 && g0.rows == 14, "工具面板: 56 宽只排一列");
            check(g0.area.bottom() <= sw0.y - 4, "工具面板: 图标单列止步于色板之上");
            const ie::Rect low{0, 0, 56, 300};
            const RailGrid g1 = railGrid(low, 14);
            check(g1.cell < g0.cell, "工具面板: 高度不够时格子等比缩小（不会加第二列）");
            check(g1.area.bottom() <= railSwatch(low).y - 4, "工具面板: 矮面板里图标仍在色板之上");
            const ie::Rect sw1 = railSwatch(low);
            check(sw1.x + sw1.w / 2 == low.right() / 2, "工具面板: 色板水平居中");
            check(sw1.bottom() == low.bottom() - 10, "工具面板: 色板贴底 10px");
            ie::Rect fg1{}, bg1{}, sw1b{};
            swatchCellsIn(sw1, fg1, bg1, sw1b);
            check(sw1.contains(fg1.x, fg1.y) && sw1.contains(bg1.x, bg1.bottom() - 1),
                  "工具面板: 前景/背景方块都收在色板内");
            check(fg1.y < bg1.y && bg1.y < sw1b.y, "色板: 竖排 前景→背景→切换");
        }
        }

        {
            ie::Editor e;
            e.newDocument(100, 100, ie::Color(0, 0, 0, 0));
            e.view.vpX = 0;
            e.view.vpY = 0;
            e.view.vpW = 400;
            e.view.vpH = 400;
            e.view.zoom = 2.0f;
            ie::CanvasNode node;
            node.setEditor(&e);
            node.place(ie::Rect{0, 0, 400, 400}, ie::Rect{0, 0, 400, 400});
            node.syncDocument();
            std::vector<ie::Rect> rects;
            node.takeDirty(rects);
            e.doc.layers[0].img.fillRect(ie::Rect{10, 10, 20, 20}, 0xFFAA3300u);
            e.doc.markDirty(ie::Rect{10, 10, 20, 20}, 0);
            node.syncDocument();
            rects.clear();
            node.takeDirty(rects);
            int area = 0;
            for (const ie::Rect& r : rects) area += r.w * r.h;
            check(!rects.empty() && area < 6000, "脏区: 20×20 笔触只让画布节点脏一小块，不是整块视图");
            e.doc.touchStructure();
            node.syncDocument();
            rects.clear();
            node.takeDirty(rects);
            int area2 = 0;
            for (const ie::Rect& r : rects) area2 += r.w * r.h;
            check(area2 > 100000, "脏区: 结构变化仍然整块重画（给安全网留口子）");
        }

        {
            ie::CanvasViewState v;
            v.vpX = 0;
            v.vpY = 0;
            v.vpW = 400;
            v.vpH = 400;
            v.zoom = 2.0f;
            const ie::Rect small = v.rectToScreen(ie::Rect{10, 10, 20, 20}, 100, 100);
            const ie::Vec2f a = v.toScreen(10.0f, 10.0f, 100, 100);
            const ie::Vec2f b = v.toScreen(30.0f, 30.0f, 100, 100);
            check(small.contains((int)a.x, (int)a.y) && small.contains((int)b.x, (int)b.y),
                  "脏区映射: 文档小矩形换算后包住它的两个角");
            check(small.w < 50 && small.h < 50, "脏区映射: 20×20 的笔触只脏 ~40×40，不是整块画布");
            const ie::Rect all = v.rectToScreen(ie::Rect{0, 0, 100, 100}, 100, 100);
            check(all.w >= 200 && all.h >= 200, "脏区映射: 整幅文档仍然映射成整幅视图");
        }

        {
            ie::CanvasViewState v;
            v.vpX = 0;
            v.vpY = 0;
            v.vpW = 200;
            v.vpH = 200;
            v.zoom = 1.0f;
            v.rotationDeg = 45.0f;
            const ie::Rect r = v.rectToScreen(ie::Rect{40, 40, 20, 20}, 100, 100);
            const ie::Vec2f c = v.toScreen(60.0f, 60.0f, 100, 100);
            check(r.contains((int)c.x, (int)c.y) && r.w > 28,
                  "脏区映射: 旋转后取包围盒，仍包住旋转后的角");
        }

        {
            ie::Image a(64, 64);
            a.fillAll(0xFF00FF00u);
            ie::Image b = a;
            check(b.sharesWith(a), "写时复制: 赋值只共享缓冲");
            const size_t c0 = ie::Image::detachCopies();
            const ie::Image& rb = b;
            const uint32_t* ro = rb.pixels();
            (void)ro;
            check(ie::Image::detachCopies() == c0 && b.sharesWith(a),
                  "写时复制: 只读走 const 取值不复制、不脱共享");
            b.pixels()[0] = 0xFF0000FFu;
            check(ie::Image::detachCopies() == c0 + 1 && !b.sharesWith(a),
                  "写时复制: 真的要写才复制一次");
            check(a.pixels()[0] == 0xFF00FF00u, "写时复制: 复制后原图不被写穿");
        }

        {
            ie::Editor e;
            e.newDocument(128, 128, ie::Color(0, 0, 0, 0));
            e.doc.addEmptyLayer("A");
            const int li = e.doc.active;
            e.doc.layers[(size_t)li].img.fillRect(ie::Rect{8, 8, 40, 40}, 0xFF3366CCu);
            const ie::Image snapshot = e.doc.layers[(size_t)li].img;
            const size_t c0 = ie::Image::detachCopies();
            const ie::Image flat = e.doc.flatten();
            check(ie::Image::detachCopies() == c0, "写时复制: 合成整图只读，不复制任何图层");
            check(!flat.empty() && e.doc.layers[(size_t)li].img.sharesWith(snapshot),
                  "写时复制: 合成后图层缓冲仍与撤销快照共享");
            const ie::Rect cb = e.doc.layerContentBounds(li);
            check(ie::Image::detachCopies() == c0 && !cb.empty(),
                  "写时复制: 取内容框只读，不复制图层");
        }

        {
            ie::Editor e;
            e.newDocument(64, 64, ie::Color(0, 0, 0, 0));
            e.doc.addEmptyLayer("上");
            e.doc.layers[0].img.fillAll(0xFF0000FFu);
            e.doc.layers[1].img.fillRect(ie::Rect{0, 0, 32, 32}, 0xFF00FF00u);
            const ie::Image snap0 = e.doc.layers[0].img;
            const ie::Image snap1 = e.doc.layers[1].img;
            const size_t c0 = ie::Image::detachCopies();
            e.doc.mergeDown(1);
            check(ie::Image::detachCopies() == c0, "写时复制: 向下合并不复制上下层缓冲（只读读入、结果换新）");
            check(e.doc.layers.size() == 1 && !e.doc.layers[0].img.sharesWith(snap0) &&
                      !e.doc.layers[0].img.sharesWith(snap1),
                  "写时复制: 合并结果是新缓冲，不写穿撤销快照");
        }

        {
            ie::Document d;
            d.createEmpty(16, 16, ie::Color(0, 0, 0, 255));
            d.addEmptyLayer("B");
            d.addEmptyLayer("C");
            d.toggleSelect(0);
            d.removeLayer(d.active);
            check(d.active >= 0 && d.active < (int)d.layers.size(),
                  "层状态: 删掉当前层后 active 仍指向真实存在的层");
            bool pruned = true;
            for (int id : d.selIds)
                if (d.indexOfId(id) < 0) pruned = false;
            check(pruned, "层状态: 多选集合里不再残留已删除层的 id");
            d.moveLayer(0, (int)d.layers.size() - 1);
            check(d.active >= 0 && d.active < (int)d.layers.size(), "层状态: 重排后 active 依旧合法");
        }

        {
            ie::Editor e;
            e.newDocument(64, 64, ie::Color(0, 0, 0, 0));
            check(!e.activeHasContent() && !e.activeHasPixels(), "命令闸: 空图层既无内容也不是变形目标");
            e.doc.addEmptyLayer("有内容");
            const int li = e.doc.active;
            e.doc.layers[(size_t)li].img = ie::Image(10, 6);
            e.doc.layers[(size_t)li].img.fillAll(0xFF00FF00u);
            e.doc.layers[(size_t)li].x = 0;
            e.doc.layers[(size_t)li].y = 0;
            check(e.activeHasContent() && e.activeHasPixels(), "命令闸: 有像素的层可合并/可居中/可变形");
            check(e.beginMeshWarp(li), "命令闸: 有内容的层能进网络变形");
        }

        {
            ie::Editor e;
            e.newDocument(64, 64, ie::Color(0, 0, 0, 0));
            const int empty = e.doc.active;
            const bool okFree = e.beginFreeTransform(empty);
            check(!okFree, "命令闸: 空图层进不了自由变形");
            check(e.doc.active == empty, "命令闸: 被拒绝的变形不留下半截状态");
        }

        {
            ie::Editor e;
            e.newDocument(64, 64, ie::Color(0, 0, 0, 0));
            ie::TextData t;
            t.text = "A";
            t.size = 20;
            t.color = ie::Color(0, 0, 0, 255);
            t.x = 10.0f;
            t.y = 30.0f;
            const int ti = e.addTextLayer(t);
            check(ti >= 0 && e.activeHasContent() && !e.activeHasPixels(), "命令闸: 文字层有内容、无像素，但可变形");
            check(e.beginFreeTransform(ti), "命令闸: 文字层能进自由变形");
            e.cancelFreeTransform();
            check(!e.beginMeshWarp(ti), "命令闸: 文字层进不了网络变形");
        }

        {
            ie::Editor e;
            e.newDocument(64, 64, ie::Color(0, 0, 0, 0));
            e.doc.addEmptyLayer("图");
            const int li = e.doc.active;
            e.doc.layers[(size_t)li].img = ie::Image(10, 6);
            e.doc.layers[(size_t)li].img.fillAll(0xFF0000FFu);
            e.doc.layers[(size_t)li].x = 0;
            e.doc.layers[(size_t)li].y = 0;
            e.centerActiveLayer();
            const ie::Rect c = e.doc.layerContentBounds(li);
            check(c.x == 27 && c.y == 29,
                  "图层居中: 内容框中心落在画布中心（64x64 里 10x6 → 27,29）");
            const int rev = e.documentRevision();
            e.centerActiveLayer();
            check(e.documentRevision() == rev && e.doc.layers[(size_t)li].x == 27,
                  "图层居中: 已在中心时不动像素、不留空历史步");
        }

        {
            ie::Editor e;
            e.newDocument(64, 64, ie::Color(0, 0, 0, 0));
            e.doc.addEmptyLayer("A");
            const int a = e.doc.active;
            e.doc.layers[(size_t)a].img = ie::Image(8, 8);
            e.doc.layers[(size_t)a].img.fillAll(0xFF0000FFu);
            e.doc.layers[(size_t)a].x = 0;
            e.doc.layers[(size_t)a].y = 0;
            e.doc.addEmptyLayer("B");
            const int b2 = e.doc.active;
            e.doc.layers[(size_t)b2].img = ie::Image(12, 4);
            e.doc.layers[(size_t)b2].img.fillAll(0xFF00FF00u);
            e.doc.layers[(size_t)b2].x = 50;
            e.doc.layers[(size_t)b2].y = 58;
            e.toggleLayerSelection(a);
            check(e.doc.selectedIndices().size() == 2, "图层居中多选: 两层都在选区里");
            const size_t undoBefore = e.hist.undoCount();
            e.centerActiveLayer();
            const ie::Rect ca = e.doc.layerContentBounds(a);
            const ie::Rect cb = e.doc.layerContentBounds(b2);
            check(ca.x == 28 && ca.y == 28, "图层居中多选: A 层内容框居中（64 里 8x8 → 28,28）");
            check(cb.x == 26 && cb.y == 30, "图层居中多选: B 层内容框同样居中（64 里 12x4 → 26,30）");
            check(e.hist.undoCount() - undoBefore == 1, "图层居中多选: 只记一条历史");
        }

        {
            const auto& pgs = ie::ui::ImagePanelNode::pages();
            check(pgs.size() == 5 && std::string(pgs[0].title) == "三色" && std::string(pgs[1].title) == "衡影",
                  "图像面板: 页签顺序 三色 → 衡影 → 描边 → 投影 → 颜叠");
        }
        {
            using namespace ie::ui;
            check(kLayerBtnCount == 3 && std::string(layerBtnLabel(0)) == "新建" &&
                      std::string(layerBtnLabel(1)) == "复制" && std::string(layerBtnLabel(2)) == "删除",
                  "图层面板: 按钮改成文字 新建/复制/删除，合并图标已移除");
            check(layerBtnOffset(1) > layerBtnOffset(0) && layerBtnOffset(2) > layerBtnOffset(1) &&
                      layerBtnTotalW() <= dims::rightW - 16,
                  "图层面板: 三个文字按钮横排，首尾都在面板内");
        }

        {
            ie::Editor e;
            e.newDocument(32, 32, ie::Color(0, 0, 0, 0));
            {
                ie::Image bg(32, 32);
                uint32_t* bp = bg.pixels();
                for (int yy = 0; yy < 32; ++yy)
                    for (int xx = 0; xx < 32; ++xx)
                        bp[(size_t)yy * 32 + (size_t)xx] = (xx < 16) ? 0xff0000ffu : 0xffff0000u;
                e.doc.addLayerAt(std::move(bg), 0, 0, "底");
                e.doc.active = 0;
            }
            e.doc.addEmptyLayer("上");
            const int top = e.doc.active;
            e.doc.layers[(size_t)top].img = ie::Image(10, 10);
            e.doc.layers[(size_t)top].img.fillAll(0xff00ff00u);
            e.doc.layers[(size_t)top].x = 2;
            e.doc.layers[(size_t)top].y = 2;
            check(e.wandSelect(20, 20, 0, ie::Editor::WandMode::Replace), "魔棒: 点得中");
            check(e.sel.contains(3, 25), "魔棒只对图层: 底层红区也进了选区（说明种子取的是当前层的透明）");
            check(!e.sel.contains(6, 6), "魔棒只对图层: 当前层里的绿块不会被选进来（颜色不同）");
            e.wandSelect(6, 6, 0, ie::Editor::WandMode::Replace);
            check(e.sel.contains(6, 6) && !e.sel.contains(20, 20),
                  "魔棒只对图层: 点绿块只选中绿块，不因下层颜色而扩散");
        }

        {
            ie::Editor e;
            check(!e.composeLines, "构图线: 默认隐藏");
            e.toggleComposeLines();
            check(e.composeLines, "构图线: 菜单开一次就显示");
            ie::OverlayRecording rec;
            ie::OverlayRecorder r(&rec);
            ie::CanvasViewState v;
            v.vpX = 0.0f;
            v.vpY = 0.0f;
            v.vpW = 96.0f;
            v.vpH = 64.0f;
            ie::guides::drawCompose(&r, v, 96, 64);
            bool third = false, twoThirds = false, horiz = false;
            for (const auto& op : rec.ops) {
                if (op.kind != ie::OvKind::Line) continue;
                if (op.x0 == op.x1 && std::fabs(op.x0 - 32.0f) < 0.01f) third = true;
                if (op.x0 == op.x1 && std::fabs(op.x0 - 64.0f) < 0.01f) twoThirds = true;
                if (op.y0 == op.y1 && std::fabs(op.y0 - 64.0f / 3.0f) < 0.01f) horiz = true;
            }
            check(third && twoThirds && horiz, "构图线: 竖线与横线都落在三等分处（96x64）");
            check(!rec.ops.empty() && rec.ops.size() > 8,
                  "构图线: 画的是蚂蚁线（虚线，不是一根实线）");
        }

        ie::ui::initFonts();
        check(ie::ui::textWidth("0") > 0, "NumField: 字体已就绪（选区的像素换算要有真实字宽）");
        f.beginEdit();
        pe.pos = Vec2f{6.0f + ie::ui::textWidth("100"), 11.0f};
        pe.pressed = true;
        pe.down = pe.released = false;
        f.pointer(pe, box);
        check(f.editing && f.sel.caret == 3 && !f.hasSel(), "NumField: 输入态按下先把光标放到指针处");
        pe.pressed = false;
        pe.down = true;
        pe.pos = Vec2f{4.0f, 11.0f};
        f.pointer(pe, box);
        check(f.hasSel() && f.selBegin() == 0 && f.selEnd() == 3,
              "NumField: 往左拖出一整段选区（0..3）");
        pe.down = false;
        pe.released = true;
        f.pointer(pe, box);
        check(f.hasSel(), "NumField: 松手后选区保留（不是一松就没）");
        KeyEvent ke2;
        ke2.scancode = 0;
        ke2.unicode = '7';
        f.key(ke2);
        check(f.buf == "7" && f.sel.caret == 1 && !f.hasSel(),
              "NumField: 有选区时键入是替换（旧字被整段换掉）");
        ke2.scancode = KeyEnter;
        ke2.unicode = 0;
        f.key(ke2);
        check(!f.editing && ow.v == 7.0, "NumField: 替换后的值照常按回车提交");

        f.beginEdit();
        check(f.editing && f.hasSel() && f.selBegin() == 0 && f.selEnd() == 1,
              "NumField: 进输入态就把整段数值选中（不用先拖选，直接打字就是替换）");
        check(f.clipText() == "7", "NumField: 复制取整段（无选区时不必先全选）");
        check(f.pasteText("42.5") && f.buf == "42.5" && !f.hasSel(),
              "NumField: 粘贴合法数字 = 替换整段选区");
        check(!f.pasteText("abc") && f.buf == "42.5", "NumField: 粘贴文字不动（原样保留）");
        check(!f.pasteText("12a") && f.buf == "42.5", "NumField: 粘贴混排文字不动");
        check(!f.pasteText("") && f.buf == "42.5", "NumField: 空剪贴板不动");
        KeyEvent ctrlA;
        ctrlA.scancode = KeyA;
        ctrlA.ctrl = true;
        f.key(ctrlA);
        check(f.hasSel() && f.selEnd() == 4, "NumField: Ctrl+A 全选");
        check(f.pasteText("  -8 ") && f.buf == "-8" && f.sel.caret == 2,
              "NumField: 带空白的负数也认，且有选区时整段替换成 -8");
        KeyEvent escK;
        escK.scancode = KeyEsc;
        f.key(escK);
        check(!f.editing && !f.hasSel() && ow.v == 7.0,
              "NumField: 粘贴后 Esc 仍作废，值保持原 7");
    }

    {
        check(ie::utf8::prev("a中b", 4) == 1 && ie::utf8::next("a中b", 0) == 1 &&
                  ie::utf8::next("a中b", 1) == 4,
              "textsel: 码点步进（3 字节的中文不切半个字）");
        check(ie::utf8::snap("a中b", 2) == 4, "textsel: 落在延续字节上的偏移吸附到码点边界");

        std::string s = "abc";
        ie::ui::TextSel sel;
        ie::KeyEvent ctrlA;
        ctrlA.scancode = KeyA;
        ctrlA.ctrl = true;
        check(ie::ui::textSelKey(ctrlA, sel, s, ie::ui::ByteStep{}, ie::ui::PasteRule::Number) &&
                  sel.hasSel() && sel.lo() == 0 && sel.hi() == 3,
              "textsel: Ctrl+A 全选（表面无关）");

        ie::KeyEvent nonShiftLeft = ctrlA;
        nonShiftLeft.scancode = KeyLeft;
        nonShiftLeft.ctrl = false;
        ie::ui::textSelKey(nonShiftLeft, sel, s, ie::ui::ByteStep{}, ie::ui::PasteRule::Number);
        check(!sel.hasSel() && sel.caret == 0, "textsel: 有选区时方向键先收掉选区（不进格）");

        ie::KeyEvent shiftEnd = nonShiftLeft;
        shiftEnd.scancode = KeyEnd;
        shiftEnd.shift = true;
        ie::ui::textSelKey(shiftEnd, sel, s, ie::ui::ByteStep{}, ie::ui::PasteRule::Number);
        ie::KeyEvent back = nonShiftLeft;
        back.scancode = KeyBackspace;
        ie::ui::textSelKey(back, sel, s, ie::ui::ByteStep{}, ie::ui::PasteRule::Number);
        check(s.empty() && !sel.hasSel() && sel.caret == 0,
              "textsel: Shift+End 拉选区后退格删掉整段（不是删一个字）");

        std::string zh = "中";
        ie::ui::TextSel zsel;
        zsel.caret = (int)zh.size();
        ie::KeyEvent zb = back;
        ie::ui::textSelKey(zb, zsel, zh, ie::ui::Utf8Step{}, ie::ui::PasteRule::Text);
        check(zh.empty() && zsel.caret == 0, "textsel: UTF-8 面退格删一整个汉字");

        ie::KeyEvent ka;
        ka.scancode = KeyA;
        ka.ctrl = true;
        ka.shift = true;
        check(ie::ui::resolveTextCmd(ka) == ie::ui::TextCmd::SelectAll,
              "textsel: Ctrl+Shift+A 仍是全选（Shift 不参与组合身份）");
        ka.alt = true;
        check(ie::ui::resolveTextCmd(ka) == ie::ui::TextCmd::None,
              "textsel: Ctrl+Alt+A 不是全选（修饰位要精确匹配，不串味）");
        ka.alt = false;
        ka.ctrl = false;
        ka.shift = false;
        check(ie::ui::resolveTextCmd(ka) == ie::ui::TextCmd::None,
              "textsel: 裸 A 不是命令（它是字符，归表面自己过滤）");
        ie::KeyEvent kl;
        kl.scancode = 38;
        kl.ctrl = true;
        check(ie::ui::resolveTextCmd(kl) == ie::ui::TextCmd::None,
              "textsel: 表里没有的组合一律 None（加 Ctrl+L 只需加一行表）");

        {
            std::string s2;
            ie::ui::TextSel h;
            h.reset();
            ie::KeyEvent z;
            z.pressed = true;
            z.scancode = KeyZ;
            z.ctrl = true;
            ie::KeyEvent zr = z;
            zr.shift = true;
            ie::KeyEvent y;
            y.pressed = true;
            y.scancode = KeyY;
            y.ctrl = true;

            ie::ui::textSelInsert(h, s2, "1");
            ie::ui::textSelInsert(h, s2, "2");
            check(s2 == "12" && h.undoStack.size() == 1,
                  "textsel: 连续打字合并成一条历史（12 是一步，不是两步）");

            ie::ui::textSelKey(z, h, s2, ie::ui::ByteStep{}, ie::ui::PasteRule::Number);
            check(s2.empty() && h.caret == 0, "textsel: Ctrl+Z 退回连打之前");

            ie::ui::textSelKey(zr, h, s2, ie::ui::ByteStep{}, ie::ui::PasteRule::Number);
            check(s2 == "12" && h.caret == 2, "textsel: Ctrl+Shift+Z 恢复整段");

            ie::ui::textSelKey(y, h, s2, ie::ui::ByteStep{}, ie::ui::PasteRule::Number);
            check(s2 == "12" && h.redoStack.empty(),
                  "textsel: 没得恢复时 Ctrl+Y 不动文本（也吃掉这一键，不漏到文档撤销栈）");
            h.reset();
            check(h.undoStack.empty() && h.redoStack.empty() && !h.hasSel(),
                  "textsel: 切换格子（reset）清空会话历史，Ctrl+Z 不会跨格乱撤");
        }
    }

    {
        ie::KeyEvent g;
        g.pressed = true;
        g.scancode = KeyZ;
        g.ctrl = true;
        check(ie::resolveGlobalKey(g) == ie::KeyAct::Undo, "globalmap: Ctrl+Z 是撤销");
        g.shift = true;
        check(ie::resolveGlobalKey(g) == ie::KeyAct::Redo, "globalmap: Ctrl+Shift+Z 是恢复（一行表，不是分支里判 shift）");
        g.shift = false;
        g.alt = true;
        check(ie::resolveGlobalKey(g) == ie::KeyAct::None, "globalmap: Ctrl+Alt+Z 什么都不是");
        g.alt = false;
        g.scancode = KeyY;
        check(ie::resolveGlobalKey(g) == ie::KeyAct::Redo, "globalmap: Ctrl+Y 也是恢复");
        g.ctrl = false;
        g.scancode = KeyA;
        check(ie::resolveGlobalKey(g) == ie::KeyAct::ToolLasso,
              "globalmap: 裸 A 是工具快捷键（表里绑的是套索）");
        g.scancode = KeyDelete;
        check(ie::resolveGlobalKey(g) == ie::KeyAct::Delete, "globalmap: 裸 Delete 是删除（是否生效由焦点另判）");
        g.scancode = KeyTab;
        check(ie::resolveGlobalKey(g) == ie::KeyAct::NextTab, "globalmap: Tab 是下一个标签页");
        g.scancode = KeyT;
        g.ctrl = true;
        check(ie::resolveGlobalKey(g) == ie::KeyAct::Transform, "globalmap: Ctrl+T 是变形（不再是新建标签页）");
    }

    {
        Editor pe;
        const float fg[4] = {0.1f, 0.2f, 0.3f, 0.4f};
        const float bg[4] = {0.9f, 0.8f, 0.7f, 0.6f};
        pe.palette.setFg(fg);
        pe.palette.setBg(bg);
        check(pe.palette.fg[0] == 0.1f && pe.palette.fg[3] == 0.4f && pe.palette.bg[0] == 0.9f && pe.palette.bg[3] == 0.6f,
              "V2: 前/背景色唯一经 Palette::setFg/setBg 写入（单一持有者）");
        pe.palette.swap();
        check(pe.palette.fg[0] == 0.9f && pe.palette.bg[0] == 0.1f, "V2: swap 亦只动 Palette 自持字段");
        pe.palette.reset();
        check(pe.palette.fg[0] == 0.0f && pe.palette.bg[0] == 1.0f, "V2: reset 归位（写全在 Palette 内）");
    }

    {
        Editor pe;
        const float fg[4] = {0.1f, 0.2f, 0.3f, 1.0f};
        const float bg[4] = {0.9f, 0.8f, 0.7f, 1.0f};
        pe.palette.setFg(fg);
        pe.palette.setBg(bg);
        pe.swapPalette();
        check(pe.palette.fg[0] == 0.9f && pe.palette.bg[0] == 0.1f,
              "T52: swapPalette 同时交换前景/背景色");
    }

    {
        struct Box : Node {
            Size sz;
            explicit Box(Size s) : sz(s) {}
            Size requestSize() const override { return sz; }
        };

        OverlayLayer ov;
        ov.place(Rect{0, 0, 800, 600}, Rect{0, 0, 800, 600});

        auto dlg = std::make_unique<Box>(Size{0, 0});
        Node* dlgRaw = dlg.get();
        ov.addFill(std::move(dlg));
        check(dlgRaw->frame() == Rect{0, 0, 800, 600}, "T52: 遮罩类子铺满父 frame");

        auto pop = std::make_unique<Box>(Size{200, 100});
        Node* popRaw = pop.get();
        ov.addAt(std::move(pop), 900.0f, 610.0f);
        check(popRaw->frame() == Rect{600, 500, 200, 100}, "T52: 弹层锚点越界按当前 frame 钳回");

        ov.place(Rect{0, 0, 1000, 700}, Rect{0, 0, 1000, 700});
        check(dlgRaw->frame() == Rect{0, 0, 1000, 700}, "T52: 改窗后遮罩跟随新尺寸（不持开窗快照）");
        check(popRaw->frame() == Rect{800, 600, 200, 100}, "T52: 改窗后弹层按新 frame 重新钳位");

        ov.place(Rect{0, 0, 400, 300}, Rect{0, 0, 400, 300});
        check(popRaw->frame() == Rect{200, 200, 200, 100}, "T52: 缩窗后弹层仍完整落在视野内");

        ov.pop();
        check(ov.top() == dlgRaw, "T52: pop 只摘顶层，摆放参数与子同步弹出");
    }

    {
        Editor se;
        se.newDocument(240, 180, Color(0, 0, 0, 0));
        Layer& A = se.doc.layers[0];
        A.img = Image(60, 60);
        A.img.fillAll(0xffff0000u);
        A.x = 20;
        A.y = 20;
        A.style.strokeOn = true;
        A.style.strokeWidth = 3.0f;
        A.style.strokeColor[0] = A.style.strokeColor[1] = A.style.strokeColor[2] = 0.0f;
        A.style.strokeColor[3] = 1.0f;

        Image bim(40, 40);
        bim.fillAll(0xffffffffu);
        se.doc.addLayerAt(std::move(bim), 120, 90, "B");
        Layer& B = se.doc.layers[1];
        B.style.shadowOn = true;
        B.style.shadowDist = 12.0f;
        B.style.shadowSize = 4.0f;
        B.style.shadowAngle = 135.0f;
        B.style.shadowOpacity = 80.0f;
        check(stylePad(A.style) == 4 && stylePad(B.style) > 4, "T54: 样式外扩只有 stylePad 一个定义");
        const int padB = stylePad(B.style);
        check(se.doc.layerVisualBounds(1).w == se.doc.layerContentBounds(1).w + padB * 2,
              "T54: 显示范围 = 内容框 + 样式外扩");

        se.doc.markAllDirty();
        se.doc.recomposite(Rect{0, 0, 240, 180});
        se.doc.takeDirty();
        const Rect vBold = se.doc.layerVisualBounds(1);

        se.doc.active = 1;
        se.beginMove();
        se.updateMove(25, 18, false);
        const Rect d = se.doc.takeDirty();
        se.endMove();
        check(d.intersect(vBold) == vBold, "T54: 移动带投影的图层，脏区覆盖旧位置投影范围");

        se.doc.recomposite(d);
        const Image& inc = se.doc.compositeImage();
        std::vector<uint32_t> incPx(inc.pixels(), inc.pixels() + (size_t)240 * 180);
        se.doc.markAllDirty();
        se.doc.recomposite(Rect{0, 0, 240, 180});
        const uint32_t* fullPx = se.doc.compositeImage().pixels();
        bool same = true;
        for (size_t i = 0; i < incPx.size(); ++i)
            if (incPx[i] != fullPx[i]) {
                same = false;
                break;
            }
        check(same, "T54: 移动后增量合成 == 整幅重算（无样式残影）");

        const Rect vOld = se.doc.layerVisualBounds(1);
        se.doc.takeDirty();
        LayerStyle noShadow = se.doc.layers[1].style;
        noShadow.shadowOn = false;
        se.applyLayerSettings(1, se.doc.layers[1].blend, 1.0f, false, noShadow);
        const Rect d2 = se.doc.takeDirty();
        check(d2.intersect(vOld) == vOld, "T54: 关掉投影时脏区覆盖旧的投影范围");
    }

    {
        Image solid(16, 12);
        solid.fillAll(0xff3366ccu);
        int ox = 0, oy = 0;
        const XForm xf{8.0f, 6.0f, 2.0f, 2.0f, 0.0f};
        Image up = xformRasterize(solid, Rect{0, 0, 16, 12}, xf, 0, 0, ox, oy);
        const Rect cb = up.contentBounds();
        bool exact = true;
        for (int y = cb.y + 1; y < cb.bottom() - 1 && exact; ++y)
            for (int x = cb.x + 1; x < cb.right() - 1; ++x)
                if (up.pixels()[(size_t)y * up.width() + (size_t)x] != 0xff3366ccu) {
                    exact = false;
                    break;
                }
        check(exact, "T54: 2× 放大内部像素与源像素逐字节一致（两趟 == 4 邻域）");
        check(cb.w == 32 && cb.h == 24, "T54: 2× 放大的内容框尺寸为 32×24");
    }

    {
        Image art(8, 6);
        art.detach();
        for (size_t i = 0; i < 8u * 6u; ++i)
            art.pixels()[i] = 0xff000000u | ((uint32_t)(i * 2246822519u) & 0x00ffffffu);
        int oxA = 0, oyA = 0, oxB = 0, oyB = 0;
        const XForm t{13.0f, 10.0f, 1.0f, 1.0f, 0.0f};
        Image fast = xformRasterize(art, Rect{0, 0, 8, 6}, t, 0, 0, oxA, oyA, true);
        Image fine = xformRasterize(art, Rect{0, 0, 8, 6}, t, 0, 0, oxB, oyB, false);
        bool same = (fast.width() == fine.width() && fast.height() == fine.height() && oxA == oxB && oyA == oyB);
        if (same) {
            const Rect cb = fine.contentBounds();
            for (int y = cb.y; y < cb.bottom() && same; ++y)
                for (int x = cb.x; x < cb.right(); ++x)
                    if (fast.pixels()[(size_t)y * fast.width() + (size_t)x] !=
                        fine.pixels()[(size_t)y * fine.width() + (size_t)x]) {
                        same = false;
                        break;
                    }
        }
        check(same, "T55: 仅平移时拖动快速路径 == 精确路径（逐字节，aseprite FAST 同一性质）");

        int ox2 = 0, oy2 = 0;
        const XForm s{4.0f, 3.0f, 2.0f, 2.0f, 0.0f};
        Image upFast = xformRasterize(art, Rect{0, 0, 8, 6}, s, 0, 0, ox2, oy2, true);
        const Rect cb2 = upFast.contentBounds();
        bool ok2 = cb2.w >= 16 && cb2.h >= 12;
        for (int y = cb2.y + 1; y < cb2.bottom() - 1 && ok2; ++y)
            for (int x = cb2.x + 1; x < cb2.right() - 1; ++x) {
                const uint32_t v = upFast.pixels()[(size_t)y * upFast.width() + (size_t)x];
                if ((v >> 24) != 0xffu) {
                    ok2 = false;
                    break;
                }
            }
        check(ok2, "T55: 拖动快速路径 2× 放大后内容不透明、尺寸正确");
    }

    {
        Editor e;
        e.newDocument(200, 150, Color(255, 255, 255, 255));
        e.view.vpX = 0;
        e.view.vpY = 0;
        e.view.vpW = 800;
        e.view.vpH = 600;
        e.view.fit(e.doc.width, e.doc.height);
        auto mv = makeMoveTool();
        mv->onEnter(e);
        e.beginFreeTransform(0);
        check(e.transforming(), "T56: 双击进入的自由变换状态可由工具读到");

        Vec2f q[4];
        e.transformQuad(q);
        InputFrame in;
        const auto screenOf = [&](const Vec2f& p) {
            const Vec2f s = e.view.toScreen(p.x, p.y, e.doc.width, e.doc.height);
            return Vec2f(s.x, s.y);
        };
        in.mouse = screenOf(q[0]);
        check(mv->cursor(in) == CursorShape::ResizeNWSE, "T56: 悬停左上角 → 对角缩放光标(NWSE)");
        in.mouse = screenOf(q[1]);
        check(mv->cursor(in) == CursorShape::ResizeNESW, "T56: 悬停右上角 → 对角缩放光标(NESW)");

        const Vec2f c0 = screenOf(q[0]);
        in.mouse = Vec2f(c0.x + 10.0f, c0.y + 10.0f);
        check(mv->cursor(in) == CursorShape::ResizeNWSE, "T56: 角内侧 10px 仍是角手柄（方框命中，不被旋转带抢走）");

        const Vec2f q0 = screenOf(q[0]), q3 = screenOf(q[3]);
        in.mouse = Vec2f((q0.x + q3.x) * 0.5f, q0.y + 18.0f);
        check(mv->cursor(in) == CursorShape::Rotate, "T56: 上边框内侧 → 旋转光标");

        const Vec2f boxC{(q0.x + q[2].x) * 0.5f, (q0.y + q[2].y) * 0.5f};
        InputFrame center;
        center.mouse = Vec2f(boxC.x, boxC.y);
        check(mv->cursor(center) == CursorShape::Move, "T56: 变形框内 → 四向拖动光标");

        InputFrame away;
        away.mouse = Vec2f(q0.x - 120.0f, q0.y - 120.0f);
        check(mv->cursor(away) == CursorShape::Rotate, "T56: 变形线之外 → 旋转光标");
        e.cancelFreeTransform();
    }

    {
        check(textFontReady(), "T56: 系统字体已加载（默认取 C:/Windows/Fonts 里的中文字体）");
        check(textFontCount() > 0, "T56: 字体注册表非空（下拉条目有来源）");

        TextData t;
        t.x = 10.0f;
        t.y = 20.0f;
        t.size = 32;
        const std::string s = "ab中\n第二行";
        t.text = s;
        const TextMetrics m = measureText(t);
        check(m.ok && m.width > 0.0f && m.lines == 2, "T56: 中英混排测量成功且按换行分成两行");

        bool roundTrip = true;
        for (int off = 0; off <= (int)s.size(); ++off) {
            if (off > 0 && ((unsigned char)s[(size_t)off] & 0xC0) == 0x80) continue;
            const TextCaret c = textCaretAt(t, off);
            const int back = textOffsetAt(t, c.x, c.y + c.h * 0.5f);
            if (back != off) {
                roundTrip = false;
                std::printf("  caret 偏移 %d 反查得 %d\n", off, back);
                break;
            }
        }
        check(roundTrip, "T56: 光标偏移与位置双向一致（选区不会和字形错位）");

        Rect boxes[8];
        const int nb = textSelectionRects(t, 1, (int)s.size(), boxes, 8);
        {
            char sl[220];
            std::snprintf(sl, sizeof sl, "T56: 跨行选区产出逐行矩形 块数=%d", nb);
            check(nb >= 2, sl);
        }
        const int nb2 = textSelectionRects(t, 2, 2, boxes, 8);
        check(nb2 == 0, "T56: 空区间不产生选区");
    }

    {
        Editor e;
        e.newDocument(40, 30, Color(255, 255, 255, 255));
        e.addEmptyLayer();
        e.doc.active = 0;
        const size_t before = e.doc.layers.size();

        e.sel.reset(e.doc.width, e.doc.height);
        e.sel.setRect(Rect{10, 8, 12, 10});
        e.deleteByKey();
        check(e.doc.layers.size() == before, "T57: 有画布选区时 Delete 只清选区内容，图层数量不变");
        check(e.doc.layers[0].img.pixels()[(size_t)9 * e.doc.layers[0].img.width() + 12] == 0,
              "T57: 选区内像素被清成透明");
        check(e.doc.layers[0].img.pixels()[(size_t)2 * e.doc.layers[0].img.width() + 2] != 0,
              "T57: 选区外像素原样保留");

        e.sel.clear();
        e.deleteByKey();
        check(e.doc.layers.size() == 1, "T57: 无选区时 Delete 才删图层（且至少保留一层）");

        e.addEmptyLayer();
        e.addEmptyLayer();
        e.doc.active = 0;
        e.doc.selectOnly(0);
        e.extendLayerSelection(2);
        check(e.doc.selectedIndices().size() == 3, "T57: Shift 点击选中连续区间（含端点）");
        e.doc.selectOnly(1);
        e.doc.selectRange(1);
        check(e.doc.selectedIndices().size() == 1, "T57: 锚点与点击同一层时退化为单选");

        std::string v = "123456";
        TextField tf(&v);
        tf.setFocus(true);
        KeyEvent a;
        a.scancode = KeyA;
        a.ctrl = true;
        tf.onKey(a);
        KeyEvent bs;
        bs.scancode = KeyBackspace;
        tf.onKey(bs);
        check(v.empty(), "T57: 输入框 Ctrl+A 后 Backspace 清空整格");
        KeyEvent zz;
        zz.scancode = KeyZ;
        zz.ctrl = true;
        tf.onKey(zz);
        check(v == "123456", "T57: 输入框 Ctrl+Z 撤销回进入这一格时的值");

        e.addEmptyLayer();
        e.addEmptyLayer();
        e.doc.active = 1;
        e.selectAllLayers();
        check(e.doc.selectedIndices().size() == e.doc.layers.size(),
              "T59: Ctrl+A 全选图层（含 active，数量等于层数）");
        check(e.doc.layers[0].img.empty() == false, "T59: 全选图层不碰像素（层内容仍在）");
    }

    {
        struct CursorProbe : ie::Node {
            ie::CursorShape shape = ie::CursorShape::Arrow;
            bool claims = true;
            ie::Size sz{0, 0};
            ie::Size requestSize() const override { return sz; }
            bool cursorAt(float, float, ie::CursorShape& out) override
            {
                if (!claims) return false;
                out = shape;
                return true;
            }
        };
        ie::Container root;
        root.place(ie::Rect{0, 0, 200, 200}, ie::Rect{0, 0, 200, 200});
        auto a = std::make_unique<CursorProbe>();
        CursorProbe* pa = a.get();
        pa->shape = ie::CursorShape::IBeam;
        root.add(std::move(a));
        pa->place(ie::Rect{0, 0, 200, 200}, ie::Rect{0, 0, 200, 200});

        ie::CursorShape got = ie::CursorShape::None;
        check(root.cursorAt(50, 50, got) && got == ie::CursorShape::IBeam,
              "T60: 底层对象自己给形状（画布给 IBeam）");

        auto b = std::make_unique<CursorProbe>();
        CursorProbe* pb = b.get();
        pb->shape = ie::CursorShape::Arrow;
        root.add(std::move(b));
        pb->place(ie::Rect{100, 100, 100, 100}, ie::Rect{100, 100, 100, 100});

        ie::CursorShape top = ie::CursorShape::None;
        check(root.cursorAt(150, 150, top) && top == ie::CursorShape::Arrow,
              "T60: 上层对象压住下层时，形状由上层给（面板的箭头盖住画布）");
        ie::CursorShape low = ie::CursorShape::None;
        check(root.cursorAt(50, 50, low) && low == ie::CursorShape::IBeam,
              "T60: 落在只有下层覆盖的地方，仍由下层回答");

        pb->claims = false;
        ie::CursorShape pass = ie::CursorShape::None;
        check(root.cursorAt(150, 150, pass) && pass == ie::CursorShape::Arrow,
              "T60: 上层接住按下却不声明形状 → 箭头，不漏给画布的工具图标");
        pb->place(ie::Rect{100, 100, 100, 100}, ie::Rect{0, 0, 0, 0});
        check(root.cursorAt(150, 150, pass) && pass == ie::CursorShape::IBeam,
              "T60: 接不住指针的上层不吞这次提问，逐级上抛");

        ie::CursorShape none = ie::CursorShape::None;
        check(!root.cursorAt(250, 250, none), "T60: 谁都不覆盖的地方没人回答，宿主退回 Arrow");
    }


    check(kN32_SkColorType == kBGRA_8888_SkColorType, "T7: kN32=BGRA（Windows 窗口原生字节序前提）");
    {
        uint32_t p = 0x803228C8u;
        CanvasNode::toNative(&p, 1);
        check(p == 0x80641419u, "T7: toNative 预乘最近舍入 + BGRA 换序");
        uint32_t o = 0xFF102030u;
        CanvasNode::toNative(&o, 1);
        check(o == 0xFF302010u, "T7: toNative 全不透明=精确换序");
        uint32_t z = 0x003228C8u;
        CanvasNode::toNative(&z, 1);
        check(z == 0u, "T7: toNative alpha=0 → 0");
        uint32_t rows[2] = {0xFF802040u, 0x00FFFFFFu};
        CanvasNode::toNative(rows, 2);
        check(rows[0] == 0xFF402080u && rows[1] == 0u, "T7: toNative 批量段界正确");
    }

    {
        const int W = 4, H = 2;
        uint32_t src[W * H];
        for (int y = 0; y < H; ++y) {
            src[y * W + 0] = Color(255, 0, 0, 255).packed();
            src[y * W + 1] = Color(0, 255, 0, 128).packed();
            src[y * W + 2] = Color(0, 0, 255, 0).packed();
            src[y * W + 3] = Color(0, 0, 255, 255).packed();
        }
        uint32_t native[W * H];
        std::memcpy(native, src, sizeof(src));
        CanvasNode::toNative(native, W * H);

        auto drawOld = [&] {
            uint32_t px[W * H];
            {
                auto canvas = SkCanvas::MakeRasterDirect(SkImageInfo::MakeN32(W, H, kOpaque_SkAlphaType), px, W * 4);
                canvas->clear(SkColors::kGray);
                SkImageInfo oldInfo = SkImageInfo::Make(W, H, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
                sk_sp<SkImage> img = SkImages::RasterFromData(oldInfo, SkData::MakeWithoutCopy(src, sizeof(src)), W * 4);
                canvas->drawImageRect(img.get(), SkRect::MakeWH(W, H), SkRect::MakeWH(W, H),
                                      SkSamplingOptions(SkFilterMode::kNearest), nullptr,
                                      SkCanvas::kFast_SrcRectConstraint);
            }
            return std::vector<uint32_t>(px, px + W * H);
        };
        auto drawNew = [&] {
            uint32_t px[W * H];
            {
                auto canvas = SkCanvas::MakeRasterDirect(SkImageInfo::MakeN32(W, H, kOpaque_SkAlphaType), px, W * 4);
                canvas->clear(SkColors::kGray);
                SkImageInfo newInfo = SkImageInfo::Make(W, H, kN32_SkColorType, kPremul_SkAlphaType);
                sk_sp<SkImage> img = SkImages::RasterFromData(newInfo, SkData::MakeWithoutCopy(native, sizeof(native)), W * 4);
                canvas->drawImageRect(img.get(), SkRect::MakeWH(W, H), SkRect::MakeWH(W, H),
                                      SkSamplingOptions(SkFilterMode::kNearest), nullptr,
                                      SkCanvas::kFast_SrcRectConstraint);
            }
            return std::vector<uint32_t>(px, px + W * H);
        };
        const std::vector<uint32_t> oldPx = drawOld(), newPx = drawNew();
        check(oldPx == newPx,
              "T7: 旧(RGBA·unpremul)与新(BGRA·premul) blit 到 BGRA 目标逐字节相同（含半透明/α边缘）");
    }

    {
        Image im(4, 4);
        im.fillAll(Color(255, 0, 0, 255).packed());
        check(!imageio::save("selftest_noext", im, 92), "save: 无扩展名走错误分支不抛异常");
        check(!imageio::save("selftest_bad.xyz", im, 92), "save: 不支持格式走错误分支不抛异常");
    }

    {
        using PS = CanvasViewState::PanStep;
        const PS s0 = CanvasViewState::panStep(false, false, false, 0, true);
        check(!s0.panning && !s0.move, "pan: 会话外普通 move 不开不移动");
        const PS s1 = CanvasViewState::panStep(false, true, false, 2, true);
        check(s1.panning && s1.begin && !s1.move, "pan: 按下中键开启会话");
        const PS s2 = CanvasViewState::panStep(true, false, false, 0, true);
        check(s2.panning && s2.move, "pan: 会话内 move(button=0) 仍平移(源码:LAF move 不带 button)");
        const PS s3 = CanvasViewState::panStep(true, false, true, 2, true);
        check(!s3.panning && s3.end, "pan: 释放中键结束会话");
        const PS s4 = CanvasViewState::panStep(true, false, false, 0, false);
        check(!s4.panning && !s4.move, "pan: 出画布结束会话不移动");
    }

    {
        const std::string dir = "C:\\用户\\图片";
        check(composeSavePath(dir, "无标题", ".png") == "C:\\用户\\图片\\无标题.png",
              "savepath: 中文目录+中文名补扩展(UTF-8 不转码页)");
        check(composeSavePath(dir, "照片.jpg", ".png") == "C:\\用户\\图片\\照片.jpg",
              "savepath: 文件名已有扩展不重复补");
        check(composeSavePath("", "a", ".bmp") == ".\\a.bmp",
              "savepath: 空目录用 . 并补扩展");
        check(composeSavePath(dir + "\\", "b", ".webp") == "C:\\用户\\图片\\b.webp",
              "savepath: 目录已带尾分隔不重复加");

        check(dirNameOf("C:\\用户\\图片\\无标题.png") == "C:\\用户\\图片",
              "dirname: 取中文来源目录");
        check(dirNameOf("C:\\a.png") == "C:",
              "dirname: 盘根下取 'C:'");
        check(dirNameOf("无标题.png") == ".",
              "dirname: 无分隔符回退当前目录");
        check(composeSavePath(dirNameOf("C:\\图\\a.png"), "b", ".jpg") == "C:\\图\\b.jpg",
              "dirname: 来源目录端到端 = 存回同一目录");
    }

    {
        const std::vector<std::string> have = {"C:\\d\\x.png", "C:\\d\\x-1.png"};
        auto ex = [&](const std::string& p) { return std::find(have.begin(), have.end(), p) != have.end(); };
        check(uniquePathName("C:\\d", "x", ".png", ex) == "C:\\d\\x-2.png",
              "unique: 已存 x,x-1 → x-2");
        check(uniquePathName("C:\\d", "y", ".jpg", ex) == "C:\\d\\y.jpg",
              "unique: 不冲突则原样");
        check(uniquePathName("C:\\d", "x-1", ".png", ex) == "C:\\d\\x-2.png",
              "unique: 去叠尾号 x-1 → x-2 而非 x-1-1");
        check(uniquePathName("C:\\d", "报告", ".webp", ex) == "C:\\d\\报告.webp",
              "unique: 中文名不冲突原样(UTF-8 字节)");
    }

    {
        Editor e;
        e.newDocument(8, 8, Color(0, 0, 0, 0));
        e.doc.addEmptyLayer("a");
        e.doc.addEmptyLayer("b");
        const int before = (int)e.doc.layers.size();
        e.doc.selectOnly(0);
        e.doc.toggleSelect(1);
        e.deleteSelectedLayers();
        const int after = (int)e.doc.layers.size();
        check(after < before && after >= 1, "del: 无选区删除选中图层，层数减少且至少保留一层");
    }

    {
        ie::Host host;
        auto dlg = ie::ui::makeImageSizeDialog(host);
        const int W = 640, H = 400;
        dlg->place(ie::Rect{0, 0, W, H}, ie::Rect{0, 0, W, H});
        std::vector<uint32_t> pix((size_t)W * H, 0);
        auto can = SkCanvas::MakeRasterDirect(
            SkImageInfo::MakeN32(W, H, kPremul_SkAlphaType), pix.data(), W * 4);
        dlg->paint(*can);
        const uint32_t corner = pix[1 * W + 1];
        const uint32_t panelPx = pix[(size_t)(H / 2) * W + W / 2];
        check((corner & 0xff000000u) == 0,
              "modal: 对话框之外不再压黑（遮罩是透明的，不改变画布观感）");
        check((panelPx & 0xff000000u) != 0, "modal: 面板本身照常不透明地画出来");
    }
    {
        ie::Host host;
        host.ed().newDocument(100, 50, ie::Color(255, 255, 255, 255));
        auto dlg = ie::ui::makeImageSizeDialog(host);
        const int W = 640, H = 480;
        dlg->place(ie::Rect{0, 0, W, H}, ie::Rect{0, 0, W, H});
        const int px = 130, py = 145;
        const float rowY = (float)(py + 44 + 8 + 15);
        const float ctrlX = (float)(px + 16 + 120 + 40);
        auto press = [&](float x, float y) {
            ie::PointerEvent pe;
            pe.pos = ie::Vec2f{x, y};
            pe.pressed = true;
            pe.down = true;
            dlg->onPointer(pe);
            pe.pressed = false;
            dlg->onPointer(pe);
        };
        auto dragTo = [&](float x, float y) {
            ie::PointerEvent pe;
            pe.pos = ie::Vec2f{x, y};
            pe.pressed = false;
            pe.down = true;
            dlg->onPointer(pe);
        };
        auto release = [&](float x, float y) {
            ie::PointerEvent pe;
            pe.pos = ie::Vec2f{x, y};
            pe.pressed = false;
            pe.down = false;
            pe.released = true;
            dlg->onPointer(pe);
        };
        press(ctrlX, rowY);
        dragTo(ctrlX + 60.0f, rowY);
        release(ctrlX + 60.0f, rowY);
        const float okX = (float)(px + 380 - 16 - 42), okY = (float)(py + 190 - 12 - 15);
        press(okX, okY);
        release(okX, okY);
        const ie::Document& d = host.ed().doc;
        const int wAfter = d.width;
        check(wAfter > 100, "图像大小: 拖宽度行 + 确定之后画布真的变宽了（拖之前先确认这一步没落空）");
        check(d.height != 50, "图像大小: 保持比例时改宽度，高度不是原地不动");
        check(std::abs(d.height - (int)std::lround((double)wAfter * 50.0 / 100.0)) <= 1,
              "图像大小: 高度按原比例 100:50 算出来");
    }
    {
        ie::Host host;
        host.ed().newDocument(32, 32, ie::Color(200, 120, 60, 255));
        auto dlg = ie::ui::makeThresholdDialog(host);
        const int W = 640, H = 480;
        dlg->place(ie::Rect{0, 0, W, H}, ie::Rect{0, 0, W, H});
        const int pw = dlg->requestSize().w;
        const int ph = dlg->requestSize().h;
        const int px = (W - pw) / 2, py = (H - ph) / 2;
        auto click = [&](float x, float y) {
            ie::PointerEvent pe;
            pe.pos = ie::Vec2f{x, y};
            pe.pressed = true;
            pe.down = true;
            dlg->onPointer(pe);
            pe.pressed = false;
            pe.down = false;
            pe.released = true;
            dlg->onPointer(pe);
        };
        const float okX = (float)(px + pw - 16 - 84 / 2);
        const float closeX = (float)(px + pw - 16 - 84 - 8 - 84 / 2);
        const float btnY = (float)(py + ph - 12 - 30 / 2);
        const size_t n0 = host.ed().hist.undoCount();
        click(okX, btnY);
        check(host.ed().hist.undoCount() == n0 + 1, "应用: 按一次记一步（一步就是图像改了一次）");
        click(okX, btnY);
        check(host.ed().hist.undoCount() == n0 + 2, "应用: 框不关，可以连着应用");
        click(closeX, btnY);
        check(host.ed().hist.undoCount() == n0 + 2, "关闭: 关窗不退已应用的步（撤销归图像，不归按钮）");
    }

    {
        ie::Editor e;
        e.newDocument(100, 50, ie::Color(255, 255, 255, 255));
        e.resizeCanvasAligned(200, 100, 1, 1);
        check(e.doc.width == 200 && e.doc.height == 100, "画布: 尺寸落到新值");
        check(e.doc.activeLayer()->x == 50 && e.doc.activeLayer()->y == 25,
              "画布: 居中格把整画布大小的层放到正中间");

        ie::Editor e2;
        e2.newDocument(100, 50, ie::Color(255, 255, 255, 255));
        e2.resizeCanvasAligned(200, 100, 2, 2);
        check(e2.doc.activeLayer()->x == 100 && e2.doc.activeLayer()->y == 50,
              "画布: 右下格把所有图层右对齐 / 底对齐");

        ie::Editor e3;
        e3.newDocument(100, 50, ie::Color(255, 255, 255, 255));
        e3.resizeCanvasAligned(200, 100, 0, 1);
        check(e3.doc.activeLayer()->x == 0 && e3.doc.activeLayer()->y == 25,
              "画布: 左中格 = 贴左、上下居中");
    }

    {
        const std::vector<std::string> titles = ie::ui::menuTitles();
        bool hasAbout = false, hasHelp = false;
        for (const auto& s : titles) {
            if (s == "作者") hasAbout = true;
            if (s == "帮助") hasHelp = true;
        }
        check(hasAbout && !hasHelp, "菜单: 顶层最后一项是「作者」，没有「帮助」");
        check(!titles.empty() && titles.back() == "作者", "菜单: 「作者」在最右");
        ie::Host host;
        auto dlg = ie::ui::makeAboutDialog(host);
        const ie::Size s = dlg->requestSize();
        check(s.h >= 200 + 44, "关于: 面板高度容得下 200×200 的图（底部不再留按钮条）");
        const int W = 640, H = 480;
        dlg->place(ie::Rect{0, 0, W, H}, ie::Rect{0, 0, W, H});
        std::vector<uint32_t> pix((size_t)W * H, 0);
        SkImageInfo info = SkImageInfo::MakeN32(W, H, kPremul_SkAlphaType);
        auto can = SkCanvas::MakeRasterDirect(info, pix.data(), (size_t)W * 4);
        if (can) {
            dlg->paint(*can);
            check(true, "关于: 画一遍不崩");
        }
        const ie::Rect panel = dlg->requestSize().w ? ie::Rect{0, 0, W, H} : ie::Rect{0, 0, W, H};
        (void)panel;
    }

    {
        ie::Host host;
        std::string bad;
        const int rowH = ie::ui::theme().rowH;
        const int widths[] = {1360};
        for (int wi = 0; wi < 1; ++wi) {
            const ie::Rect area{0, 0, widths[wi], ie::ui::dims::optionsH};
            for (int ti = 0; ti < host.toolsCount(); ++ti) {
                host.setTool(ti);
                ie::Props ps;
                host.tool().props(ps, host.ed());
                int xs = area.x + 8;
                for (int i = 1; i < (int)ps.size(); ++i) {
                    const ie::Prop& q = ps[(size_t)i];
                    if (q.kind != ie::PropKind::Float && q.kind != ie::PropKind::Int) {
                        int w = 0;
                        switch (q.kind) {
                            case ie::PropKind::Header: w = (int)ie::ui::textWidth(q.label) + 8; break;
                            case ie::PropKind::Label:
                                w = (int)ie::ui::textWidth(q.label) + (int)ie::ui::textWidth(q.hint) + 16;
                                break;
                            case ie::PropKind::Separator: w = 12; break;
                            case ie::PropKind::Bool: w = (int)ie::ui::textWidth(q.label) + 30; break;
                            case ie::PropKind::Combo: w = 172; break;
                            case ie::PropKind::Color: w = (int)ie::ui::textWidth(q.label) + 56; break;
                            case ie::PropKind::Button: w = area.w - 16; break;
                            case ie::PropKind::Seg: {
                                if (q.icons) {
                                    w = 30 * q.itemCount + 6 * std::max(0, q.itemCount - 1);
                                } else {
                                    int sum = 0;
                                    for (int k2 = 0; k2 < q.itemCount; ++k2)
                                        sum += (int)ie::ui::textWidth(q.items[k2]) + 16;
                                    w = sum + 6 * std::max(0, q.itemCount - 1);
                                }
                                break;
                            }
                            default: break;
                        }
                        xs += w + 8;
                        continue;
                    }
                    const ie::Prop& p = ps[(size_t)i];
                    const int labelW = ie::ui::propsLabelCol(ps, area.w - 16);
                    const float px = (float)(xs + labelW + 8);
                    const float py = (float)(area.y + 4 + rowH / 2);
                    auto bar = ie::ui::makeOptionsBar(host);
                    bar->place(area, area);
                    std::vector<uint32_t> pix((size_t)area.w * (size_t)area.h, 0);
                    auto can = SkCanvas::MakeRasterDirect(
                        SkImageInfo::MakeN32(area.w, area.h, kPremul_SkAlphaType), pix.data(), area.w * 4);
                    std::vector<double> before;
                    for (int k = 0; k < (int)ps.size(); ++k) {
                        const ie::Prop& q = ps[(size_t)k];
                        if (q.kind == ie::PropKind::Float)
                            before.push_back(q.f ? (double)*q.f : 0.0);
                        else if (q.kind == ie::PropKind::Int)
                            before.push_back(q.i ? (double)*q.i : 0.0);
                    }
                    ie::PointerEvent e0;
                    e0.pos = ie::Vec2f{px, py};
                    e0.pressed = true;
                    bar->onPointer(e0);
                    bar->paint(*can);
                    ie::PointerEvent m = e0;
                    m.pressed = false;
                    m.down = true;
                    m.pos = ie::Vec2f{px + 40.0f, py};
                    bar->onPointer(m);
                    ie::PointerEvent r = m;
                    r.down = false;
                    r.released = true;
                    bar->onPointer(r);
                    int col = -1;
                    for (int k = 0; k < (int)ps.size(); ++k) {
                        const ie::Prop& q = ps[(size_t)k];
                        if (q.kind != ie::PropKind::Float && q.kind != ie::PropKind::Int) continue;
                        ++col;
                        const double now = q.kind == ie::PropKind::Float ? (q.f ? (double)*q.f : 0.0)
                                                                        : (q.i ? (double)*q.i : 0.0);
                        if (k == i) {
                            if (now == before[(size_t)col])
                                bad += " " + std::string(host.tool().name()) + "/" + p.label;
                        } else if (now != before[(size_t)col]) {
                            bad += " " + std::string(host.tool().name()) + "误写/" + q.label;
                        }
                    }
                    xs += 168 + 8;
                }
            }
        }
        check(bad.empty(), (std::string("numrows: 拖哪一行就只写哪一行") + bad).c_str());
    }

    {
        const ie::ui::Theme& t = ie::ui::theme();
        auto lum = [](ie::Color32 c) {
            return (int)(0.299 * ((c >> 16) & 255) + 0.587 * ((c >> 8) & 255) + 0.114 * (c & 255));
        };
        const int lumLine = lum(t.boxLine);
        const int lumPanel = lum(t.panel);
        const int lumHi = lum(t.panelHi);
        const int worst = std::min(std::abs(lumLine - lumPanel), std::abs(lumLine - lumHi));
        check(worst >= 60, "控件界: 勾选框/色块的 1px 界与面板底色亮度差 >= 60（深色下看得见）");
    }

    {
        ie::Host host;
        host.setTool(1);
        const ie::Rect area{0, 0, 220, 210};
        int hit = 0;
        std::string miss;
        for (int k = 0; k < 4; ++k) {
            auto bar = ie::ui::makeOptionsBar(host);
            bar->place(area, area);
            ie::PointerEvent e;
            e.pos = ie::Vec2f{110.0f, 6.0f + (float)(k * 30) + 13.0f};
            e.pressed = true;
            bar->onPointer(e);
            ie::Props ps;
            host.tool().props(ps, host.ed());
            for (const ie::Prop& q : ps)
                if (q.kind == ie::PropKind::Seg && q.sel) {
                    if (*q.sel == k) ++hit;
                    else miss += " " + std::to_string(k) + "->" + std::to_string(*q.sel);
                }
        }
        check(hit == 4, (std::string("选区: 模式条一列四行、每行同宽，逐行都点得动") + miss).c_str());
    }

    {
        const ie::Rect half{-20, -20, 40, 40};
        const ie::Rect in = half.clampTo(200, 200);
        check(in.w == 20 && in.h == 20, "选区: 画布外的部分不计入尺寸（40×40 只算进来的一半 = 20×20）");
        const ie::Rect out{300, 300, 40, 40};
        const ie::Rect none = out.clampTo(200, 200);
        check(none.w <= 0 && none.h <= 0, "选区: 整块在画布外时尺寸不出现负数（钳成 0）");
    }

    {
        ie::Host host;
        host.setTool(1);
        const ie::Rect area{0, 0, 286, 240};
        int hit = 0;
        for (int k = 0; k < 4; ++k) {
            auto bar = ie::ui::makeOptionsBar(host);
            bar->place(area, area);
            ie::PointerEvent e;
            e.pos = ie::Vec2f{143.0f, 6.0f + (float)(k * 30) + 13.0f};
            e.pressed = true;
            bar->onPointer(e);
            ie::Props ps;
            host.tool().props(ps, host.ed());
            for (const ie::Prop& q : ps)
                if (q.kind == ie::PropKind::Seg && q.sel && *q.sel == k) ++hit;
        }
        check(hit == 4, "属性栏: 竖向面板（宽 286，不是 220）里模式条也是一列四行");
    }

    {
        ie::Host host;
        const ie::Rect area{0, 0, 1360, ie::ui::dims::optionsH};
        const int h = ie::ui::theme().rowH;
        std::string miss;
        int tested = 0;
        for (int ti = 0; ti < host.toolsCount(); ++ti) {
            host.setTool(ti);
            ie::Props ps;
            host.tool().props(ps, host.ed());
            std::vector<std::pair<int, ie::Rect>> items;
            int x = area.x + 8, y = area.y + 4;
            for (int i = 1; i < (int)ps.size(); ++i) {
                const ie::Prop& q = ps[(size_t)i];
                int w = 0;
                switch (q.kind) {
                    case ie::PropKind::Header: w = (int)ie::ui::textWidth(q.label) + 8; break;
                    case ie::PropKind::Label:
                        w = (int)ie::ui::textWidth(q.label) + (int)ie::ui::textWidth(q.hint) + 16;
                        break;
                    case ie::PropKind::Separator: w = 12; break;
                    case ie::PropKind::Float:
                    case ie::PropKind::Int: w = 168; break;
                    case ie::PropKind::Bool: w = (int)ie::ui::textWidth(q.label) + 30; break;
                    case ie::PropKind::Combo: w = 172; break;
                    case ie::PropKind::Color: w = (int)ie::ui::textWidth(q.label) + 56; break;
                    case ie::PropKind::Button: w = area.w - 16; break;
                }
                if (w > 0 && x + w > area.right() - 8) {
                    x = area.x + 8;
                    y += h + 4;
                }
                items.emplace_back(i, ie::Rect{x, y, w, h});
                x += w + 8;
            }
            for (auto& it : items) {
                const ie::Prop& p = ps[(size_t)it.first];
                if (p.kind != ie::PropKind::Bool || !p.b) continue;
                auto bar = ie::ui::makeOptionsBar(host);
                bar->place(area, area);
                const bool was = *p.b;
                ie::PointerEvent e;
                e.pos = ie::Vec2f{(float)(it.second.right() + 8), (float)(it.second.y + h / 2)};
                e.pressed = true;
                bar->onPointer(e);
                ie::PointerEvent up = e;
                up.pressed = false;
                up.released = true;
                bar->onPointer(up);
                ++tested;
                if (*p.b != was) miss += " 空白误触/" + std::string(p.label);
                ie::PointerEvent b2;
                b2.pos = ie::Vec2f{(float)(it.second.x + 7), (float)(it.second.y + h / 2)};
                b2.pressed = true;
                bar->onPointer(b2);
                if (*p.b == was) miss += " 框点不动/" + std::string(p.label);
            }
        }
        check(tested > 0 && miss.empty(),
              (std::string("属性栏: 空白处点不动、勾选框本身点得动") + miss).c_str());
    }

    {
        ie::Host host;
        const ie::Rect area{0, 0, 220, 210};
        int hitRows = 0;
        std::string miss;
        for (int ti = 0; ti < host.toolsCount(); ++ti) {
            host.setTool(ti);
            ie::Props ps;
            host.tool().props(ps, host.ed());
            int fi = -1;
            for (int i = 0; i < (int)ps.size(); ++i) {
                const ie::Prop& q = ps[(size_t)i];
                if (q.kind == ie::PropKind::Float || q.kind == ie::PropKind::Int) {
                    fi = i;
                    break;
                }
            }
            if (fi < 0) continue;
            const ie::Prop& p = ps[(size_t)fi];
            const int head = (ps.empty() || ps[0].kind != ie::PropKind::Header) ? 0 : 1;
            int rowY = 6;
            for (int k2 = head; k2 < fi; ++k2) {
                const ie::Prop& q = ps[(size_t)k2];
                const int n = std::max(1, q.itemCount);
                rowY += (q.kind == ie::PropKind::Seg ? 26 * n + 4 * (n - 1) : 26) + 4;
            }
            if (rowY + 22 > area.h) {
                miss += " 越界/" + std::string(host.tool().name());
                continue;
            }
            auto bar = ie::ui::makeOptionsBar(host);
            bar->place(area, area);
            std::vector<uint32_t> pix((size_t)area.w * (size_t)area.h, 0);
            auto can = SkCanvas::MakeRasterDirect(
                SkImageInfo::MakeN32(area.w, area.h, kPremul_SkAlphaType), pix.data(), area.w * 4);
            bar->paint(*can);
            const double before = p.f ? (double)*p.f : (double)*p.i;
            const float px = (float)(8 + ie::ui::propsLabelCol(ps, area.w - 16) + 18);
            const float py = (float)(rowY + 11);
            ie::PointerEvent e0;
            e0.pos = ie::Vec2f{px, py};
            e0.pressed = true;
            bar->onPointer(e0);
            ie::PointerEvent m = e0;
            m.pressed = false;
            m.down = true;
            m.pos = ie::Vec2f{px + 40.0f, py};
            bar->onPointer(m);
            ie::PointerEvent r = m;
            r.down = false;
            r.released = true;
            bar->onPointer(r);
            const double now = p.f ? (double)*p.f : (double)*p.i;
            if (now == before) miss += " 点不动/" + std::string(host.tool().name()) + "/" + p.label;
            else ++hitRows;
        }
        check(hitRows > 0 && miss.empty(),
              (std::string("属性栏: 220 宽单列排版，数值框仍点得动且不越界") + miss).c_str());
        check(area.w - 16 <= 220 - 16, "属性栏: 单列行宽等于面板宽减左右各 8");
    }

    {
        double a = 3.0, b = 7.0;
        auto getFn = +[](const void* ud) -> double { return *static_cast<const double*>(ud); };
        auto setFn = +[](void* ud, double v) { *static_cast<double*>(ud) = v; };
        ie::NumField f;
        f.get = getFn;
        f.set = setFn;
        f.readUd = &a;
        f.writeUd = &a;
        f.lo = 0.0;
        f.hi = 100.0;
        f.step = 1.0;

        f.beginEdit();
        ie::KeyEvent k1;
        k1.unicode = '1';
        f.key(k1);
        ie::KeyEvent k2;
        k2.unicode = '2';
        f.key(k2);
        check(f.text(true) == "12", "numfield: 被点的那一行显示键入内容（整段替换后为 12）");

        f.readUd = &b;
        check(f.text(false) == "7", "numfield: 别的行仍显示自己的值，不显示这一行的键入缓冲");

        f.readUd = &a;
        ie::KeyEvent ent;
        ent.scancode = ie::KeyEnter;
        f.key(ent);
        check(a == 12.0, "numfield: 提交只写被编辑的行（整段替换后为 12）");
        check(b == 7.0, "numfield: 未被编辑的行一个字节都不改");
    }

    {
        ie::Host host;

        host.ed().newDocument(32, 32, ie::Color(100, 100, 100, 255));
        const int W = 300, H = 180;
        auto panelOwn = ie::ui::makeImagePanel(host);
        ie::ui::ImagePanelNode* panel = (ie::ui::ImagePanelNode*)panelOwn.get();
        ie::Container root;
        root.place(ie::Rect{0, 0, W, H}, ie::Rect{0, 0, W, H});
        root.add(std::move(panelOwn));
        panel->place(ie::Rect{0, 0, W, H}, ie::Rect{0, 0, W, H});
        std::vector<uint32_t> pix((size_t)W * (size_t)H, 0);
        auto can = SkCanvas::MakeRasterDirect(SkImageInfo::MakeN32(W, H, kPremul_SkAlphaType), pix.data(), W * 4);
        panel->paint(*can);

        {
            ie::CursorShape cs = ie::CursorShape::None;
            check(root.cursorAt(10.0f, 10.0f, cs) && cs == ie::CursorShape::Arrow,
                  "图像浮层: 标题条给箭头（固定面板不再搬动）");
            check(root.cursorAt(90.0f, kImgRowY0, cs) && cs == ie::CursorShape::Arrow,
                  "tri 浮层: 非编辑态的数值行给箭头（进度条不是文本输入框）");
            ie::PointerEvent de;
            de.pos = ie::Vec2f{90.0f, kImgRowY0};
            de.pressed = true;
            de.doubleClick = true;
            panel->onPointer(de);
            check(root.cursorAt(90.0f, kImgRowY0, cs) && cs == ie::CursorShape::IBeam,
                  "tri 浮层: 进入数值输入态后给 I 形光标");
            ie::KeyEvent ke;
            ke.scancode = KeyEsc;
            panel->onKey(ke);
            check(root.cursorAt(20.0f, kImgRowY0, cs) && cs == ie::CursorShape::Arrow,
                  "tri 浮层: 标签/空白处给箭头（通用规则：接住指针的那层给箭头）");
            check(!root.cursorAt((float)W + 20.0f, 60.0f, cs),
                  "tri 浮层: 面板之外不回答（交给下面的对象）");
        }

        const ie::Layer* l = host.ed().doc.activeLayer();
        const std::vector<uint32_t> base(l->img.pixels(), l->img.pixels() + 32 * 32);
        auto compSig = [&]() {
            const int dw2 = host.ed().doc.width, dh2 = host.ed().doc.height;
            std::vector<uint32_t> px((size_t)dw2 * dh2, 0);
            host.ed().doc.compositeInto(px.data(), dw2, ie::Rect{0, 0, dw2, dh2}, -1);
            unsigned long long s = 0;
            for (size_t i = 0; i < px.size(); i += 5) s += (unsigned long long)px[i];
            return s;
        };
        const unsigned long long cs0 = compSig();
        const float bx = 12.0f + (float)panel->probeLabelW(), bw = 212.0f, by = kImgRowY0;
        ie::PointerEvent e0;
        e0.pos = ie::Vec2f{bx + 10.0f, by};
        e0.pressed = true;
        panel->onPointer(e0);
        panel->paint(*can);
        ie::PointerEvent m = e0;
        m.pressed = false;
        m.down = true;
        m.pos = ie::Vec2f{bx + 70.0f, by};
        panel->onPointer(m);
        panel->paint(*can);
        ie::PointerEvent r = m;
        r.down = false;
        r.released = true;
        panel->onPointer(r);

        const ie::LayerChild* c = host.ed().triChild(0);
        check(c && c->id > 0, "tri 浮层: 拖红后这一层多了一条带ID的三色子对象");
        check(c && c->r < 0.9f && c->g == 1.0f && c->b == 1.0f, "tri 浮层: 只写了红通道，绿蓝不动");
        const ie::Layer* l2 = host.ed().doc.activeLayer();
        bool pixSame2 = true;
        for (int i = 0; i < 32 * 32; ++i)
            if (l2->img.pixels()[i] != base[(size_t)i]) pixSame2 = false;
        check(pixSame2, "tri 浮层: 拖动后图层像素逐字节未动");
        check(host.ed().triChild(0)->r == c->r, "tri 浮层: 控件读数与文档同源");
        check(compSig() != cs0, "tri 浮层: 拖红之后合成画面确实变了（不是只有数字变了）");
        {
            ie::Document d2;
            d2.width = 8;
            d2.height = 8;
            ie::Layer lay;
            lay.img = ie::Image(8, 8);
            for (int i = 0; i < 64; ++i) lay.img.pixels()[i] = 0xff2060a0u;
            lay.id = 1;
            d2.layers.push_back(lay);
            auto sig = [](const ie::Layer& L) {
                int ox2 = 0, oy2 = 0;
                const ie::Image& st = ie::styledPixels(L, ox2, oy2);
                unsigned long long s = 0;
                for (int i = 0; i < st.width() * st.height(); ++i) s += (unsigned long long)st.pixels()[i];
                return s;
            };
            const unsigned long long sA = sig(d2.layers[0]);
            ie::LayerChild tc;
            tc.kind = ie::ChildKind::Tri;
            tc.id = 7;
            tc.r = 2.0f;
            d2.layers[0].children.push_back(tc);
            const unsigned long long sB = sig(d2.layers[0]);
            check(sA != sB, "tri 浮层: 屏幕像素链（styledPixels）也吃到三色，不只是离屏合成");
        }

        const float gy = by + 26.0f;
        const float redBefore = host.ed().triChild(0)->r;
        ie::PointerEvent ge = e0;
        ge.pos = ie::Vec2f{bx + 10.0f, gy};
        panel->onPointer(ge);
        panel->paint(*can);
        ie::PointerEvent gm = ge;
        gm.pressed = false;
        gm.down = true;
        gm.pos = ie::Vec2f{bx + 70.0f, gy};
        panel->onPointer(gm);
        panel->paint(*can);
        ie::PointerEvent gr = gm;
        gr.down = false;
        gr.released = true;
        panel->onPointer(gr);
        const ie::LayerChild* cg = host.ed().triChild(0);
        check(cg && cg->g < 0.9f, "tri 浮层: 拖绿真的改到绿通道");
        check(cg && std::fabs(cg->r - redBefore) < 1e-4f, "tri 浮层: 拖绿不动红（三个框各绑各的通道）");
        check(cg && cg->b == 1.0f, "tri 浮层: 拖绿不动蓝");
        const float byy = by + 52.0f;
        const float redNow = cg->r, greenNow = cg->g;
        ie::PointerEvent be = e0;
        be.pos = ie::Vec2f{bx + 10.0f, byy};
        panel->onPointer(be);
        panel->paint(*can);
        ie::PointerEvent bm = be;
        bm.pressed = false;
        bm.down = true;
        bm.pos = ie::Vec2f{bx + 120.0f, byy};
        panel->onPointer(bm);
        panel->paint(*can);
        ie::PointerEvent br = bm;
        br.down = false;
        br.released = true;
        panel->onPointer(br);
        const ie::LayerChild* cb = host.ed().triChild(0);
        check(cb && cb->b > 1.05f, "tri 浮层: 拖蓝真的改到蓝通道");
        check(cb && std::fabs(cb->r - redNow) < 1e-4f && std::fabs(cb->g - greenNow) < 1e-4f,
              "tri 浮层: 拖蓝不动红绿");

        host.ed().undo();
        const ie::LayerChild* cu = host.ed().triChild(0);
        check(cu && cu->b == 1.0f && std::fabs(cu->g - greenNow) < 1e-4f,
              "tri 浮层: 一次撤销只回退最后那一步（蓝回 100%，绿仍是刚才的值）");
        host.ed().undo();
        host.ed().undo();
        check(host.ed().triChild(0) == nullptr, "tri 浮层: 撤销后这条子对象整体回退");
        {
            const int rev0 = host.ed().doc.layers[0].pixRev;
            host.ed().applyLayerTri(0, 1.5f, 1.0f, 1.0f, false);
            check(host.ed().doc.layers[0].pixRev > rev0,
                  "tri 浮层: 改三色抬 pixRev，画布纹理缓存跟着失效重传");
            host.ed().undo();
        }
        (void)bw;

        {
            auto tap = [&](float x, float y, bool dbl) {
                ie::PointerEvent pe;
                pe.pos = ie::Vec2f{x, y};
                pe.pressed = true;
                pe.doubleClick = dbl;
                panel->onPointer(pe);
                pe.pressed = false;
                pe.down = false;
                pe.released = true;
                pe.doubleClick = false;
                panel->onPointer(pe);
            };
            ie::CursorShape cs2 = ie::CursorShape::None;
            tap(90.0f, kImgRowY0, true);
            check(root.cursorAt(90.0f, kImgRowY0, cs2) && cs2 == ie::CursorShape::IBeam,
                  "tri 浮层: 双击 A 后 A 行在编辑");
            tap(90.0f, kImgRowY1, true);
            check(root.cursorAt(90.0f, kImgRowY1, cs2) && cs2 == ie::CursorShape::IBeam,
                  "tri 浮层: 双击 B 后 B 行在编辑");
            check(root.cursorAt(90.0f, kImgRowY0, cs2) && cs2 == ie::CursorShape::Arrow,
                  "tri 浮层: 切到 B 时 A 的输入态已被收掉（不会两行同时在编辑）");
            tap(90.0f, kImgRowY0, false);
            check(root.cursorAt(90.0f, kImgRowY0, cs2) && cs2 == ie::CursorShape::Arrow,
                  "tri 浮层: 单击 A 只是把值跳到点击处，不会回到编辑态");
        }
    }

    {
        ie::Host host;
        host.ed().newDocument(64, 64, ie::Color(140, 140, 140, 255));
        {
            ie::Layer* l = host.ed().doc.activeLayer();
            if (l) for (int y = 8; y < 24; ++y)
                for (int x = 8; x < 24; ++x) l->img.pixels()[(size_t)y * 32 + x] = ie::Color(50, 50, 50, 255).packed();
        }
        const int W = 300, H = 240;
        auto panelOwn = ie::ui::makeImagePanel(host);
        ie::ui::ImagePanelNode* panel = (ie::ui::ImagePanelNode*)panelOwn.get();
        ie::Container root;
        root.place(ie::Rect{0, 0, W, H}, ie::Rect{0, 0, W, H});
        root.add(std::move(panelOwn));
        panel->place(ie::Rect{0, 0, W, H}, ie::Rect{0, 0, W, H});
        std::vector<uint32_t> pix((size_t)W * (size_t)H, 0);
        auto can = SkCanvas::MakeRasterDirect(SkImageInfo::MakeN32(W, H, kPremul_SkAlphaType), pix.data(), W * 4);
        panel->paint(*can);

        auto tapAt = [&](float x, float y) {
            ie::PointerEvent pe;
            pe.pos = ie::Vec2f{x, y};
            pe.pressed = true;
            panel->onPointer(pe);
            pe.pressed = false;
            pe.down = false;
            pe.released = true;
            panel->onPointer(pe);
        };
        const ie::Size before = panel->requestSize();
        {
            const ie::Layer* lv = host.ed().doc.activeLayer();
            check(lv && lv->style.overlayColor[0] == 0.0f && lv->style.overlayColor[1] == 0.0f &&
                      lv->style.overlayColor[2] == 0.0f,
                  "图像浮层: 颜叠颜色默认是黑（不是红）");
        }
        tapAt(10.0f, 10.0f);
        check(panel->requestSize().h == before.h, "图像浮层: 标题条单击不再折叠（高度不变）");
        tapAt(150.0f, 44.0f);
        panel->paint(*can);
        check(panel->requestSize().h != before.h, "图像浮层: 切到描边页后高度按该页行数重算");
        const size_t n0 = host.ed().hist.undoCount();
        const ie::Layer* l0 = host.ed().doc.activeLayer();
        check(l0 && !l0->style.strokeOn, "图像浮层: 描边页刚打开时启用是关的");
        tapAt((float)panel->probeToggle(0).x + 8.0f, kImgRowY0);
        const ie::Layer* l1 = host.ed().doc.activeLayer();
        check(l1 && l1->style.strokeOn, "图像浮层: 点「启用」真的写进这一层的 LayerStyle");
        check(host.ed().hist.undoCount() == n0 + 1, "图像浮层: 一次点击只留一条历史");

        check(root.hit((float)(W - 1), 120.0f) == panel, "图像浮层: 右边框归面板命中（不是漏给画布）");
        check(root.hit(150.0f, (float)(H - 1)) == panel, "图像浮层: 下边框归面板命中");
        check(root.hit((float)(W + 4), 120.0f) == panel,
              "图像浮层: 边框外 4px 仍归面板（hitMargin 让影子边也算手柄）");

        {
            const ie::Size ps = panel->requestSize();
            const int pw = 300;
            panel->place(ie::Rect{0, 0, pw, ps.h}, ie::Rect{0, 0, pw, ps.h});
        }
        tapAt(108.0f, 44.0f);
        check(panel->valueAt(0) == 0.0 && panel->valueAt(1) == 0.0, "图像浮层: 阴影页强度/半径初值是 0");
        auto dragRow = [&](float y, float x0, float x1) {
            ie::PointerEvent pe;
            pe.pos = ie::Vec2f{x0, y};
            pe.pressed = true;
            panel->onPointer(pe);
            pe.pressed = false;
            pe.down = true;
            pe.pos = ie::Vec2f{x1, y};
            panel->onPointer(pe);
            pe.down = false;
            pe.released = true;
            panel->onPointer(pe);
        };
        dragRow(kImgRowY0, 150.0f, 230.0f);
        check(panel->valueAt(0) > 0.0, "图像浮层: 拖强度行真的改值");
        tapAt(260.0f, 226.0f);
        check(panel->valueAt(0) == 0.0 && panel->valueAt(1) == 0.0, "图像浮层: 衡影页「重置」把强度/半径归 0");
        check(panel->valueAt(2) == 50.0 && panel->valueAt(3) == 0.0 && panel->valueAt(4) == 3.0,
              "图像浮层: 衡影页「重置」把灵敏度/柔化/上限也回默认（50/0/3）");
        auto layerSig = [&]() {
            const ie::Layer* l = host.ed().doc.activeLayer();
            if (!l) return 0ull;
            const ie::Image& im = l->img;
            unsigned long long s = 0;
            const uint32_t* px = im.pixels();
            const size_t n = (size_t)im.width() * (size_t)im.height();
            for (size_t i = 0; i < n; i += 7) s += (unsigned long long)px[i];
            return s;
        };
        const unsigned long long sig0 = layerSig();
        const float b0 = (float)panel->probeCtrl(0).x;
        const float b1 = (float)panel->probeCtrl(1).x;
        auto canvasSig = [&]() {
            const int dw = host.ed().doc.width, dh = host.ed().doc.height;
            std::vector<uint32_t> px((size_t)dw * (size_t)dh, 0);
            const ie::Rect full{0, 0, dw, dh};
            host.ed().doc.compositeInto(px.data(), dw, full, -1);
            unsigned long long s = 0;
            for (size_t i = 0; i < px.size(); i += 7) s += (unsigned long long)px[i];
            return s;
        };
        const unsigned long long cs0 = canvasSig();
        dragRow(kImgRowY0, 150.0f, 240.0f);
        dragRow(kImgRowY1, b1 + 60.0f, b1 + 60.0f);
        check(panel->valueAt(0) > 0.0, "图像浮层: 第二次拖强度行确实改了值");
        check(canvasSig() != cs0, "图像浮层: 衡影连拖两行之后合成结果确实变了（参数真的生效）");
        {
            const ie::LayerChild* c = host.ed().liftChild(host.ed().doc.active);
            check(c && c->liftStrength > 0.0f && c->liftRadius > 0.0f, "图像浮层: 衡影改的是这一层的记录（子对象在）");
        }
        check(layerSig() == sig0, "图像浮层: 衡影不改像素（原始像素一个字节都不动）");
        tapAt(260.0f, 226.0f);
        check(panel->valueAt(0) == 0.0 && panel->valueAt(1) == 0.0, "图像浮层: 重置把强度/半径归 0");
        check(layerSig() == sig0, "图像浮层: 重置后像素仍是原样");
        check(canvasSig() == cs0, "图像浮层: 重置后合成结果回到原样（不是只剩数字、画面还亮着）");

        {
            host.ed().doc.addLayer(ie::Image(32, 32), "第二层");
            const int a = host.ed().doc.active == 1 ? 0 : host.ed().doc.active;
            host.ed().doc.active = 1;
            check(panel->valueAt(0) == 0.0 && panel->valueAt(1) == 0.0,
                  "图像浮层: 换到另一层后衡影显示的是这一层自己的值（0），不是上一层的数");
            host.ed().doc.active = a;
        }
        dragRow(kImgRowY0, 280.0f, 280.0f);
        dragRow(kImgRowY1, 106.0f, 106.0f);
        {
            const ie::LayerChild* c = host.ed().liftChild(host.ed().doc.active);
            check(c && c->liftStrength > 0.0f, "图像浮层: 参数拉满后这一层的记录确实拉满");
        }
        dragRow(kImgRowY0, b0 + 70.0f, b0 - 30.0f);
        dragRow(kImgRowY1, b1 + 70.0f, b1 - 30.0f);
        check(panel->valueAt(0) == 0.0 && panel->valueAt(1) == 0.0, "图像浮层: 拖回 0 时数值确实是 0");
        check(layerSig() == sig0, "图像浮层: 参数拖回 0，像素始终是原样");
        tapAt(150.0f, 44.0f);
        {
            const ie::Layer* l = host.ed().doc.activeLayer();
            const bool was = l && l->style.strokeOn;
            tapAt(262.0f, kImgRowY0);
            const ie::Layer* l2 = host.ed().doc.activeLayer();
            check(l2 && l2->style.strokeOn == was, "图像浮层: 点勾选框右边的空白不该改勾选");
            tapAt((float)panel->probeToggle(0).x + 8.0f, kImgRowY0);
            const ie::Layer* l3 = host.ed().doc.activeLayer();
            check(l3 && l3->style.strokeOn != was, "图像浮层: 点勾选框本身要生效");
            tapAt((float)panel->probeToggle(0).x + 8.0f, kImgRowY0);
        }
        host.consumeSettingsFresh();
        check(!host.settingsFresh(), "默认桌面: 摆过一次就收旗（拖动图像面板不再带着图层面板跑）");
    }


    {
        ie::Host host;
        host.ed().newDocument(32, 32, ie::Color(255, 255, 255, 255));
        auto pane = ie::ui::makeLayerPanel(host);
        const ie::Rect area{0, 0, 290, 500};
        pane->place(area, area);
        const ie::Vec2f boxPt{area.x + 70.0f, area.y + 40.0f};
        ie::PointerEvent press;
        press.pos = boxPt;
        press.pressed = true;
        pane->onPointer(press);
        ie::PointerEvent rel = press;
        rel.pressed = false;
        rel.released = true;
        pane->onPointer(rel);

        ie::KeyEvent down;
        down.pressed = true;
        down.scancode = ie::KeyDown;
        host.dispatchKey(down);
        check((int)host.ed().doc.layers[0].blend == 1, "blend 待选: 按 ↓ 走下一项（正常 → 变暗）");
        host.dispatchKey(down);
        check((int)host.ed().doc.layers[0].blend == 2, "blend 待选: 再按 ↓ 继续下一项");
        host.ed().undo();
        check((int)host.ed().doc.layers[0].blend == 0, "blend 待选: 撤销一次回到待选前（连续 ↑↓ 合成一步）");

        pane->onPointer(press);
        pane->onPointer(rel);
        host.dispatchKey(down);
        check((int)host.ed().doc.layers[0].blend == 1, "blend 待选: 重新点开后 ↓ 仍生效");
        ie::KeyEvent esc = down;
        esc.scancode = ie::KeyEsc;
        host.dispatchKey(esc);
        check((int)host.ed().doc.layers[0].blend != 0, "blend 待选: Esc 解除时不动值");
        host.dispatchKey(down);
        check((int)host.ed().doc.layers[0].blend == 1, "blend 待选: 解除后 ↓ 不再改图层（免得漏给 MoveTool）");

        int stepped = 0;
        host.armList(ie::Rect{10, 10, 100, 20}, [&stepped](int dir) { stepped += dir; });
        ie::KeyEvent up = down;
        up.scancode = ie::KeyUp;
        host.dispatchKey(up);
        host.dispatchKey(up);
        check(stepped == -2, "armList: 工具面板的下拉共用同一套 ↑↓ 路由（字体列表也走这条）");
    }

    {
        ie::Host host;
        host.ed().newDocument(32, 32, ie::Color(255, 255, 255, 255));
        auto pane = ie::ui::makeLayerPanel(host);
        const ie::Rect area{0, 0, 290, 500};
        pane->place(area, area);
        ie::PointerEvent press;
        press.pos = ie::Vec2f{area.x + 70.0f, area.y + 40.0f};
        press.pressed = true;
        pane->onPointer(press);
        ie::PointerEvent rel = press;
        rel.pressed = false;
        rel.released = true;
        pane->onPointer(rel);

        ie::KeyEvent down;
        down.pressed = true;
        down.scancode = ie::KeyDown;
        const int last = ie::kBlendModeCount - 1;
        for (int i = 0; i < last; ++i) host.dispatchKey(down);
        check((int)host.ed().doc.layers[0].blend == last, "blend 待选: ↓ 能一路走到末项（夹在末端）");
        for (int i = 0; i < 4; ++i) host.dispatchKey(down);
        check((int)host.ed().doc.layers[0].blend == last, "blend 待选: 到末项后继续按 ↓ 值不动");
        host.ed().undo();
        check((int)host.ed().doc.layers[0].blend == 0,
              "blend 待选: 末项空按不记历史（撤销一次回到点开之前，不用按四次）");
        check(!host.ed().hist.canUndo(), "blend 待选: 空按之后撤销栈里没有多余的空步");
    }

    {
        const std::string fp = textFontPath();
        if (!fp.empty()) {
            ie::Host host;
            host.ed().newDocument(220, 180, ie::Color(0, 0, 0, 0));
            ie::TextData tt;
            tt.text = "文字";
            tt.x = 40.0f;
            tt.y = 40.0f;
            tt.size = 32;
            host.ed().doc.addTextLayer(tt, "文字");
            host.ed().doc.refreshTextLayers();
            const float tx0 = host.ed().doc.layers.back().text.x;
            const float ty0 = host.ed().doc.layers.back().text.y;
            const int lx0 = host.ed().doc.layers.back().x;
            const int ly0 = host.ed().doc.layers.back().y;
            auto pane = ie::ui::makeLayerPanel(host);
            const ie::Rect area{0, 0, 290, 500};
            pane->place(area, area);
            ie::PointerEvent press;
            press.pos = ie::Vec2f{area.x + 70.0f, area.y + 40.0f};
            press.pressed = true;
            pane->onPointer(press);
            ie::PointerEvent rel = press;
            rel.pressed = false;
            rel.released = true;
            pane->onPointer(rel);
            ie::KeyEvent kd;
            kd.pressed = true;
            kd.scancode = ie::KeyDown;
            host.dispatchKey(kd);
            host.dispatchKey(kd);
            const ie::Layer& lt = host.ed().doc.layers.back();
            check(lt.text.x == tx0 && lt.text.y == ty0, "blend 待选: 文字层不被方向键挪位");
            check(lt.x == lx0 && lt.y == ly0, "blend 待选: 文字层图像偏移不动");
            ie::KeyEvent ku = kd;
            ku.pressed = false;
            ku.scancode = ie::KeyDown;
            host.dispatchKey(ku);
            host.dispatchKey(ku);
            check(host.ed().doc.layers.back().text.y == ty0,
                  "blend 待选: 松开方向键也不挪文字层（键起不能被工具当成一次移动）");
        }
    }

    {
        const std::string fp = textFontPath();
        if (!fp.empty()) {
            ie::Document doc;
            doc.createEmpty(160, 160, ie::Color(128, 128, 128, 255));
            ie::TextData td;
            td.text = "合";
            td.x = 30.0f;
            td.y = 30.0f;
            td.size = 56;
            td.color = ie::Color(230, 90, 60, 255);
            const int ti = doc.addTextLayer(td, "文字");
            doc.layers[(size_t)ti].style.strokeOn = true;
            doc.layers[(size_t)ti].style.strokeWidth = 3.0f;
            doc.refreshTextLayers();
            const ie::Image a = doc.flatten();
            doc.layers[(size_t)ti].blend = ie::BlendMode::Multiply;
            const ie::Image b = doc.flatten();
            long pixDiff = 0;
            for (int i = 0, n = 160 * 160; i < n; ++i) {
                if (a.empty() || b.empty()) break;
                if (a.pixels()[i] != b.pixels()[i]) ++pixDiff;
            }
            check(!a.empty() && !b.empty(), "blend 文字: 文字带描边仍能出图");
            const std::string msg = std::string("blend 文字: 带描边的文字层换混合模式后画面跟着变 (pix ") +
                                    std::to_string(pixDiff) + ")";
            check(pixDiff > 0, msg.c_str());
        }
    }

    {
        ie::Document doc;
        doc.createEmpty(16, 16, ie::Color(0, 0, 0, 255));
        ie::History hist;
        ie::Selection sel;
        sel.reset(16, 16);
        hist.reset(doc);
        hist.beginMerge();
        for (int i = 0; i < 5; ++i) {
            hist.record(doc, sel, "字体");
            doc.width += 1;
        }
        hist.endMerge();
        check(doc.width == 21, "merge: 段内改动都生效");
        hist.undo(doc, sel);
        check(doc.width == 16, "merge: 撤销一次回到整段之前（不是一次一格）");
        check(!hist.canUndo(), "merge: 整段只占一条撤销记录");
        hist.record(doc, sel, "普通一步");
        doc.width = 30;
        hist.undo(doc, sel);
        check(doc.width == 16, "merge: 段收口后普通操作仍是独立一步");
    }

    {
        ie::Editor ed;
        ed.newDocument(32, 32, ie::Color(255, 255, 255, 255));
        const size_t n0 = ed.hist.undoCount();

        ed.beginLayerOpacityEdit();
        ed.setLayerOpacity(ed.doc.active, ed.doc.layers[0].opacity);
        check(ed.hist.undoCount() == n0, "空步: 不透明度没变不留历史");
        ed.setLayerOpacity(ed.doc.active, 0.5f);
        ed.setLayerOpacity(ed.doc.active, 1.0f);
        ed.endEditStep();
        check(ed.hist.undoCount() == n0 + 1, "空步: 一段拖动只占一条（过程不各占一步）");
        ed.undo();
        check(ed.doc.layers[0].opacity == 1.0f, "空步: 撤销一次回到拖动前的值");
        const size_t nBase = ed.hist.undoCount();

        ed.applyBrightnessContrast(0.0f, 0.0f);
        check(ed.hist.undoCount() == nBase, "空步: 亮度对比度全 0（恒等）不留历史");
        ed.applyBrightnessContrast(20.0f, 0.0f);
        check(ed.hist.undoCount() == nBase + 1, "空步: 真改亮度才记一条");

        const size_t nTh = ed.hist.undoCount();
        ed.applyHueShift(0);
        check(ed.hist.undoCount() == nTh, "空步: 色相 0 度不留历史");
        ed.applyHueShift(60);
        check(ed.hist.undoCount() == nTh + 1 && ed.hist.undoLabel() == "色相",
              "色相: 转 60 度记一条、标签是「色相」");
        ed.applyThreshold(128);
        check(ed.hist.undoCount() == nTh + 2 && ed.hist.undoLabel() == "阈值",
              "阈值: 记一条、标签是「阈值」");

        {
            const size_t nPrev = ed.hist.undoCount();
            const uint32_t before = ed.doc.layers[0].img.pixels()[0];
            check(ed.beginPixelPreview("色相"), "预览会话: 拖滑块前能开预览会话");
            ed.previewPixels([](ie::Image& img, ie::Rect roi) { ie::blend::hueShift(img, roi, 90); });
            check(ed.hist.undoCount() == nPrev + 1, "预览会话: 预览一开就留一条待确认的历史");
            ed.commitPixelPreview();
            check(ed.hist.undoCount() == nPrev + 1 && ed.hist.undoLabel() == "色相",
                  "预览会话: 回车提交只留一条、标签是调整名");
            ed.undo();
            check(ed.doc.layers[0].img.pixels()[0] == before, "预览会话: 提交后撤销一次回到调整前");
            check(ed.beginPixelPreview("阈值"), "预览会话: 能再开一次预览");
            ed.previewPixels([](ie::Image& img, ie::Rect roi) { ie::blend::threshold(img, roi, 200); });
            ed.cancelPixelPreview();
            check(ed.doc.layers[0].img.pixels()[0] == before && ed.hist.undoCount() == nPrev,
                  "预览会话: Esc 取消还原像素并且不留历史");
        }

        const size_t n1 = ed.hist.undoCount();
        ed.resizeCanvas(ed.doc.width, ed.doc.height, 0, 0);        ed.resizeImage(ed.doc.width, ed.doc.height);
        check(ed.hist.undoCount() == n1, "空步: 画布/图像尺寸未变不留历史");

        ed.sel.selectAll();
        ed.cropToSelection();
        check(ed.hist.undoCount() == n1 && ed.doc.width == 32, "空步: 整画布选区裁切不做、不留历史");

        const size_t n2 = ed.hist.undoCount();
        ed.mergeImageLayer();
        check(ed.hist.undoCount() == n2 && ed.doc.layers.size() == 1,
              "空步: 单层无特效合并图像不做、不留历史");

    {
        Editor fe;
        fe.newDocument(60, 40, Color(0, 0, 0, 0));
        fe.doc.activeLayer()->img.detach();
        fe.doc.activeLayer()->img.fillRect(Rect{10, 10, 20, 20}, Color(255, 0, 0, 255).packed());
        LayerStyle& fs = fe.doc.layers[0].style;
        fs.strokeOn = true;
        fs.strokeWidth = 3.0f;
        fs.strokeColor[0] = 1.0f;
        fs.strokeColor[1] = 1.0f;
        fs.strokeColor[2] = 1.0f;
        fs.strokeColor[3] = 1.0f;
        fe.doc.markAllDirty();
        const size_t nf = fe.hist.undoCount();
        fe.mergeImageLayer();
        check(fe.doc.layers.size() == 1 && fe.doc.layers[0].kind == LayerKind::Pixel,
              "合并图像: 单层带描边也合成一张像素图");
        check(!ie::styleActive(fe.doc.layers[0].style), "合并图像: 结果层没有特效参数（只余像素）");
        check(fe.hist.undoCount() == nf + 1 && fe.hist.undoLabel() == "合并图像",
              "合并图像: 记一步撤销，标签是「合并图像」");

        fe.addEmptyLayer();
        const int top = fe.doc.active;
        fe.doc.layers[(size_t)top].img.detach();
        fe.doc.layers[(size_t)top].img.fillRect(Rect{30, 5, 10, 10}, Color(0, 255, 0, 255).packed());
        fe.doc.layers[(size_t)top].style.strokeOn = true;
        fe.doc.layers[(size_t)top].style.strokeWidth = 2.0f;
        fe.doc.markAllDirty();
        const size_t nTwo = fe.hist.undoCount();
        fe.mergeImageLayer();
        check(fe.doc.layers.size() == 2 && (int)fe.doc.active == top,
              "合并图像: 只合当前这一层，不向下合并、层数不变");
        check(!ie::styleActive(fe.doc.layers[(size_t)top].style) &&
                  !ie::styleActive(fe.doc.layers[0].style),
              "合并图像: 只清当前层的特效，另一层不动");
        check(fe.hist.undoCount() == nTwo + 1, "合并图像: 单层合并记一步撤销");
    }

        const size_t n3 = ed.hist.undoCount();
        ed.beginChildEdit();
        ed.applyLayerTri(0, 1.0f, 1.0f, 1.0f, false);
        ed.endEditStep();
        check(ed.hist.undoCount() == n3, "空步: 三色片段没改动不留历史");
        ed.beginChildEdit();
        ed.applyLayerTri(0, 1.4f, 1.0f, 1.0f, false);
        ed.applyLayerTri(0, 1.8f, 1.0f, 1.0f, false);
        ed.endEditStep();
        check(ed.hist.undoCount() == n3 + 1, "空步: 三色整段只占一条");

        const size_t n4 = ed.hist.undoCount();
        const ie::Layer& L = ed.doc.layers[0];
        ed.beginLayerStyleEdit();
        ed.applyLayerSettings(0, L.blend, L.opacity, L.lockAlpha, L.style);
        ed.endEditStep();
        check(ed.hist.undoCount() == n4, "空步: 图层样式参数没变不留历史");

        ed.beginChildEdit();
        ed.applyLayerTri(0, 2.0f, 1.0f, 1.0f, false);
        ed.undo();
        ed.beginChildEdit();
        ed.applyLayerTri(0, 0.5f, 1.0f, 1.0f, false);
        ed.endEditStep();
        check(ed.hist.canUndo() && ed.hist.undoLabel() == "三色", "空步: 撤销后新事务自己独立记一步");
    }

    {
        check(ie::Host::splitTrackValue(0, 180, 40, 0, 1360, 800) == 220, "分隔条: 左栏拖动按位移跟手");
        check(ie::Host::splitTrackValue(0, 180, -80, 0, 1360, 800) == 140, "分隔条: 左栏拖到下限就不再缩");
        check(ie::Host::splitTrackValue(1, 300, 40, 0, 1360, 800) == 260, "分隔条: 右栏向左拖变宽（方向相反）");
        check(ie::Host::splitTrackValue(1, 300, -400, 0, 1360, 800) <= 453, "分隔条: 右栏不超过视图 1/3");
        check(ie::Host::splitTrackValue(2, 300, 0, -120, 1360, 800) == 180, "分隔条: 右侧图像区不小于 140");
    }

    {
        ie::Image th(4, 1);
        th.pixels()[0] = ie::Color(100, 100, 100).packed();
        th.pixels()[1] = ie::Color(140, 140, 140).packed();
        ie::blend::threshold(th, ie::Rect{0, 0, 4, 1}, 128);
        check((th.pixels()[0] & 0xff) == 0 && (th.pixels()[1] & 0xff) == 255,
              "阈值: 低于 128 变黑、高于变白");
        ie::Image hue(1, 1);
        hue.pixels()[0] = ie::Color(255, 0, 0).packed();
        ie::blend::hueShift(hue, ie::Rect{0, 0, 1, 1}, 120);
        check((hue.pixels()[0] >> 8 & 0xff) == 255 && (hue.pixels()[0] & 0xff) == 0,
              "色相: 红色转 120° 得到绿色（通道顺序 BGR）");
    }



    {
        const std::vector<std::string> items = ie::ui::menuItemTitles("图层");
        const bool onlyListed = items.size() == 7 && items[0] == "合并图层" && items[1] == "合并图像" &&
                                items[2] == "图层旋转" && items[3] == "图层居中" &&
                                items[4] == "网络变形" && items[5] == "局部变形" && items[6] == "变形";
        check(onlyListed, "菜单: 图层是「合并图层 / 合并图像 / 图层旋转 / 图层居中 / 网络变形 / 局部变形 / 变形」");
        bool removed = true;
        for (const auto& s : items)
            if (s == "新建图层" || s == "复制图层" || s == "删除图层" || s == "向下合并" ||
                s == "拼合图像" || s == "上移一层" || s == "下移一层" || s == "合并选中图层" ||
                s == "混合选项")
                removed = false;
        check(removed, "菜单: 图层里的新建/复制/删除/向下合并/拼合/上移/下移/混合选项都已移除");

        const std::vector<std::string> img = ie::ui::menuItemTitles("图像");
        bool noLift = true;
        for (const auto& s : img)
            if (s == "衡影") noLift = false;
        bool hasRotate = false;
        for (const auto& s : img)
            if (s == "图像旋转") hasRotate = true;
        check(noLift && hasRotate && img.size() == 7,
              "菜单: 图像里多了「图像旋转」，没有「衡影」");
        bool noAdjust = true;
        for (const auto& s : img)
            if (s == "阈值" || s == "亮/对比" || s == "色相") noAdjust = false;
        check(noAdjust && img.back() == "去色",
              "菜单: 阈值/亮·对比/色相 已移出图像菜单（改到左侧「调整」面板），末项是「去色」");
        check(img[2] == "裁切透明",
              "菜单: 裁切只有「裁切透明」一条（裁切选区已并入裁剪工具）");
        const std::vector<std::string> file = ie::ui::menuItemTitles("文件");
        bool hasKeys = false;
        for (const auto& s : file)
            if (s == "快捷键") hasKeys = true;
        check(hasKeys, "菜单: 文件里有「快捷键」");
        bool noSettings = true;
        for (const auto& s : file)
            if (s == "设置") noSettings = false;
        check(noSettings, "菜单: 文件里没有「设置」（界面缩放已移除）");
        const std::vector<std::string> edit = ie::ui::menuItemTitles("编辑");
        bool noStrokeSel = true;
        for (const auto& s : edit)
            if (s == "描边选区") noStrokeSel = false;
        check(noStrokeSel, "菜单: 编辑里没有「描边选区」");
        std::vector<std::string> gone = {"撤销", "重做", "剪切", "复制", "粘贴", "删除", "内容识别填充"};
        bool noneLeft = true;
        for (const auto& s : edit)
            for (const auto& g : gone)
                if (s == g) noneLeft = false;
        check(noneLeft, "菜单: 编辑里只剩全选/取消选择/反选（撤销重做剪切复制粘贴删除填充都撤了）");
        check(edit.size() == 3, "菜单: 编辑恰好三项");
    }
    {
        ie::shortcuts::resetDefaults();
        check(ie::shortcuts::toolSlot(ie::KeyAct::TrimCanvas) == -1 &&
                  ie::shortcuts::resolve(ie::KeyR, ie::keymap::None) == ie::KeyAct::TrimCanvas,
              "快捷键: R 不再是「换工具」，而是直接跑裁切透明");
    }
    {
        ie::shortcuts::resetDefaults();
        check(ie::shortcuts::toolCombo(0) == "V", "快捷键: 悬停「移动」带 V");
        const int row = ie::shortcuts::rowIndex(ie::KeyAct::ToolMove, 0);
        ie::shortcuts::bindRow(row, ie::KeyX, ie::keymap::None);
        check(ie::shortcuts::toolCombo(0) == "X", "快捷键: 改绑后悬停文字跟着变（不再写死 V）");
        check(ie::shortcuts::toolCombo(11) == "U", "快捷键: 悬停「形状」带 U");
        ie::shortcuts::resetDefaults();
    }
    {
        ie::Editor ge;
        ge.newDocument(64, 64, ie::Color(255, 255, 255, 255));
        std::unique_ptr<ie::Tool> br = ie::makeBrushTool();
        ie::Props ps;
        br->props(ps, ge);
        bool hasColor = false;
        for (const ie::Prop& q : ps)
            if (q.kind == ie::PropKind::Color) hasColor = true;
        check(!hasColor, "画笔: 属性里没有取色（颜色只认左下色板）");
        const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        ge.palette.setFg(red);
        ge.sel.reset(ge.doc.width, ge.doc.height);
        ge.view.zoom = 1.0f;
        ge.view.panX = ge.view.panY = 0.0f;
        ge.view.vpX = ge.view.vpY = 0.0f;
        ge.view.vpW = (float)ge.doc.width;
        ge.view.vpH = (float)ge.doc.height;
        ie::InputFrame in;
        in.mouse = ie::Vec2f(32, 32);
        in.pressed[0] = true;
        br->onPress(ge, in, 0);
        in.pressed[0] = false;
        in.released[0] = true;
        br->onRelease(ge, in, 0);
        const ie::Rect d = ge.doc.takeDirty();
        ge.doc.recomposite(d);
        const ie::Color px = ie::Color::fromPacked(ge.doc.compositeImage().pixels()[(size_t)32 * 64 + 32]);
        check(px.r > 200 && px.g < 40 && px.b < 40, "画笔: 画出来的是色板前景色（红）");
    }
    {
        const std::vector<std::string> items = ie::ui::menuItemTitles("视图");
        check(!items.empty() && items[0] == "构图线", "菜单: 视图第一项是「构图线」");
    }

    {
        ie::shortcuts::resetDefaults();
        check(ie::shortcuts::resolve(ie::KeyZ, ie::keymap::Ctrl) == ie::KeyAct::Undo,
              "快捷键: 默认 Ctrl+Z 是撤销");
        const int undoRow = ie::shortcuts::rowIndex(ie::KeyAct::Undo, 0);
        ie::shortcuts::bindRow(undoRow, ie::KeyY, ie::keymap::Ctrl);
        check(ie::shortcuts::resolve(ie::KeyZ, ie::keymap::Ctrl) == ie::KeyAct::None &&
                  ie::shortcuts::resolve(ie::KeyY, ie::keymap::Ctrl) == ie::KeyAct::Undo,
              "快捷键: 改绑后旧组合失效、新组合生效");
        const int redoRow = ie::shortcuts::rowIndex(ie::KeyAct::Redo, 0);
        ie::shortcuts::bindRow(redoRow, ie::KeyY, ie::keymap::Ctrl);
        check(ie::shortcuts::resolve(ie::KeyY, ie::keymap::Ctrl) == ie::KeyAct::Redo &&
                  ie::shortcuts::rowCombo(ie::shortcuts::rows()[(size_t)undoRow]).empty(),
              "快捷键: 组合撞车时从原动作上摘掉，不重复占用");
        int key = 0;
        unsigned mods = 0;
        check(ie::shortcuts::parseCombo("Ctrl+Shift+S", key, mods) && key == ie::KeyS &&
                  mods == (ie::keymap::Ctrl | ie::keymap::Shift) &&
                  ie::shortcuts::comboText(key, mods) == "Ctrl+Shift+S",
              "快捷键: 组合文本可解析也可回写");
        check(ie::shortcuts::comboText(29, ie::keymap::Ctrl) == "Ctrl+2" &&
                  ie::shortcuts::keyHasName(29) && !ie::shortcuts::keyHasName(116),
              "快捷键: 数字键有名字、修饰键不算可绑按键");
        ie::shortcuts::resetDefaults();
        std::vector<ie::shortcuts::Row> draft = ie::shortcuts::rows();
        ie::shortcuts::bindRowIn(draft, 0, 29, ie::keymap::Ctrl);
        const ie::KeyAct live = ie::shortcuts::resolve(29, ie::keymap::Ctrl);
        ie::shortcuts::applyRows(draft);
        check(ie::shortcuts::resolve(29, ie::keymap::Ctrl) == ie::KeyAct::Undo && live != ie::KeyAct::Undo,
              "快捷键: 草稿改完点应用才生效（改前不影响正在用的绑定）");
        ie::shortcuts::resetDefaults();

        int toolRows = 0;
        for (const ie::shortcuts::Row& r : ie::shortcuts::rows())
            if (ie::shortcuts::toolSlot(r.act) >= 0) ++toolRows;
        check(toolRows == 12, "快捷键: 表里有 12 个工具各占一行");
        check(ie::shortcuts::resolve(ie::KeyV, ie::keymap::None) == ie::KeyAct::ToolMove,
              "快捷键: 裸 V 是移动");
        check(ie::shortcuts::resolve(ie::KeyM, ie::keymap::None) == ie::KeyAct::ToolSelect &&
                  ie::shortcuts::resolve(ie::KeyA, ie::keymap::None) == ie::KeyAct::ToolLasso &&
                  ie::shortcuts::resolve(ie::KeyW, ie::keymap::None) == ie::KeyAct::ToolWand,
              "快捷键: 选框 M / 套索 A / 魔棒 W");
        check(ie::shortcuts::resolve(ie::KeyE, ie::keymap::Shift) == ie::KeyAct::ToolEraser &&
                  ie::shortcuts::resolve(ie::KeyG, ie::keymap::None) == ie::KeyAct::ToolBucket &&
                  ie::shortcuts::resolve(ie::KeyG, ie::keymap::Shift) == ie::KeyAct::ToolGradient,
              "快捷键: 橡皮 Shift+E、油漆桶 G、渐变 Shift+G");
        check(ie::shortcuts::toolSlot(ie::KeyAct::ToolMask) == 7 &&
                  ie::shortcuts::resolve(ie::KeyP, ie::keymap::None) == ie::KeyAct::ToolMask,
              "快捷键: 抠图占第 8 个槽位、裸 P 进入抠图");
        check(ie::shortcuts::toolSlot(ie::KeyAct::ToolShape) == 11 &&
                  ie::shortcuts::rowIndex(ie::KeyAct::ToolText, 0) >= 0,
              "快捷键: 工具槽位与工具栏顺序对得上，且文字行在表里");
    }

    {
        ie::Host hl;
        hl.ed().newDocument(32, 32, ie::Color(255, 255, 255, 255));
        hl.ed().sel.selectAll();
        hl.setTool(2);
        ie::InputFrame in;
        const ie::Vec2f sp = hl.ed().view.toScreen(10.5f, 10.5f, 32, 32);
        in.mouse = ie::Vec2f(sp.x, sp.y);
        in.shift = true;
        hl.tool().onPress(hl.ed(), in, 0);
        ie::KeyEvent del;
        del.pressed = true;
        del.scancode = ie::KeyDelete;
        hl.dispatchKey(del);
        check(hl.ed().sel.active, "lasso: 正在画多边形时 Delete 抹锚点，不被图层面板抢去删内容");
        const ie::Color px = ie::Color::fromPacked(hl.ed().doc.flatten().pixels()[0]);
        check(px.r == 255 && px.g == 255 && px.b == 255, "lasso: Delete 抹锚点之后画面没被删掉");
    }

    {
        Editor ed;
        ed.newDocument(100, 80, Color(255, 255, 255, 255));
        auto tools = createTools();
        InputFrame in;
        CanvasNode node;
        node.setEditor(&ed);
        node.setTool(tools[1].get());
        node.setInputSource(&in);
        node.place(Rect{0, 0, 200, 200}, Rect{0, 0, 200, 200});
        ed.fitIfPending();

        const Vec2f sp = ed.view.toScreen(10.0f, 10.0f, ed.doc.width, ed.doc.height);
        in.mouse = Vec2f(sp.x, sp.y);
        in.pressed[0] = true;
        in.down[0] = true;
        PointerEvent down;
        down.pos = in.mouse;
        down.pressed = true;
        down.down = true;
        down.button = 0;
        check(node.onPointer(down), "拖出画布: 按下先由画布接住");

        in.pressed[0] = false;
        in.mouse = Vec2f(600.0f, 500.0f);
        PointerEvent move;
        move.pos = in.mouse;
        move.down = true;
        move.button = 0;
        check(node.onPointer(move), "拖出画布: 指针出画布矩形后画布仍接住拖动，不把事件丢掉");

        const float panBefore = ed.view.panX;
        node.autoScrollDrag(in);
        check(ed.view.panX < panBefore, "拖出画布: 指针停在右边界外，画布自己往右滚（像 PS 那样跟随）");

        const float panStill = ed.view.panX;
        const Vec2f keep = in.mouse;
        in.mouse = Vec2f(sp.x, sp.y);
        node.autoScrollDrag(in);
        check(ed.view.panX == panStill, "拖出画布: 指针回到画布里面就不再滚");
        in.mouse = keep;

        in.down[0] = false;
        in.released[0] = true;
        PointerEvent up;
        up.pos = in.mouse;
        up.released = true;
        up.button = 0;
        check(node.onPointer(up), "拖出画布: 松手事件同样还给画布");

        const Rect b = ed.sel.bounds();
        check(ed.sel.active && b.x == 10 && b.y == 10 && b.right() >= ed.doc.width && b.bottom() >= ed.doc.height,
              "拖出画布: 选区跟着拖到边界外，松手时才夹回画布");
    }

    {
        Editor e;
        e.newDocument(64, 64, Color(255, 255, 255, 255));
        auto tools = createTools();
        InputFrame in;
        const auto at = [&](float ix, float iy) { return e.view.toScreen(ix, iy, e.doc.width, e.doc.height); };

        const size_t n0 = e.hist.undoCount();
        in.mouse = at(10.0f, 10.0f);
        in.down[0] = in.pressed[0] = true;
        tools[1]->onPress(e, in, 0);
        in.pressed[0] = false;
        in.mouse = at(30.0f, 30.0f);
        tools[1]->onDrag(e, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        tools[1]->onRelease(e, in, 0);
        check(e.sel.active, "选区撤销: 拖出矩形选区");
        check(e.hist.undoCount() == n0 + 1, "选区撤销: 一次拖动只记一条（不是每帧一条）");
        e.undo();
        check(!e.sel.active, "选区撤销: 一次撤销把选区退回「没有选区」");
        e.redo();
        check(e.sel.active, "选区撤销: 重做把选区拿回来");
        const Rect marq = e.sel.bounds();

        const size_t n1 = e.hist.undoCount();
        in.mouse = at(5.0f, 5.0f);
        in.down[0] = in.pressed[0] = true;
        tools[2]->onPress(e, in, 0);
        in.pressed[0] = false;
        in.mouse = at(40.0f, 5.0f);
        tools[2]->onPress(e, in, 0);
        in.mouse = at(40.0f, 40.0f);
        tools[2]->onPress(e, in, 0);
        in.down[0] = false;
        in.key = KeyEnter;
        tools[2]->onKey(e, in);
        check(e.hist.undoCount() == n1 + 1, "选区撤销: 多边形套索完成记一条");
        const Rect lassoRect = e.sel.bounds();
        e.undo();
        check(e.sel.active && e.sel.bounds() == marq,
              (std::string("选区撤销: 撤销套索回到上一步的矩形选区 实际=") +
               std::to_string(e.sel.bounds().x) + "," + std::to_string(e.sel.bounds().y) + "," +
               std::to_string(e.sel.bounds().w) + "," + std::to_string(e.sel.bounds().h) + " active=" +
               std::to_string((int)e.sel.active)).c_str());

        const size_t n2 = e.hist.undoCount();
        in.mouse = at(50.0f, 50.0f);
        in.down[0] = in.pressed[0] = true;
        tools[3]->onPress(e, in, 0);
        in.pressed[0] = false;
        in.down[0] = false;
        in.released[0] = true;
        check(e.hist.undoCount() == n2 + 1, "选区撤销: 魔棒点一下记一条");
        const Rect wandRect = e.sel.bounds();
        e.undo();
        check(e.sel.active && e.sel.bounds() == marq, "选区撤销: 撤销魔棒回到上一步的矩形选区");
        (void)lassoRect;
        (void)wandRect;

        const size_t n3 = e.hist.undoCount();
        e.selectAll();
        check(e.hist.undoCount() == n3 + 1, "选区撤销: 全选记一条");
        e.undo();
        check(e.sel.active && e.sel.bounds() == marq, "选区撤销: 撤销全选回到原选区");
        e.deselect();
        check(!e.sel.active && e.hist.undoCount() == n3 + 1, "选区撤销: 取消选择也记一条");
        e.undo();
        check(e.sel.active, "选区撤销: 撤销取消选择把选区拿回来");
    }

    {
        ie::Host hca;
        hca.ed().newDocument(32, 32, ie::Color(255, 255, 255, 255));
        ie::Image bl(8, 8);
        bl.fillAll(0xffff0000u);
        hca.ed().doc.addLayerAt(std::move(bl), 4, 4, "B");

        auto lpOwn = ie::ui::makeLayerPanel(hca);
        ie::Node* lp = lpOwn.get();
        lp->place(ie::Rect{220, 0, 120, 200}, ie::Rect{220, 0, 120, 200});

        ie::KeyEvent ctrlA;
        ctrlA.pressed = true;
        ctrlA.ctrl = true;
        ctrlA.scancode = ie::KeyA;

        hca.input().mouse = ie::Vec2f(280.0f, 100.0f);
        check(lp->onKey(ctrlA) && hca.ed().doc.selectedIndices().size() > 1 && !hca.ed().sel.active,
              "Ctrl+A: 指针在图层面板上 = 全选图层，不动选区");

        hca.ed().doc.selIds.clear();
        hca.input().mouse = ie::Vec2f(10.0f, 10.0f);
        check(!lp->onKey(ctrlA), "Ctrl+A: 指针不在图层面板上时面板不抢这个键（让给全选画布选区）");

        hca.ed().selectAll();
        check(hca.ed().sel.active && hca.ed().sel.bounds().w == 32 && hca.ed().sel.bounds().h == 32,
              "Ctrl+A: 画布全选有选区边界（画布才画得出那圈虚线）");
    }

    {
        ie::Selection s;
        s.reset(40, 40);
        s.setRect(ie::Rect{5, 5, 20, 20});
        s.intersectRect(ie::Rect{15, 15, 20, 20});
        check(s.active && s.bounds() == ie::Rect{15, 15, 10, 10}, "选区交叉: 矩形只留重叠的那块");

        ie::Selection s2;
        s2.reset(40, 40);
        s2.setRect(ie::Rect{0, 0, 10, 10});
        s2.intersectRect(ie::Rect{20, 20, 10, 10});
        check(!s2.active, "选区交叉: 两块不挨着就没有选区");
    }

    for (int mi = 1; mi <= 3; ++mi) {
        ie::Editor e;
        e.newDocument(40, 40, ie::Color(255, 255, 255, 255));
        auto tools = createTools();
        ie::Tool& selTool = *tools[1];
        ie::Props pr;
        selTool.props(pr, e);
        for (auto& p : pr)
            if (p.kind == ie::PropKind::Seg && p.setV) p.setV((double)mi, p.setUd);

        e.sel.reset(40, 40);
        e.sel.setRect(ie::Rect{5, 5, 20, 20});

        const auto at = [&](float ix, float iy) { return e.view.toScreen(ix, iy, e.doc.width, e.doc.height); };
        ie::InputFrame in;
        in.mouse = at(30.0f, 30.0f);
        in.down[0] = in.pressed[0] = true;
        selTool.onPress(e, in, 0);
        in.pressed[0] = false;
        in.mouse = at(38.0f, 38.0f);
        selTool.onDrag(e, in, 0);
        in.down[0] = false;
        in.released[0] = true;
        selTool.onRelease(e, in, 0);

        const bool inBase = e.sel.contains(10, 10);
        const bool inNew = e.sel.contains(35, 35);
        if (mi == 1) {
            check(inBase && inNew, "选区模式: 添加到选区 = 旧块和新块都在");
        } else if (mi == 2) {
            check(inBase && !inNew, "选区模式: 从选区减去 = 旧块还在，新块挖掉");
        } else {
            check(!e.sel.active, "选区模式: 与选区交叉 = 两块不重叠就什么都不剩");
        }

    {
        ie::Editor e;
        e.newDocument(16, 16, ie::Color(255, 0, 0, 255));
        e.sel.reset(16, 16);
        e.sel.selectAll();
        const size_t n0 = e.hist.undoCount();
        check(e.beginMoveAt(8, 8, false), "移动+全选: 能起手");
        e.updateMove(3, 2, false);
        e.endMove();
        ie::Layer& l = e.doc.layers[0];
        const uint32_t kept = l.img.pixels()[0];
        check(l.x == 3 && l.y == 2, "移动+全选: 整层搬走（不按选区实时剪切）");
        check((kept >> 24) == 255, "移动+全选: 层内像素没被挖洞");
        check(e.hist.undoCount() == n0 + 1, "移动+全选: 一次拖动记一条撤销");

        ie::Editor e2;
        e2.newDocument(16, 16, ie::Color(255, 0, 0, 255));
        e2.sel.reset(16, 16);
        e2.sel.setRect(ie::Rect{2, 2, 4, 4});
        check(e2.beginMoveAt(3, 3, false), "移动+局部选区: 能起手");
        e2.updateMove(5, 5, false);
        e2.endMove();
        ie::Layer& l2 = e2.doc.layers[0];
        const uint32_t inSel = l2.img.pixels()[(size_t)2 * (size_t)l2.img.width() + (size_t)2];
        check(l2.x == 5 && l2.y == 5, "移动+局部选区: 也是整层搬走");
        check((inSel >> 24) == 255, "移动+局部选区: 选区不做实时剪切（像素还在层里）");

        ie::Editor e3;
        e3.newDocument(16, 16, ie::Color(255, 0, 0, 255));
        e3.sel.reset(16, 16);
        e3.sel.setRect(ie::Rect{4, 4, 4, 4});
        e3.cutSelection();
        ie::Layer& l3 = e3.doc.layers[0];
        const uint32_t cutOut = l3.img.pixels()[(size_t)(5 - l3.y) * (size_t)l3.img.width() + (size_t)(5 - l3.x)];
        check((cutOut >> 24) == 0, "剪切: 快捷键剪切才按选区挖走像素");
    }
    }

    {
        ie::Host hk;
        hk.ed().newDocument(32, 32, ie::Color(255, 255, 255, 255));
        ie::shortcuts::resetDefaults();
        ie::KeyEvent kv;
        kv.pressed = true;
        kv.scancode = ie::KeyV;
        hk.dispatchKey(kv);
        check(hk.activeToolIndex() == 0, "快捷键: 按 V 切到移动工具（走表，不是工具内部藏字符）");
        ie::KeyEvent km = kv;
        km.scancode = ie::KeyM;
        hk.dispatchKey(km);
        check(hk.toolId(hk.activeToolIndex()) == ie::ToolId::Select, "快捷键: 按 M 切到选框");
        const int selRow = ie::shortcuts::rowIndex(ie::KeyAct::ToolSelect, 0);
        ie::shortcuts::bindRow(selRow, ie::KeyF, ie::keymap::None);
        ie::KeyEvent kf = kv;
        kf.scancode = ie::KeyF;
        hk.dispatchKey(kf);
        check(hk.toolId(hk.activeToolIndex()) == ie::ToolId::Select, "快捷键: 改绑成 F 之后按 F 仍能切到选框");
        ie::KeyEvent km2 = kv;
        km2.scancode = ie::KeyM;
        hk.dispatchKey(km2);
        check(ie::shortcuts::resolve(ie::KeyM, ie::keymap::None) == ie::KeyAct::None &&
                  hk.toolId(hk.activeToolIndex()) == ie::ToolId::Select,
              "快捷键: 改绑后旧键 M 不再切工具（被摘干净，不残留）");
        ie::shortcuts::resetDefaults();
    }

    {
        ie::Editor se;
        se.newDocument(100, 100, ie::Color(0, 0, 0, 0));
        ie::Image b(10, 10);
        b.fillAll(0xff0000ffu);
        se.doc.addLayerAt(std::move(b), 44, 44, "B层");
        se.doc.active = 1;
        se.setActiveLayer(1);
        se.beginMove();
        const ie::Vec2i s = se.snapMoveDelta(-1, 0);
        check(s.x == 1 && s.y == 1, "吸附: 中心差 2px 与 1px 都吸到画布中线");
        check(se.snapGuideX() == 50.0f && se.snapGuideY() == 50.0f, "吸附: 两条参考线落在画布中线上");
        se.endMove();
        se.beginMove();
        const ie::Vec2i away = se.snapMoveDelta(-20, -20);
        check(away.x == -20 && away.y == -20 && se.snapGuideX() < 0.0f, "吸附: 超出 10px 不吸");
        se.endMove();
    }

    {
        ie::Editor re;
        re.newDocument(4, 3, ie::Color(0, 0, 0, 0));
        ie::Image src(2, 2);
        src.fillAll(0xff112233u);
        re.doc.addLayerAt(std::move(src), 0, 0, "块");
        re.doc.active = 1;
        re.setActiveLayer(1);
        re.rotateCanvas(0);
        check(re.doc.width == 4 && re.doc.height == 3 && re.doc.layers[1].x == 2 &&
                  re.doc.layers[1].y == 1 && re.doc.layers.size() == 2,
              "图像旋转: 180 度不动画布尺寸，层挪到对角位");
        re.undo();
        re.rotateCanvas(1);
        check(re.doc.width == 3 && re.doc.height == 4 && re.doc.layers[1].x == 1 &&
                  re.doc.layers[1].y == 0,
              "图像旋转: 向右 90 度交换画布宽高并换算层位置");
    }

    {
        ie::Editor le;
        le.newDocument(20, 20, ie::Color(0, 0, 0, 0));
        ie::Image block(4, 2);
        block.fillAll(0xffaa5500u);
        le.doc.addLayerAt(std::move(block), 8, 9, "层");
        le.doc.active = 1;
        le.setActiveLayer(1);
        const int cx = 8 + 2, cy = 9 + 1;
        le.rotateLayer(1);
        const ie::Layer& l = le.doc.layers[1];
        check(l.img.width() == 2 && l.img.height() == 4 && l.x + l.img.width() / 2 == cx &&
                  l.y + l.img.height() / 2 == cy,
              "图层旋转: 只转这一层，尺寸互换、中心不动、画布不变");
    }

    {
        ie::Selection sel;
        sel.reset(20, 20);
        sel.setRect(ie::Rect{2, 2, 4, 4});
        sel.translate(3, 1);
        const ie::Rect b = sel.bounds();
        check(sel.active && b.x == 5 && b.y == 3 && b.w == 4 && b.h == 4,
              "选区: 四向键平移把选区整块搬走");
    }

    {
        ie::shortcuts::resetDefaults();
        check(ie::shortcuts::resolve(ie::KeyBackspace, ie::keymap::None) == ie::KeyAct::None &&
                  ie::shortcuts::resolve(ie::KeyDelete, ie::keymap::None) == ie::KeyAct::Delete,
              "快捷键: 删除只剩 Delete，Backspace 已摘掉");
    }

    {
        ie::Editor ed;
        ed.newDocument(32, 32, ie::Color(255, 255, 255, 255));
        ie::Image b(16, 16);
        b.fillAll(0xffff0000u);
        ed.doc.addLayerAt(std::move(b), 8, 8, "B层");
        ed.doc.active = 1;
        ed.setActiveLayer(1);
        ed.toggleLayerSelection(0);
        check(ed.doc.selectedIndices().size() == 2, "画布多选: Ctrl+点第二个图层后两层都选中");
        ed.toggleLayerSelection(0);
        check(ed.doc.selectedIndices().size() == 1, "画布多选: 再点一次同一层就取消它");
        ed.toggleLayerSelection(0);
        check(ed.doc.selectedIndices().size() == 2, "画布多选: 再点回来又是两层");
        ed.mergeSelectedLayers();
        check(ed.doc.layers.size() == 1, "画布多选: 多选后「合并图层」真的合成一层");
    }

    {
        ie::Editor ed;
        ed.newDocument(40, 40, ie::Color(255, 255, 255, 255));
        ie::Image b(16, 16);
        b.fillAll(0xffff0000u);
        ed.doc.addLayerAt(std::move(b), 8, 8, "B层");
        ed.setActiveLayer(0);
        ed.toggleLayerSelection(1);
        check(ed.doc.selectedIndices().size() == 2 && ed.doc.active == 1,
              "Ctrl+点: 点中的那一层成为当前层且两层都在选区");
        const int bx = ed.doc.layers[1].x, by = ed.doc.layers[1].y;
        const int ax = ed.doc.layers[0].x, ay = ed.doc.layers[0].y;
        check(ed.beginMoveAt(bx + 4, by + 4, true), "Ctrl+点: 选中之后仍能抓起这一层");
        ed.updateMove(5, 3, false);
        check(ed.doc.layers[1].x == bx + 5 && ed.doc.layers[1].y == by + 3,
              "Ctrl+点: 拖动照常移动像素位置（选择不等于丢掉拖动）");
        check(ed.doc.layers[0].x == ax + 5 && ed.doc.layers[0].y == ay + 3,
              "Ctrl+点多选: 抓一层拖，整个选区一起走（不是只动一层）");
        check(ed.doc.selectedIndices().size() == 2, "Ctrl+点: 移动之后多选也没被收成单选");
        ed.endMove();
    }

    {
        ie::Editor ed;
        ed.newDocument(40, 40, ie::Color(255, 255, 255, 255));
        ie::Image b(16, 16);
        b.fillAll(0xffff0000u);
        ed.doc.addLayerAt(std::move(b), 8, 8, "B层");
        ed.setActiveLayer(0);
        ed.toggleLayerSelection(1);
        const int ax = ed.doc.layers[0].x, ay = ed.doc.layers[0].y;
        const int bx = ed.doc.layers[1].x, by = ed.doc.layers[1].y;
        check(ed.beginMoveAt(ax + 4, ay + 4, false), "多选: 按住选区内任一层都能抓起来");
        check(ed.doc.selectedIndices().size() == 2 && ed.doc.active == 0,
              "多选: 按住的那一层成为当前层，选区仍是两层");
        ed.updateMove(4, -2, false);
        check(ed.doc.layers[0].x == ax + 4 && ed.doc.layers[0].y == ay - 2 &&
                  ed.doc.layers[1].x == bx + 4 && ed.doc.layers[1].y == by - 2,
              "多选: 按住哪一层都整组一起移动（不分主副）");
        ed.endMove();
        check(ed.beginMoveAt(ax + 4, ay - 2 + 4, false), "多选: 单击前仍能进入按下态");
        ed.collapseMovePressTo(ed.movePressHit());
        check(ed.doc.selectedIndices().size() == 1 && ed.doc.active == 0,
              "多选: 按住没拖的单击把多选收成单击的那一层");
        ed.endMove();
    }

    {
        ie::Host host;
        host.ed().newDocument(32, 32, ie::Color(255, 255, 255, 255));
        check(!ie::ui::menuCmdEnabled(host, ie::Cmd::MergeSelected), "菜单: 单层时「合并图层」灰色不可用");
        host.ed().doc.addEmptyLayer("第二层");
        host.ed().toggleLayerSelection(0);
        check(ie::ui::menuCmdEnabled(host, ie::Cmd::MergeSelected), "菜单: 多选后「合并图层」变可用");

        const auto on = [&](ie::Cmd c) { return ie::ui::menuCmdEnabled(host, c); };
        host.ed().doc.active = 1;
        check(!on(ie::Cmd::MergeImage) && !on(ie::Cmd::LayerRotate) && !on(ie::Cmd::LayerCenter),
              "菜单: 空图层时 合并图像/图层旋转/图层居中 全灰");
        check(!on(ie::Cmd::MeshWarp) && !on(ie::Cmd::LocalWarp) && !on(ie::Cmd::FreeTransform),
              "菜单: 空图层时 网络变形/局部变形/变形 全灰");

        ie::TextData td;
        td.text = "A";
        td.size = 20;
        td.x = 6.0f;
        td.y = 6.0f;
        td.color = ie::Color(0, 0, 0, 255);
        const int tIdx = host.ed().doc.addTextLayer(td, "字");
        host.ed().doc.active = tIdx;
        check(on(ie::Cmd::MergeImage) && on(ie::Cmd::LayerRotate) && on(ie::Cmd::LayerCenter),
              "菜单: 文字层 合并图像/图层旋转/图层居中 可用");
        check(!on(ie::Cmd::MeshWarp) && !on(ie::Cmd::LocalWarp) && on(ie::Cmd::FreeTransform),
              "菜单: 文字层 网络变形/局部变形 灰，变形可用");

        host.ed().doc.active = 0;
        check(on(ie::Cmd::MergeImage) && on(ie::Cmd::LayerRotate) && on(ie::Cmd::LayerCenter) &&
                  on(ie::Cmd::MeshWarp) && on(ie::Cmd::LocalWarp) && on(ie::Cmd::FreeTransform),
              "菜单: 图像层 六项全部可用");
    }

    {
        ie::Editor ed;
        ed.newDocument(32, 32, ie::Color(255, 255, 255, 255));
        ie::LayerStyle st;
        st.strokeOn = true;
        st.strokeWidth = 999.0f;
        st.strokePos = ie::StrokePos::Outside;
        st.shadowOn = true;
        st.shadowDist = 500.0f;
        st.shadowSize = 500.0f;
        ed.applyLayerSettings(0, ie::BlendMode::Normal, 1.0f, false, st);
        const ie::LayerStyle& got = ed.doc.layers[0].style;
        check(got.strokeWidth == ie::kStyleStrokeWidthMax, "style: 描边宽度上限 50");
        check(got.shadowSize == ie::kStyleShadowBlurMax, "style: 投影模糊上限 50");
        check(got.shadowDist == ie::kStyleShadowDistMax, "style: 投影距离上限 100");
    }

    {
        const ie::ImeWindowForm f = ie::imeWindowForm(300, 176, 1, 24, 1280, 800);
        check(f.caretX == 300 && f.caretY == 176, "ime: 候选锚点与系统 caret 都取光标左上角");
        check(f.areaL == 300 && f.areaT == 176 && f.areaR == 301 && f.areaB == 200,
              "ime: 排除区就是光标矩形本身，不是整行也不是整窗");
        check(f.areaR <= 1280 && f.areaB <= 800, "ime: 排除区不越客户区");

        const ie::ImeWindowForm c = ie::imeWindowForm(5000, 900, 3, 30, 1280, 800);
        check(c.caretX == 1279 && c.caretY == 799 && c.areaR == 1280 && c.areaB == 800,
              "ime: 光标在客户区外时锚点与排除区夹回客户区");
    }

    if (!std::string(textFontPath()).empty()) {
        Editor ed;
        ed.newDocument(400, 300, Color(0, 0, 0, 0));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        auto ttool = makeTextTool();
        InputFrame in;
        in.mouse = Vec2f(120, 90);
        in.pressed[0] = true;
        ttool->onPress(ed, in, 0);
        if (char* buf = ttool->canvasTextBuffer()) {
            std::snprintf(buf, (size_t)ttool->canvasTextCapacity(), "文字");
            Rect got{};
            if (ttool->caretImageRect(ed, got)) {
                char im[200];
                std::snprintf(im, sizeof im, "ime: 光标矩形 x=%d y=%d w=%d h=%d（x 判 118..240，y 判 88..92）",
                              got.x, got.y, got.w, got.h);
                check(got.x >= 118 && got.x < 240, im);
                check(got.y >= 88 && got.y <= 92, im);
            } else {
                check(false, "ime: 文字会话内必须能报出光标矩形");
            }
            ed.requestTextCommit();
            ttool->update(ed, in);
        }
    }

    check(!std::string(textFontPath()).empty(), "text-box: 系统中文字体可用（下面两段的前置条件）");
    if (!std::string(textFontPath()).empty()) {
        Editor ed;
        ed.newDocument(400, 300, Color(0, 0, 0, 0));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = 400.0f;
        ed.view.vpH = 300.0f;
        auto tt = makeTextTool();
        ed.palette.fg[0] = 1.0f;
        ed.palette.fg[1] = 0.0f;
        ed.palette.fg[2] = 0.0f;
        ed.palette.fg[3] = 1.0f;
        InputFrame in;
        in.mouse = Vec2f(80, 70);
        in.pressed[0] = true;
        tt->onPress(ed, in, 0);
        if (char* buf = tt->canvasTextBuffer()) std::snprintf(buf, (size_t)tt->canvasTextCapacity(), "A");
        ed.requestTextCommit();
        in.pressed[0] = false;
        tt->update(ed, in);
        const int t1 = ed.doc.active;
        if (t1 >= 0 && ed.doc.layers[(size_t)t1].kind == LayerKind::Text) {
            const Color c1 = ed.doc.layers[(size_t)t1].text.color;
            check(c1.r == 255 && c1.g == 0 && c1.b == 0, "text-color: 新建文字取当前前景色（红）");
            ed.palette.fg[0] = 0.0f;
            ed.palette.fg[1] = 1.0f;
            InputFrame in2;
            in2.mouse = Vec2f(200, 180);
            in2.pressed[0] = true;
            tt->onPress(ed, in2, 0);
            if (char* buf = tt->canvasTextBuffer()) std::snprintf(buf, (size_t)tt->canvasTextCapacity(), "B");
            ed.requestTextCommit();
            in2.pressed[0] = false;
            tt->update(ed, in2);
            const int t2 = ed.doc.active;
            if (t2 >= 0 && ed.doc.layers[(size_t)t2].kind == LayerKind::Text) {
                const Color c2 = ed.doc.layers[(size_t)t2].text.color;
                check(c2.r == 0 && c2.g == 255 && c2.b == 0,
                      "text-color: 换前景色后下一段文字立刻取新色（绿），不沿用旧缓存");
                ed.palette.bg[0] = 0.0f;
                ed.palette.bg[1] = 0.0f;
                ed.palette.bg[2] = 1.0f;
                ed.palette.bg[3] = 1.0f;
                ed.swapPalette();
                InputFrame in3;
                in3.mouse = Vec2f(120, 220);
                tt->update(ed, in3);
                in3.pressed[0] = true;
                tt->onPress(ed, in3, 0);
                if (char* buf = tt->canvasTextBuffer()) std::snprintf(buf, (size_t)tt->canvasTextCapacity(), "C");
                ed.requestTextCommit();
                in3.pressed[0] = false;
                tt->update(ed, in3);
                const int t3 = ed.doc.active;
                if (t3 >= 0 && ed.doc.layers[(size_t)t3].kind == LayerKind::Text) {
                    const Color c3 = ed.doc.layers[(size_t)t3].text.color;
                    check(c3.r == 0 && c3.g == 0 && c3.b == 255,
                          "text-color: 点「切换」换前景后，第三段文字立刻取切换后的前景（蓝）");
                } else {
                    check(false, "text-color: 第三段文字没建起来");
                }
                ed.doc.active = t3;
                ed.palette.fg[0] = 1.0f;
                ed.palette.fg[1] = 0.0f;
                ed.palette.fg[2] = 0.0f;
                ed.palette.fg[3] = 1.0f;
                ed.palette.bg[0] = 0.0f;
                ed.palette.bg[1] = 0.0f;
                ed.palette.bg[2] = 1.0f;
                ed.palette.bg[3] = 1.0f;
                tt->onEnter(ed);
                ed.swapPalette();
                InputFrame in4;
                in4.mouse = Vec2f(0, 0);
                tt->update(ed, in4);
                ie::Props pr;
                tt->props(pr, ed);
                const float* cptr = nullptr;
                for (const auto& q : pr)
                    if (q.kind == ie::PropKind::Color) cptr = q.col;
                check(cptr == nullptr, "text-color: 文字属性里没有取色（颜色只认左下色板）");
                float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                ed.applyTextStyle(t3, ed.doc.layers[(size_t)t3].text.size,
                                  ed.doc.layers[(size_t)t3].text.lineSpacing,
                                  ed.doc.layers[(size_t)t3].text.opacity, red);
                ed.palette.fg[0] = 0.0f;
                ed.palette.fg[1] = 0.0f;
                ed.palette.fg[2] = 1.0f;
                ed.palette.fg[3] = 1.0f;
                ed.palette.bg[0] = 0.0f;
                ed.palette.bg[1] = 1.0f;
                ed.palette.bg[2] = 0.0f;
                ed.palette.bg[3] = 1.0f;
                tt->onEnter(ed);
                ed.swapPalette();
                InputFrame in5;
                in5.mouse = Vec2f(0, 0);
                tt->update(ed, in5);
                const Color c5 = ed.doc.layers[(size_t)t3].text.color;
                check(c5.r == 0 && c5.g == 255 && c5.b == 0,
                      "text-color: 编辑中的文字在点「切换」后立刻改成切换后的前景（绿）");
    if (!std::string(textFontPath()).empty()) {
        Editor ed2;
        ed2.newDocument(400, 300, Color(0, 0, 0, 0));
        ed2.sel.reset(ed2.doc.width, ed2.doc.height);
        ed2.view.zoom = 1.0f;
        ed2.view.vpX = ed2.view.vpY = 0.0f;
        ed2.view.vpW = 400.0f;
        ed2.view.vpH = 300.0f;
        auto tt2 = makeTextTool();
        InputFrame m1;
        m1.mouse = Vec2f(100, 80);
        m1.pressed[0] = true;
        tt2->onPress(ed2, m1, 0);
        if (char* buf = tt2->canvasTextBuffer()) std::snprintf(buf, (size_t)tt2->canvasTextCapacity(), "文字");
        m1.pressed[0] = false;
        tt2->update(ed2, m1);
        const int lid2 = ed2.doc.active;
        if (lid2 >= 0 && ed2.doc.layers[(size_t)lid2].kind == LayerKind::Text) {
            const TextData td0 = ed2.doc.layers[(size_t)lid2].text;
            const Rect box2 = textBoxRect(td0);
            InputFrame m2;
            m2.mouse = Vec2f((float)box2.x - 2.0f, (float)(box2.y + box2.h / 2));
            m2.pressed[0] = true;
            tt2->onPress(ed2, m2, 0);
            m2.pressed[0] = false;
            m2.mouse = Vec2f(m2.mouse.x + 32.0f, m2.mouse.y + 24.0f);
            tt2->onDrag(ed2, m2, 0);
            const TextData td1 = ed2.doc.layers[(size_t)lid2].text;
            check(std::abs(td1.x - td0.x - 32.0f) < 1.5f && std::abs(td1.y - td0.y - 24.0f) < 1.5f,
                  "text-move: 编辑中按住框线拖动，文字位置跟着走");
            check(tt2->canvasTextBuffer() != nullptr, "text-move: 拖完仍在编辑态，没有误提交");
        } else {
            check(false, "text-move: 文字会话没建起来");
        }
    }
            } else {
                check(false, "text-color: 第二段文字没建起来");
            }
        } else {
            check(false, "text-color: 第一段文字没建起来");
        }
    }
    if (!std::string(textFontPath()).empty()) {
        Editor ed;
        ed.newDocument(400, 300, Color(0, 0, 0, 0));
        TextData t;
        t.text = "文字";
        t.x = 60.0f;
        t.y = 50.0f;
        t.size = 48;
        const int ti = ed.addTextLayer(t);
        check(ti >= 0, "text-box: 真实建一个文字层");
        const Rect box = textBoxRect(ed.doc.layers[(size_t)ti].text);
        check(!box.empty(), "text-box: 文字层有自己的外接矩形");
        int bx = -1, by = -1;
        for (int y = box.y; y < box.y + box.h && bx < 0; ++y)
            for (int x = box.x; x < box.x + box.w && bx < 0; ++x) {
                const int li = ti;
                const uint32_t px = layerPixelAt(ed.doc.layers[(size_t)li], x, y);
                if ((px >> 24) == 0) { bx = x; by = y; }
            }
        check(bx >= 0, "text-box: 文字框内确实存在空白像素（这就是老实现抓不到的地方）");
        if (bx >= 0) {
            const int hit = ed.layerIndexAt(bx, by);
            check(hit == ti, "text-box: 框内空白处点击也命中这一层（矩形是实体，不是字形像素）");
            check(ed.layerIndexAt(box.x + box.w + 20, box.y + box.h + 20) < 0,
                  "text-box: 框外远处不命中，矩形没有无限外扩");
        }
    }

    if (!std::string(textFontPath()).empty()) {
        Editor ed;
        ed.newDocument(400, 300, Color(0, 0, 0, 0));
        ed.sel.reset(ed.doc.width, ed.doc.height);
        ed.view.zoom = 1.0f;
        ed.view.vpX = ed.view.vpY = 0.0f;
        ed.view.vpW = (float)ed.doc.width;
        ed.view.vpH = (float)ed.doc.height;
        TextData t;
        t.text = "文字";
        t.x = 100.0f;
        t.y = 100.0f;
        t.size = 48;
        ed.addTextLayer(t);
        int reqLayer = -1;
        Vec2f reqAt{};
        check(ed.requestTextEditAt(140, 120) && ed.takeTextEditRequest(reqLayer, reqAt) &&
                  reqLayer >= 0 && ed.doc.layers[(size_t)reqLayer].kind == LayerKind::Text,
              "text-dbl: 客户端坐标的双击也定位到文字实体");
        auto mtool = makeMoveTool();
        InputFrame in;
        in.mouse = Vec2f(140, 120);
        in.pressed[0] = true;
        in.doubleClick = true;
        mtool->onPress(ed, in, 0);
        reqLayer = -1;
        check(ed.takeTextEditRequest(reqLayer, reqAt) && reqLayer >= 0 &&
                  ed.doc.layers[(size_t)reqLayer].kind == LayerKind::Text,
              "text-dbl: 双击文字发出「进入编辑」的意图（不烘成像素）");
        check(!ed.textSessionActive(), "text-dbl: 意图本身不直接改文档，会话由工具自己开");
        auto ttool = makeTextTool();
        check(ttool->beginEditAt(ed, reqAt), "text-dbl: 文字工具在落点开会话成功");
        check(ed.textSessionActive() && ttool->canvasTextActive(),
              "text-dbl: 会话已开，键盘归文字工具");
        check(std::string(ttool->canvasTextBuffer()).size() > 0,
              "text-dbl: 缓冲区装的是这一段已有的文字（就地改，不是新建空层）");
        ed.requestTextCommit();
        ttool->update(ed, in);

        {
            Editor rx;
            rx.newDocument(400, 300, Color(0, 0, 0, 0));
            rx.sel.reset(rx.doc.width, rx.doc.height);
            rx.view.zoom = 1.0f;
            rx.view.vpX = rx.view.vpY = 0.0f;
            rx.view.vpW = (float)rx.doc.width;
            rx.view.vpH = (float)rx.doc.height;
            TextData rt;
            rt.text = "文字";
            rt.x = 100.0f;
            rt.y = 100.0f;
            rt.size = 48;
            const int rl = rx.addTextLayer(rt);
            const SkRect rb = rx.doc.layerDrawBounds(rl);
            rx.doc.layers[(size_t)rl].textXf =
                SkMatrix::RotateDeg(90.0f, SkPoint::Make(rb.centerX(), rb.centerY()));
            rx.doc.refreshTextLayers();
            const Rect rbox = textBoxRect(rx.doc.layers[(size_t)rl].text);
            const Vec2f localHit{(float)(rbox.x + rbox.w * 3 / 4), (float)(rbox.y + rbox.h / 2)};
            const SkPoint mapped = rx.doc.layers[(size_t)rl].textXf.mapXY(localHit.x, localHit.y);
            auto rtool = makeTextTool();
            const Vec2f atDoc{mapped.x(), mapped.y()};
            check(rtool->beginEditAt(rx, atDoc), "text-edit: 转过 90° 的字仍然能就地进入编辑（不再被挡）");
            check(rx.textSessionActive(), "text-edit: 屏幕上的字还是文字，没有变成像素");
            const int want = textOffsetAt(rx.doc.layers[(size_t)rl].text, localHit.x, localHit.y);
            Rect cr{};
            check(rtool->caretImageRect(rx, cr), "text-edit: 光标能报出自己的位置");
            const TextCaret wc = textCaretAt(rx.doc.layers[(size_t)rl].text, want);
            const SkPoint w0 = rx.doc.layers[(size_t)rl].textXf.mapXY(wc.x, wc.y);
            const SkPoint w1 = rx.doc.layers[(size_t)rl].textXf.mapXY(wc.x, wc.y + wc.h);
            const double mx = ((double)w0.x() + (double)w1.x()) * 0.5;
            const double my = ((double)w0.y() + (double)w1.y()) * 0.5;
            const double cx = (double)cr.x + (double)cr.w * 0.5;
            const double cy = (double)cr.y + (double)cr.h * 0.5;
            char cl[220];
            std::snprintf(cl, sizeof cl, "text-edit: 光标矩形中心=(%.1f,%.1f) 期望=(%.1f,%.1f)", cx, cy, mx, my);
            check(std::fabs(cx - mx) <= 3.0 && std::fabs(cy - my) <= 3.0, cl);
            check(want > 0 && want < (int)std::strlen(rx.doc.layers[(size_t)rl].text.text.c_str()),
                  "text-edit: 点选换算出的字符位置落在这一段文字中间（不是永远 0）");
            rx.requestTextCommit();
            rtool->update(rx, in);
        }
    }

    {
        Editor a;
        a.newDocument(64, 64, Color(255, 0, 0, 255));
        const int ax0 = a.doc.layers[0].x, ay0 = a.doc.layers[0].y;
        check(a.beginMove(), "tabdrop: 整层拖动会话已开");
        a.updateMove(20, 10, false);
        check(a.wholeLayerMoveActive(), "tabdrop: 拖动中走的是整层路径（跨标签复制只认这条）");
        check(a.doc.layers[0].x == ax0 + 20 && a.doc.layers[0].y == ay0 + 10,
              "tabdrop: 拖动期间位移实时写进图层原点");
        a.cancelMoveRestore();
        check(a.doc.layers[0].x == ax0 && a.doc.layers[0].y == ay0,
              "tabdrop: 拖到别的标签页后源图层回到原位（是复制不是搬走）");
        check(!a.hist.canUndo(), "tabdrop: 源文档的撤销栈里没留下这条位移");

        Editor b;
        b.newDocument(320, 200, Color(0, 0, 0, 0));
        const int before = (int)b.doc.layers.size();
        const int idx = b.importLayer(a.doc.layers[0]);
        check(idx >= 0 && (int)b.doc.layers.size() == before + 1, "tabdrop: 目标文档多出一层");
        check(idx == b.doc.active, "tabdrop: 置入的层成为目标文档的活动层");
        check(b.doc.layers[(size_t)idx].x == a.doc.layers[0].x &&
                  b.doc.layers[(size_t)idx].y == a.doc.layers[0].y,
              "tabdrop: 置入的图层保留自己的原点（不被拉到画布左上）");
        check(b.doc.layers[(size_t)idx].img.pixels() != nullptr, "tabdrop: 图层像素真的搬过来了");
        check(b.hist.canUndo(), "tabdrop: 置入记了一步历史（能撤销）");
    }

    {
        Editor c;
        c.newDocument(32, 32, Color(0, 0, 0, 255));
        c.addEmptyLayer();
        c.addEmptyLayer();
        const int topId = c.doc.layers.back().id;
        const int topIdx = (int)c.doc.layers.size() - 1;
        check(c.doc.active == topIdx, "layerdrag: 新建的层就在活动层上");
        c.moveSelectedLayers(0);
        check(c.doc.layers.front().id == topId, "layerdrag: 拖到底之后这一层真的在最底层");
        check(c.hist.canUndo(), "layerdrag: 排序记了一步历史");
    }

    {
        Host h;
        check(!h.layerGhostOn(), "ghost: 没拖的时候没有幽灵");
        h.setLayerDragGhost(Rect{10, 20, 200, 20}, std::string("很长的图层名字很长的图层名字"));
        check(h.layerGhostOn(), "ghost: 拖起来就有幽灵");
        check(h.layerGhostRect().w == 200 && h.layerGhostRect().x == 10, "ghost: 位置与宽度照面板给的原样");
        check(h.layerGhostText().size() > 8, "ghost: 幽灵写着层名");
        h.clearLayerDragGhost();
        check(!h.layerGhostOn(), "ghost: 松手就清掉");
    }

    std::printf("\n%s（失败 %d 项）\n", g_failures ? "自检未通过" : "全部自检通过", g_failures);
    return g_failures ? 1 : 0;
}

int runBench()
{
    using namespace ie;
    Editor e;
    e.newDocument(1600, 1000, Color(255, 255, 255, 255));
    e.doc.layers[0].img.detach();
    uint32_t* p = e.doc.layers[0].img.pixels();
    for (size_t i = 0; i < (size_t)1600 * 1000; ++i) p[i] = 0xff000000u | ((uint32_t)(i * 2654435761u) & 0x00ffffffu);
    e.view.vpW = 1400;
    e.view.vpH = 800;
    e.view.fit(1600, 1000);

    const int N = 60;
    double rebuild = 0, compose = 0, worst = 0;

    e.beginMove();
    for (int i = 0; i < N; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        e.updateMove(2 * i, i, false);
        const auto t1 = std::chrono::steady_clock::now();
        const Rect d = e.doc.takeDirty();
        if (!d.empty()) e.doc.recomposite(d);
        const auto t2 = std::chrono::steady_clock::now();

        const double ms = std::chrono::duration<double, std::milli>(t2 - t0).count();
        rebuild += std::chrono::duration<double, std::milli>(t1 - t0).count();
        compose += std::chrono::duration<double, std::milli>(t2 - t1).count();
        if (ms > worst) worst = ms;
    }
    e.endMove();

    Image a(1600, 1000), b(1600, 1000);
    b.fillAll(0xff804020u);
    const size_t n = (size_t)1600 * 1000;
    double memsetMs = 0, overMs = 0;
    for (int i = 0; i < 20; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        a.fillAll(0);
        auto t1 = std::chrono::steady_clock::now();
        blend::over(a.pixels(), b.pixels(), n, 1.0f);
        auto t2 = std::chrono::steady_clock::now();
        memsetMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        overMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
    }
    std::printf("micro: memset %.2f ms, over(1.6M px) %.2f ms, simd target %s\n", memsetMs / 20, overMs / 20,
                blend::activeSimdTarget());

    double scalarMs = 0;
    {
        a.fillAll(0);
        auto t0 = std::chrono::steady_clock::now();
        uint32_t* ap = a.pixels();
        const uint32_t* bp = b.pixels();
        for (size_t i = 0; i < n; ++i) ap[i] = blend::overPixel(ap[i], bp[i], 1.0f);
        auto t1 = std::chrono::steady_clock::now();
        scalarMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    std::printf("micro: scalar over %.2f ms\n", scalarMs);

    double opaqueMs = 0;
    {
        Image q(1600, 1000);
        q.fillAll(0xff804020u);
        int yesCount = 0;
        for (int i = 0; i < 20; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            if (blend::allOpaque(q.pixels(), n)) ++yesCount;
            const auto t1 = std::chrono::steady_clock::now();
            opaqueMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        }
        std::printf("micro: allOpaque(1.6M px 全不透明) %.2f ms (yes %d/20)\n", opaqueMs / 20, yesCount);
    }

    double rb2 = 0, cp2 = 0;
    {
        Editor e2;
        e2.newDocument(1600, 1000, Color(255, 255, 255, 255));
        e2.doc.addLayerAt(Image(1600, 1000), 0, 0, "上层");
        Layer& top = e2.doc.layers[1];
        top.img.detach();
        for (size_t i = 0; i < n; ++i) top.img.pixels()[i] = 0x80c03020u;
        e2.doc.markAllDirty();
        const Rect all2 = e2.doc.takeDirty();
        e2.doc.recomposite(all2);

        e2.beginMove();
        for (int i = 0; i < N; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            e2.updateMove(2 * i, i, false);
            const auto t1 = std::chrono::steady_clock::now();
            const Rect d = e2.doc.takeDirty();
            if (!d.empty()) e2.doc.recomposite(d);
            const auto t2 = std::chrono::steady_clock::now();
            rb2 += std::chrono::duration<double, std::milli>(t1 - t0).count();
            cp2 += std::chrono::duration<double, std::milli>(t2 - t1).count();
        }
        e2.endMove();
    }
    std::printf("scene B (bg + 50%% top layer): rebuild %.2f ms, compose %.2f ms\n", rb2 / N, cp2 / N);

    double tfRaster = 0, tfCompose = 0, tfWorst = 0, fullComposite = 0, rasterOnly = 0, rasterOnlyRot = 0,
           rasterOnlyFast = 0;
    int tfWorstFrame = 0;
    double tfWorstRaster = 0, tfWorstCompose = 0;
    int probeSink = 0;
    {
        Editor e3;
        e3.newDocument(1600, 1000, Color(255, 255, 255, 255));
        Image art(800, 600);
        art.detach();
        for (size_t i = 0; i < (size_t)800 * 600; ++i)
            art.pixels()[i] = 0xff000000u | ((uint32_t)(i * 2246822519u) & 0x00ffffffu);
        e3.doc.addLayerAt(std::move(art), 400, 200, "变换目标");
        e3.doc.active = 1;
        e3.doc.markAllDirty();
        e3.doc.takeDirty();
        e3.doc.recomposite(Rect{0, 0, 1600, 1000});

        {
            Image dst(1600, 1000);
            const auto t0 = std::chrono::steady_clock::now();
            e3.doc.compositeInto(dst.pixels(), 1600, Rect{0, 0, 1600, 1000}, -1);
            fullComposite = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }

        if (e3.beginFreeTransform(1)) {
            const float srcCx = 400.0f + 800.0f * 0.5f, srcCy = 200.0f + 600.0f * 0.5f;
            const float hw = 800.0f * 0.5f, hh = 600.0f * 0.5f;
            {
                const XForm probe{srcCx, srcCy, 1.3f, 1.3f, 0.0f};
                double only = 0;
                for (int i = 0; i < 10; ++i) {
                    int ox = 0, oy = 0;
                    const auto t0 = std::chrono::steady_clock::now();
                    Image probeIm = xformRasterize(e3.doc.layers[1].img, Rect{0, 0, 800, 600}, probe, 400, 200, ox, oy);
                    only += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    probeSink = probeIm.width() + ox + oy;
                }
                rasterOnly = only / 10.0;

                const XForm probeRot{srcCx, srcCy, 1.3f, 1.3f, 0.4f};
                double onlyRot = 0;
                for (int i = 0; i < 10; ++i) {
                    int ox = 0, oy = 0;
                    const auto t0 = std::chrono::steady_clock::now();
                    Image probeIm = xformRasterize(e3.doc.layers[1].img, Rect{0, 0, 800, 600}, probeRot, 400, 200, ox, oy);
                    onlyRot += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    probeSink = probeIm.width() + ox + oy;
                }
                rasterOnlyRot = onlyRot / 10.0;

                double onlyFast = 0;
                for (int i = 0; i < 10; ++i) {
                    int ox = 0, oy = 0;
                    const auto t0 = std::chrono::steady_clock::now();
                    Image probeIm =
                        xformRasterize(e3.doc.layers[1].img, Rect{0, 0, 800, 600}, probe, 400, 200, ox, oy, true);
                    onlyFast += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    probeSink = probeIm.width() + ox + oy;
                }
                rasterOnlyFast = onlyFast / 10.0;
            }
            e3.transformBeginGrab(3, Vec2f{srcCx + hw, srcCy + hh});
            for (int i = 1; i <= N; ++i) {
                const float s = 1.0f + 0.6f * (float)i / (float)N;
                const Vec2f cur{srcCx + hw * s, srcCy + hh * s};
                const auto t0 = std::chrono::steady_clock::now();
                e3.transformUpdateGrab(cur, false);
                const auto t1 = std::chrono::steady_clock::now();
                const Rect d = e3.doc.takeDirty();
                if (!d.empty()) e3.doc.recomposite(d);
                const auto t2 = std::chrono::steady_clock::now();
                const double raster = std::chrono::duration<double, std::milli>(t1 - t0).count();
                const double comp = std::chrono::duration<double, std::milli>(t2 - t1).count();
                tfRaster += raster;
                tfCompose += comp;
                if (raster + comp > tfWorst) {
                    tfWorst = raster + comp;
                    tfWorstFrame = i;
                }
                tfWorstRaster = std::max(tfWorstRaster, raster);
                tfWorstCompose = std::max(tfWorstCompose, comp);
            }
            e3.cancelFreeTransform();
        }
    }
    std::printf("scene C free-transform scale drag (800x600 -> 1.6x, %d frames)\n", N);
    std::printf("  rasterize avg %.2f ms/frame, compose avg %.2f ms/frame, worst %.2f ms (frame %d), full-canvas compose %.2f ms\n",
                tfRaster / N, tfCompose / N, tfWorst, tfWorstFrame, fullComposite);
    std::printf("  rasterize-only avg %.2f ms (1.3x, 800x600, 不含内容框/脏区) [%d]\n", rasterOnly, probeSink);
    std::printf("  rasterize-only avg %.2f ms (1.3x + 旋转 0.4rad，走 2D 采样)\n", rasterOnlyRot);
    std::printf("  rasterize worst %.2f ms, compose worst %.2f ms（分开看尖刺在哪一段）\n", tfWorstRaster,
                tfWorstCompose);
    std::printf("  rasterize-only avg %.2f ms (1.3x，拖动期快速路径：16.16 定点最近像素)\n", rasterOnlyFast);

    {
        Layer probe;
        probe.img.reset(1280, 800);
        probe.img.fillAll(0xffffffffu);
        probe.style.strokeOn = true;
        probe.style.strokeWidth = 12.0f;
        probe.style.strokePos = StrokePos::Outside;
        probe.style.shadowOn = true;
        probe.style.shadowDist = 20.0f;
        probe.style.shadowSize = 30.0f;
        int ox = 0, oy = 0;
        styledPixels(probe, ox, oy);
        double opMs = 0, opWorst = 0, shapeMs = 0;
        for (int i = 0; i < 20; ++i) {
            probe.style.strokeOpacity = 100.0f - (float)i;
            const auto t0 = std::chrono::steady_clock::now();
            styledPixels(probe, ox, oy);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            opMs += ms;
            opWorst = std::max(opWorst, ms);
        }
        for (int i = 0; i < 5; ++i) {
            probe.style.strokeWidth = 6.0f + (float)i;
            const auto t0 = std::chrono::steady_clock::now();
            styledPixels(probe, ox, oy);
            shapeMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        std::printf("style drag bench 1280x800 (描边 12 外部 + 投影 30)\n");
        std::printf("  opacity-only avg %.2f ms/move, worst %.2f\n", opMs / 20.0, opWorst);
        std::printf("  shape-change avg %.2f ms/move（掩码重建：膨胀 + 高斯模糊）\n", shapeMs / 5.0);
    }

    std::printf("move bench 1600x1000, %d frames\n", N);
    std::printf("  rebuild  avg %.2f ms/frame\n", rebuild / N);
    std::printf("  compose  avg %.2f ms/frame\n", compose / N);
    std::printf("  total    avg %.2f ms/frame, worst %.2f ms\n", (rebuild + compose) / N, worst);
    return 0;
}

#ifdef _WIN32
LONG WINAPI ieCrashLog(EXCEPTION_POINTERS* ep)
{
    const EXCEPTION_RECORD* er = ep ? ep->ExceptionRecord : nullptr;
    char mod[MAX_PATH] = {0};
    HMODULE m = nullptr;
    if (er && ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(er->ExceptionAddress), &m)) {
        ::GetModuleFileNameA(m, mod, MAX_PATH);
    }
    const unsigned long long base = reinterpret_cast<unsigned long long>(m);
    const unsigned long long rva = (base && er) ? reinterpret_cast<unsigned long long>(er->ExceptionAddress) - base : 0ull;
    const char* avKind = "none";
    unsigned long long avAddr = 0ull;
    if (er && er->ExceptionCode == 0xC0000005ul && er->NumberParameters >= 2) {
        avKind = er->ExceptionInformation[0] == 1 ? "write" : "read";
        avAddr = (unsigned long long)er->ExceptionInformation[1];
    }
    if (FILE* f = std::fopen("crash.log", "w")) {
        std::fprintf(f, "exception=0x%08lX address=%p base=0x%llX rva=0x%llX fault=%s@0x%llX module=%s\n",
                     er ? (unsigned long)er->ExceptionCode : 0ul, er ? er->ExceptionAddress : nullptr, base,
                     rva, avKind, avAddr, mod);
        std::fclose(f);
    }

    {
        const char* dmp = "ImageEditor.dmp";
        HANDLE h = ::CreateFileA(dmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId = ::GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), h, MiniDumpNormal,
                                ep ? &mei : nullptr, nullptr, nullptr);
            ::CloseHandle(h);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int runApp(int argc, char** argv)
{
    bool selftest = false;
    bool bench = false;
    std::string openPath;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--selftest") selftest = true;
        else if (a == "--bench") bench = true;
        else if (a == "--open" && i + 1 < argc) openPath = argv[++i];
    }

    if (selftest) {
        if (std::freopen("selftest.log", "w", stdout) == nullptr) return 2;
#ifdef _WIN32
        ::SetUnhandledExceptionFilter(ieCrashLog);
#endif
        const int rc = runSelfTest();
        std::fclose(stdout);
        return rc;
    }

    if (bench) {
        if (std::freopen("bench.log", "w", stdout) == nullptr) return 2;
        const int rc = runBench();
        std::fclose(stdout);
        return rc;
    }

#ifdef _WIN32
    ::SetUnhandledExceptionFilter(ieCrashLog);
#endif

    try {
        ie::Host host;
        if (!host.init()) {
#ifdef _WIN32
            ::MessageBoxA(nullptr, "初始化失败", "ImageEditor", MB_OK | MB_ICONERROR);
#endif
            return 1;
        }

        std::string::size_type beg = 0;
        while (beg < openPath.size()) {
            const std::string::size_type end = openPath.find(';', beg);
            const std::string one =
                openPath.substr(beg, end == std::string::npos ? std::string::npos : end - beg);
            if (!one.empty()) host.openFile(one);
            if (end == std::string::npos) break;
            beg = end + 1;
        }

        host.run();
    } catch (const std::exception& e) {
        if (FILE* f = std::fopen("crash.log", "w")) {
            std::fprintf(f, "cpp_exception: %s\n", e.what());
            std::fclose(f);
        }
        return 1;
    }
    return 0;
}

}

int main(int argc, char** argv) { return runApp(argc, argv); }

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return runApp(__argc, __argv); }
#endif
