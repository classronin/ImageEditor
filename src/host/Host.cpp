#include "host/Host.h"

#include "app/Editor.h"
#include "app/SavePath.h"
#include "host/GlobalKeys.h"
#include "host/Shortcuts.h"
#include "nodes/CanvasNode.h"
#include "nodes/Panels.h"
#include "nodes/Style.h"
#include "nodes/Widgets.h"
#include "dlgs/file_dialog.h"
#include "os/skia/skia_surface.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <imm.h>
#endif

namespace ie {

using namespace ui;

namespace {

class RootLayer : public Container {
public:
    Host* host = nullptr;
    CanvasNode* canvas = nullptr;
    Node* menu = nullptr;
    Node* tabbar = nullptr;
    Node* options = nullptr;
    Node* rail = nullptr;
    Node* props = nullptr;
    Node* layers = nullptr;
    Node* tri = nullptr;
    Node* status = nullptr;
    Node* overlay = nullptr;

protected:
    void arrange() override
    {
        const int W = frame_.w, H = frame_.h;
        int y = 0;
        if (menu) {
            const int h = menu->requestSize().h;
            placeChildAbs(menu, Rect{0, y, W, h});
            y += h;
        }

        const int top = y;
        const int statusH = status ? status->requestSize().h : 0;
        const int bottom = std::max(top, H - statusH);
        const int tabH = tabbar ? tabbar->requestSize().h : 0;

        const int splitT = Host::kSplitT;
        Host::PanelLayout ly =
            host ? host->computePanelRects(W, top, bottom, tabH) : Host::PanelLayout{};
        if (host) host->panelRects_ = ly;

        if (tabbar) placeChildAbs(tabbar, ly.tabbar);
        if (canvas) placeChildAbs(canvas, ly.canvas);
        if (rail) placeChildAbs(rail, ly.tools);
        if (options) {
            const Size os = options->requestSize();
            ly.options = host ? host->optionsRect(ly.canvas, os.w, os.h) : Rect{};
            placeChildAbs(options, ly.options);
        }
        if (tri) placeChildAbs(tri, ly.image);
        if (layers) placeChildAbs(layers, ly.layers);
        if (status) placeChildAbs(status, Rect{0, bottom, W, statusH});
        if (overlay) placeChildAbs(overlay, Rect{0, 0, W, H});
    }

    void paint(SkCanvas& c) override
    {
        if (host) {
            const Host::PanelLayout& ly = host->panelRects_;
            const uint32_t bar = IE_COL32(26, 27, 31, 255);
            const uint32_t hot = IE_COL32(58, 60, 68, 255);
            const uint32_t edge = IE_COL32(16, 17, 20, 255);
            const Rect bars[3] = {ly.splitLW, ly.splitRW, ly.splitRH};
            for (int i = 0; i < 3; ++i) {
                const Rect r = bars[i];
                if (r.w <= 0 || r.h <= 0) continue;
                const bool on = host->splitDrag() == i;
                SkPaint fill;
                fill.setColor(on ? hot : bar);
                c.drawRect(SkRect::MakeXYWH((float)r.x, (float)r.y, (float)r.w, (float)r.h), fill);
                SkPaint line;
                line.setColor(edge);
                if (i < 2) {
                    c.drawRect(SkRect::MakeXYWH((float)(r.x + r.w / 2), (float)r.y, 1.0f, (float)r.h), line);
                } else {
                    c.drawRect(SkRect::MakeXYWH((float)r.x, (float)(r.y + r.h / 2), (float)r.w, 1.0f), line);
                }
            }
        }
        Container::paint(c);
        if (host) {
            if (host->splitDrag() >= 0) {
                const Rect g = host->splitGhost();
                if (!g.empty()) {
                    const uint32_t glow = IE_COL32(72, 140, 240, 36);
                    const uint32_t glow2 = IE_COL32(72, 140, 240, 110);
                    const uint32_t core = IE_COL32(150, 195, 255, 255);
                    auto band = [&](float x, float y, float w, float h, uint32_t col) {
                        SkPaint p;
                        p.setColor(col);
                        c.drawRect(SkRect::MakeXYWH(x, y, w, h), p);
                    };
                    if (g.w < g.h) {
                        const float cx = (float)(g.x + g.w / 2);
                        band(cx - 7.0f, (float)g.y, 14.0f, (float)g.h, glow);
                        band(cx - 3.5f, (float)g.y, 7.0f, (float)g.h, glow2);
                        band(cx - 1.0f, (float)g.y, 2.0f, (float)g.h, core);
                    } else {
                        const float cy = (float)(g.y + g.h / 2);
                        band((float)g.x, cy - 7.0f, (float)g.w, 14.0f, glow);
                        band((float)g.x, cy - 3.5f, (float)g.w, 7.0f, glow2);
                        band((float)g.x, cy - 1.0f, (float)g.w, 2.0f, core);
                    }
                }
            }
            ui::paintHoverTip(c, *host);
            ui::paintDragGhost(c, *host);
        }
    }
};

std::filesystem::path exeDir()
{
#ifdef _WIN32
    constexpr DWORD kCap = 32768;
    wchar_t buf[kCap];
    const DWORD n = GetModuleFileNameW(nullptr, buf, kCap);
    if (n > 0 && n < kCap) return std::filesystem::path(std::wstring(buf, (size_t)n)).parent_path();
#endif
    return std::filesystem::current_path();
}

os::NativeCursor nativeCursor(CursorShape c)
{
    switch (c) {
        case CursorShape::IBeam: return os::NativeCursor::IBeam;
        case CursorShape::Cross: return os::NativeCursor::Crosshair;
        case CursorShape::Move: return os::NativeCursor::Move;
        case CursorShape::ResizeNS: return os::NativeCursor::SizeNS;
        case CursorShape::ResizeEW: return os::NativeCursor::SizeWE;
        case CursorShape::ResizeNWSE: return os::NativeCursor::SizeSE;
        case CursorShape::ResizeNESW: return os::NativeCursor::SizeNE;
        case CursorShape::Hand: return os::NativeCursor::Link;
        case CursorShape::None: return os::NativeCursor::Hidden;
        default: return os::NativeCursor::Arrow;
    }
}

int buttonIndex(os::Event::MouseButton b)
{
    switch (b) {
        case os::Event::LeftButton: return 0;
        case os::Event::RightButton: return 1;
        case os::Event::MiddleButton: return 2;
        default: return 0;
    }
}

}

Host::Host()
{
    tools_ = createTools();
    addEditor();
}

Host::~Host() = default;

bool Host::init()
{
    ui::initFonts();
    loadSettings();

    system_ = os::System::make();
    if (!system_) return false;
    system_->setAppName("ImageEditor");
    system_->setAppMode(os::AppMode::GUI);
    if (!createWindow()) return false;
    system_->finishLaunching();
    system_->activateApp();

    buildUI();
    tool().onEnter(ed());

    const Size v = viewSize();
    root_->place(Rect{0, 0, v.w, v.h}, Rect{0, 0, v.w, v.h});
    root_->layoutIfNeeded();
    ed().fitIfPending();
    canvas_->syncDocument();
    compositor_.invalidateAll(Rect{0, 0, v.w, v.h});
    firstFrame_ = true;
    tick();
    return true;
}

bool Host::createWindow()
{
    window_ = system_->makeWindow(settings.winW, settings.winH, 1);
    if (!window_) return false;
    window_->setTitle("ImageEditor");
    window_->setCursor(os::NativeCursor::Arrow);
#ifdef _WIN32
    const HWND hwnd = (HWND)window_->nativeHandle();
    const HMODULE exe = GetModuleHandle(nullptr);
    const HICON big = (HICON)LoadImage(exe, MAKEINTRESOURCE(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                       GetSystemMetrics(SM_CYICON), LR_SHARED);
    const HICON small = (HICON)LoadImage(exe, MAKEINTRESOURCE(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (big) SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
    if (small) SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
#endif
    applyScale();
    if (settings.winX >= 0 && settings.winY >= 0)
        window_->setFrame(gfx::Rect(settings.winX, settings.winY, settings.winW, settings.winH));
    if (settings.winMaximized) window_->maximize();
    window_->setVisible(true);
    return true;
}

void Host::applyScale()
{
    if (!window_) return;
    window_->setScale(1);
}


void Host::loadSettings()
{
    settingsPath_ = (exeDir() / "settings.ini").string();
    std::ifstream in(settingsPath_);
    if (!in) return;
    settingsFresh_ = false;
    std::string k;
    while (in >> k) {
        if (k == "scale") {
            int v = 0;
            in >> v;
        }
        else if (k == "winW") in >> settings.winW;
        else if (k == "winH") in >> settings.winH;
        else if (k == "winX") in >> settings.winX;
        else if (k == "winY") in >> settings.winY;
        else if (k == "maximized") {
            int v = 0;
            in >> v;
            settings.winMaximized = v != 0;
        } else if (k == "triFold") {
            int v = 0;
            in >> v;
        } else if (k == "triX") {
            in >> settings.triX;
        } else if (k == "triY") {
            in >> settings.triY;
        } else if (k == "triW") {
            in >> settings.triW;
        } else if (k == "triH") {
            in >> settings.triH;
        } else if (k == "optX") {
            in >> settings.optX;
        } else if (k == "optY") {
            in >> settings.optY;
        } else if (k == "colL") in >> settings.colL;
        else if (k == "colR") in >> settings.colR;
        else if (k == "lastDir") std::getline(in >> std::ws, settings.lastDir);
        else if (k == "toolsFrac") { in >> settings.toolsFrac; }
        else if (k == "optFrac") { float v = 0.0f; in >> v; }
        else if (k == "propsFrac") { in >> settings.propsFrac; }
        else if (k == "imgFrac") { in >> settings.imgFrac; }
        else if (k == "optH") { int v = 0; in >> v; }
        else if (k == "key") {
            std::string id, combo;
            int slot = 0;
            in >> id >> slot >> combo;
            const KeyAct act = shortcuts::actFromId(id);
            const int idx = shortcuts::rowIndex(act, slot);
            if (idx >= 0) {
                int key = 0;
                unsigned mods = 0;
                if (shortcuts::parseCombo(combo, key, mods)) shortcuts::bindRow(idx, key, mods);
            }
        } else {
            std::string skip;
            in >> skip;
        }
    }
    settings.winW = std::max(800, settings.winW);
    settings.winH = std::max(600, settings.winH);
}

void Host::saveSettings()
{
    if (settingsPath_.empty()) settingsPath_ = (exeDir() / "settings.ini").string();
    if (window_) {
        const bool maxed = window_->isMaximized();
        gfx::Rect rf = (maxed || window_->isMinimized()) ? window_->restoredFrame() : window_->frame();
        if (rf.w <= 0 || rf.h <= 0) rf = window_->frame();
        if (rf.w > 0 && rf.h > 0) {
            settings.winX = rf.x;
            settings.winY = rf.y;
            settings.winW = rf.w;
            settings.winH = rf.h;
        }
        settings.winMaximized = maxed;
    }
    std::filesystem::create_directories(std::filesystem::path(settingsPath_).parent_path());
    std::ofstream out(settingsPath_);
    if (!out) return;
    out << "winX " << settings.winX << "\n";
    out << "winY " << settings.winY << "\n";
    out << "winW " << settings.winW << "\n";
    out << "winH " << settings.winH << "\n";
    out << "maximized " << (settings.winMaximized ? 1 : 0) << "\n";
    out << "triX " << settings.triX << "\n";
    out << "triY " << settings.triY << "\n";
    out << "triW " << settings.triW << "\n";
    out << "triH " << settings.triH << "\n";
    out << "optX " << settings.optX << "\n";
    out << "optY " << settings.optY << "\n";
    out << "colL " << settings.colL << "\n";
    out << "colR " << settings.colR << "\n";
        out << "toolsFrac " << settings.toolsFrac << "\n";
        out << "propsFrac " << settings.propsFrac << "\n";
    out << "imgFrac " << settings.imgFrac << "\n";
    if (!settings.lastDir.empty()) out << "lastDir " << settings.lastDir << "\n";
    for (const shortcuts::Row& row : shortcuts::rows()) {
        const std::string combo = shortcuts::rowCombo(row);
        out << "key " << shortcuts::actionId(row.act) << " " << row.slot << " "
            << (combo.empty() ? "-" : combo) << "\n";
    }
}

Editor& Host::addEditor()
{
    tabs_.push_back(std::make_unique<Editor>());
    Editor& e = *tabs_.back();
    e.newDocument(1280, 800, Color(0, 0, 0, 0));
    e.title = "无标题";
    return e;
}

void Host::buildUI()
{
    auto root = std::make_unique<RootLayer>();
    root_ = std::move(root);
    RootLayer* r = static_cast<RootLayer*>(root_.get());
    r->host = this;

    auto canvas = std::make_unique<CanvasNode>();
    canvas_ = canvas.get();
    canvas_->setEditor(&ed());
    canvas_->setTool(&tool());
    canvas_->setInputSource(&input_);
    r->canvas = canvas_;
    r->add(std::move(canvas));

    auto menu = ui::makeMenuBar(*this);
    r->menu = menu.get();
    r->add(std::move(menu));

    auto tabs = ui::makeTabBar(*this);
    r->tabbar = tabs.get();
    r->add(std::move(tabs));

    auto options = ui::makeOptionsBar(*this);
    r->options = options.get();
    r->add(std::move(options));

    auto rail = ui::makeToolRail(*this);
    r->rail = rail.get();
    toolRail_ = rail.get();
    r->add(std::move(rail));


    auto layers = ui::makeLayerPanel(*this);
    r->layers = layers.get();
    layerPanel_ = layers.get();
    r->add(std::move(layers));


    auto status = ui::makeStatusBar(*this);
    r->status = status.get();
    status_ = status.get();
    r->add(std::move(status));

    auto tri = ui::makeImagePanel(*this);
    r->tri = tri.get();
    r->add(std::move(tri));

    chrome_ = {r->menu, r->tabbar, r->options, r->rail, r->layers, r->status, r->tri};

    auto overlay = std::make_unique<OverlayLayer>();
    overlay_ = overlay.get();
    r->overlay = overlay_;
    r->add(std::move(overlay));
}

void Host::markChromeDirty()
{
    for (Node* n : chrome_)
        if (n) n->markDirty(n->frame());
}

namespace {
Rect ghostDirty(const Rect& r)
{
    if (r.empty()) return r;
    return Rect::fromLTRB(r.x - 10, r.y - 10, r.right() + 10, r.bottom() + 10);
}
}

void Host::setLayerDragGhost(const Rect& r, std::string name)
{
    if (ghostOn_ && ghostRect_ == r && ghostText_ == name) return;
    Rect dirty = ghostDirty(r);
    if (ghostOn_) dirty = dirty.unite(ghostDirty(ghostRect_));
    ghostOn_ = true;
    ghostRect_ = r;
    ghostText_ = std::move(name);
    if (root_) root_->markDirty(dirty);
}

void Host::clearLayerDragGhost()
{
    if (!ghostOn_) return;
    ghostOn_ = false;
    ghostText_.clear();
    if (root_) root_->markDirty(ghostDirty(ghostRect_));
    ghostRect_ = Rect{};
}

void Host::relayout()
{
    if (!root_) return;
    const Size v = viewSize();
    root_->place(Rect{0, 0, v.w, v.h}, Rect{0, 0, v.w, v.h});
    root_->reflow();
    compositor_.invalidateAll(Rect{0, 0, v.w, v.h});
}


Size Host::viewSize() const
{
    if (window_) {
        if (os::Surface* s = window_->surface()) return Size{s->width(), s->height()};
        const gfx::Rect rc = window_->contentRect();
        return Size{rc.w, rc.h};
    }
    return Size{0, 0};
}

void Host::run()
{
    os::EventQueue* queue = system_->eventQueue();
    if (!queue) return;

    while (running_) {
        const bool anim = canvas_->wantsAnimation() || tool().animating();
        os::Event ev;
        queue->getEvent(ev, anim ? 1.0 / 60.0 : os::EventQueue::kWithoutTimeout);

        if (ev.type() == os::Event::None) {
            if (anim) { time_ += 1.0 / 60.0; tick(); }
            continue;
        }

        handleEvent(ev);

        while (running_) {
            os::Event more;
            queue->getEvent(more, 0.0);
            if (more.type() == os::Event::None) break;
            handleEvent(more);
        }

        if (running_) { time_ += 1.0 / 60.0; tick(); }
    }
    saveSettings();
}

ImeWindowForm imeWindowForm(int caretX, int lineTop, int lineW, int lineH, int clientW, int clientH){
    ImeWindowForm f;
    const int w = std::max(1, clientW);
    const int hgt = std::max(1, clientH);
    f.caretX = std::clamp(caretX, 0, w - 1);
    f.caretY = std::clamp(lineTop, 0, hgt - 1);
    const int right = std::clamp(f.caretX + std::max(1, lineW), f.caretX + 1, w);
    const int bottom = std::clamp(f.caretY + std::max(1, lineH), f.caretY + 1, hgt);
    f.areaL = f.caretX;
    f.areaT = f.caretY;
    f.areaR = right;
    f.areaB = bottom;
    return f;
}

void Host::syncTextInput()
{
    if (!system_ || !window_) return;

    Rect caret;
    const bool on = !tabs_.empty() && tool().caretImageRect(ed(), caret);

    if (!on) {
        if (imeOn_) {
            system_->setTextInput(false);
            imeOn_ = false;
            imeX_ = imeY_ = -1;
        }
#ifdef _WIN32
        if (imeCaretOwned_) { ::DestroyCaret(); imeCaretOwned_ = false; }
#endif
        return;
    }

    const Editor& e = ed();
    const Vec2f clientTop = e.view.toScreen((float)caret.x, (float)caret.y, e.doc.width, e.doc.height);
    const Vec2f clientBottom =
        e.view.toScreen((float)caret.x, (float)(caret.y + caret.h), e.doc.width, e.doc.height);
    const float lineH = std::max(1.0f, clientBottom.y - clientTop.y);

    const Size v = viewSize();
    const int cx = std::clamp((int)std::floor(clientTop.x), 2, std::max(2, v.w - 2));
    const int cy = std::clamp((int)std::floor(clientBottom.y), 2, std::max(2, v.h - 2));
    const gfx::Point screen =
        window_->pointToScreen(gfx::Point(cx, std::clamp((int)std::floor(clientTop.y), 2, std::max(2, v.h - 2))));

    const bool changed = !imeOn_ || screen.x != imeX_ || screen.y != imeY_;
    const bool composing = imeComposingNow();
    if (changed || composing) {
        if (changed) system_->setTextInput(true, screen);
        placeImeWindows(imeWindowForm((int)std::floor(clientTop.x), (int)std::floor(clientTop.y),
                                      (int)std::floor(clientBottom.x - clientTop.x),
                                      (int)std::floor(lineH), (int)v.w, (int)v.h));
        imeOn_ = true;
        imeX_ = screen.x;
        imeY_ = screen.y;
    }
    (void)cy;
}

void Host::placeImeWindows(const ImeWindowForm& f)
{
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(window_->nativeHandle());
    if (!hwnd) return;

    if (!imeCaretOwned_) {
        if (::CreateCaret(hwnd, nullptr, 1, 1)) imeCaretOwned_ = true;
    }
    if (imeCaretOwned_) {
        ::SetCaretPos(f.caretX, f.areaB);
    }

    HIMC imc = ::ImmGetContext(hwnd);
    if (!imc) return;

    LOGFONTW lf{};
    lf.lfHeight = -std::max(8, f.areaB - f.areaT);
    lf.lfWidth = 0;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    ::ImmSetCompositionFontW(imc, &lf);

    COMPOSITIONFORM comp{};
    comp.dwStyle = CFS_POINT;
    comp.ptCurrentPos.x = f.caretX;
    comp.ptCurrentPos.y = f.areaT;
    ::ImmSetCompositionWindow(imc, &comp);

    RECT rcClient{};
    ::GetClientRect(hwnd, &rcClient);
    CANDIDATEFORM ex{};
    ex.dwIndex = 0;
    ex.dwStyle = CFS_EXCLUDE;
    ex.ptCurrentPos.x = f.caretX;
    ex.ptCurrentPos.y = f.caretY;
    ex.rcArea.left = 0;
    ex.rcArea.top = f.areaT;
    ex.rcArea.right = rcClient.right > 0 ? rcClient.right : f.areaR;
    ex.rcArea.bottom = f.areaB;
    ::ImmSetCandidateWindow(imc, &ex);

    ::ImmReleaseContext(hwnd, imc);
#else
    (void)f;
#endif
}

bool Host::imeComposingNow()
{
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(window_->nativeHandle());
    if (!hwnd) return false;
    HIMC imc = ::ImmGetContext(hwnd);
    if (!imc) return false;
    const LONG len = ::ImmGetCompositionStringW(imc, GCS_COMPSTR, nullptr, 0);
    ::ImmReleaseContext(hwnd, imc);
    return len > 0;
#else
    return false;
#endif
}

void Host::tick()
{
    if (!window_ || !root_) return;

    drainPosts();

    syncTextInput();

    if (tabs_.empty()) return;

    float menuX = 0.0f, menuY = 0.0f;
    if (ed().takeCanvasMenuRequest(menuX, menuY)) openCanvasMenu(menuX, menuY);

    input_.dt = 1.0 / 60.0;
    ed().fitIfPending();
    canvas_->setEditor(&ed());
    canvas_->setTool(&tool());
    tool().update(ed(), input_);
    canvas_->syncDocument();
    canvas_->autoScrollDrag(input_);
    applyToolCursor();
    if (!tabs_.empty()) {
        const bool show = ed().meshWarpActive() || ed().localWarpActive();
        if (show != propsShown_) {
            propsShown_ = show;
            relayout();
        }
    }
    root_->layoutIfNeeded();
    canvas_->syncOverlay();

    {
        Editor& e = ed();
        const int z = zoomPercent();
        if (z != lastZoom_ || e.doc.width != lastDocW_ || e.doc.height != lastDocH_) {
            lastZoom_ = z;
            lastDocW_ = e.doc.width;
            lastDocH_ = e.doc.height;
            if (status_) status_->markDirty(status_->frame());
        }
        int lw = 0, lh = 0;
        const bool live = tool().liveSize(lw, lh);
        if (live != lastLiveOn_ || lw != lastLiveW_ || lh != lastLiveH_) {
            lastLiveOn_ = live;
            lastLiveW_ = lw;
            lastLiveH_ = lh;
            if (status_) status_->markDirty(status_->frame());
        }
    }

    {
        Editor& e = ed();
        const int sel = (int)e.doc.selIds.size();
        if (e.doc.structureRevision() != lastStructRev_ || e.doc.active != lastActiveLayer_ ||
            sel != lastSelCount_) {
            lastStructRev_ = e.doc.structureRevision();
            lastActiveLayer_ = e.doc.active;
            lastSelCount_ = sel;
            if (layerPanel_) layerPanel_->markDirty(layerPanel_->frame());
        }
    }

    dirtyScratch_.clear();
    root_->takeDirty(dirtyScratch_);
    for (const Rect& r : dirtyScratch_) compositor_.add(r);

    os::Surface* s = window_->surface();
    if (auto* surface = dynamic_cast<os::SkiaSurface*>(s)) {
        if (firstFrame_) {
            compositor_.invalidateAll(Rect{0, 0, surface->width(), surface->height()});
            firstFrame_ = false;
        }
        compositor_.present(window_.get(), surface, *root_);
    }


    for (int i = 0; i < 3; ++i) input_.pressed[i] = input_.released[i] = false;
    input_.doubleClick = false;
    input_.wheel = 0.0f;
    input_.keyPressed = false;
    input_.keyRepeat = false;
}

void Host::handleEvent(os::Event& ev)
{
    const bool ctrl = (int(ev.modifiers()) & int(os::kKeyCtrlModifier)) != 0;
    const bool shift = (int(ev.modifiers()) & int(os::kKeyShiftModifier)) != 0;
    const bool alt = (int(ev.modifiers()) & int(os::kKeyAltModifier)) != 0;

    switch (ev.type()) {
        case os::Event::CloseApp:
        case os::Event::CloseWindow:
            requestQuit();
            break;

        case os::Event::ResizeWindow: {
            relayout();
            break;
        }

        case os::Event::DropFiles:
            if (!ev.files().empty()) {
                const gfx::Point dp = ev.position();
                Node* hit = root_ ? root_->hit((float)dp.x, (float)dp.y) : nullptr;
                const bool handled = hit && hit->onDropFile(ev.files()[0], (float)dp.x, (float)dp.y);
                if (!handled) openFile(ev.files()[0]);
            }
            break;

        case os::Event::MouseEnter:
        case os::Event::MouseMove:
        case os::Event::MouseDown:
        case os::Event::MouseUp:
        case os::Event::MouseDoubleClick: {
            const gfx::Point p = ev.position();
            PointerEvent e;
            e.pos = Vec2f((float)p.x, (float)p.y);
            e.button = buttonIndex(ev.button());
            const bool isDown = ev.type() == os::Event::MouseDown ||
                                ev.type() == os::Event::MouseDoubleClick;
            e.pressed = isDown;
            e.released = ev.type() == os::Event::MouseUp;
            e.doubleClick = ev.type() == os::Event::MouseDoubleClick;
            e.down = isDown || (pointerDown_[e.button] && !e.released);
            e.ctrl = ctrl;
            e.shift = shift;
            e.alt = alt;
            if (e.doubleClick && e.button == 0 && canvas_ &&
                canvas_->frame().contains((int)e.pos.x, (int)e.pos.y))
                ed().requestTextEditAt(e.pos.x, e.pos.y);
            dispatchPointer(e);
            consumeTextEditRequest();
            break;
        }

        case os::Event::MouseWheel: {
            WheelEvent e;
            const gfx::Point p = ev.position();
            e.pos = Vec2f((float)p.x, (float)p.y);
            e.dy = -(float)ev.wheelDelta().y;
            e.ctrl = ctrl;
            e.shift = shift;
            e.alt = alt;
            input_.mouse = e.pos;
            input_.wheel = e.dy;

            if (overlay_ && overlay_->top()) {
                Node* top = overlay_->top();
                Node* hit = top->hit(e.pos.x, e.pos.y);
                if (hit) hit->onWheel(e);
                else top->onWheel(e);
                break;
            }
            Node* hit = root_->hit(e.pos.x, e.pos.y);
            if (hit) hit->onWheel(e);
            break;
        }

        case os::Event::KeyDown:
        case os::Event::KeyUp: {
            KeyEvent e;
            e.scancode = (int)ev.scancode();
            e.unicode = (int)ev.unicodeChar();
            e.pressed = ev.type() == os::Event::KeyDown;
            e.repeat = ev.repeat() > 0;
            e.ctrl = ctrl;
            e.shift = shift;
            e.alt = alt;
            if (e.scancode == KeySpace) spaceDown_ = e.pressed;
            dispatchKey(e);
            break;
        }

        default: break;
    }
}

void Host::dispatchPointer(const PointerEvent& e)
{
    input_.mouse = e.pos;
    lastMouse_ = e.pos;
    if (e.pressed) {
        input_.pressed[e.button] = true;
        input_.down[e.button] = true;
        pointerDown_[e.button] = true;
    }
    if (e.released) {
        input_.released[e.button] = true;
        input_.down[e.button] = false;
        pointerDown_[e.button] = false;
    }
    if (window_) {
        if (e.pressed) window_->captureMouse();
        else if (e.released && !pointerDown_[0] && !pointerDown_[1] && !pointerDown_[2])
            window_->releaseMouse();
    }
    input_.doubleClick = e.doubleClick;
    input_.ctrl = e.ctrl;
    input_.shift = e.shift;
    input_.alt = e.alt;
    input_.space = spaceDown_;

    if (colorPick_ && e.pressed) {
        if (e.button == 1) {
            cancelColorPick();
            return;
        }
        if (e.button == 0 && canvas_ && canvas_->frame().contains((int)e.pos.x, (int)e.pos.y)) {
            const Vec2i p = ed().canvasPixel(e.pos.x, e.pos.y);
            Color c;
            const bool got = ed().colorAtPixel(p.x, p.y, c);
            auto sink = colorPick_;
            cancelColorPick();
            if (got) {
                sink(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
            }
            return;
        }
    }


    if (!e.pressed && !e.released && !hoverHintAnchor_.empty() &&
        !hoverHintAnchor_.contains((int)e.pos.x, (int)e.pos.y))
        setHoverHint({});

    if (e.pressed) {
        for (Node* n : chrome_)
            if (n) n->cancelTransientEdit(e.pos.x, e.pos.y);
        if (Node* top = overlay_->top()) top->cancelTransientEdit(e.pos.x, e.pos.y);

        if (listStep_) {
            const bool inAnchor = listArmedRect_.contains((int)e.pos.x, (int)e.pos.y);
            const Rect pf = popupFrame();
            const bool inList = pf.w > 0 && pf.contains((int)e.pos.x, (int)e.pos.y);
            if (!inAnchor && !inList) disarmList();
        }
    } else if (!e.down && !e.released) {
        for (Node* n : chrome_)
            if (n) n->cancelTransientEdit(e.pos.x, e.pos.y);
    }

    if (ed().wholeLayerMoveActive() && pointerDown_[0]) {
        const int t = e.released ? -1 : tabIndexAtPoint(e.pos.x, e.pos.y);
        const int want = (t >= 0 && t != activeTab_) ? t : -1;
        if (want != dropTabTarget_) {
            dropTabTarget_ = want;
            markChromeDirty();
        }
    }
    if (e.released && dropTabTarget_ >= 0) {
        const int t = dropTabTarget_;
        dropTabTarget_ = -1;
        copyLayerToTab(t);
        markChromeDirty();
    }

    if (captured_) {
        Node* c = captured_;
        if (e.released) captured_ = nullptr;
        c->onPointer(e);
        return;
    }

    if (overlay_) {
        Node* top = overlay_->top();
        if (top) {
            if (hovered_ && hovered_ != top) {
                PointerEvent out = e;
                out.pos = Vec2f(-1000.0f, -1000.0f);
                out.pressed = out.released = out.down = false;
                hovered_->onPointer(out);
                hovered_ = nullptr;
            }
            const bool inside = top->frame().contains((int)e.pos.x, (int)e.pos.y);
            if (!e.pressed && !e.released && top->barMenuIndex() >= 0) {
                if (const RootLayer* rl = dynamic_cast<const RootLayer*>(root_.get())) {
                    if (rl->menu && rl->menu->frame().contains((int)e.pos.x, (int)e.pos.y)) {
                        hovered_ = rl->menu;
                        rl->menu->onPointer(e);
                        return;
                    }
                }
            }
            if (!inside && e.pressed) {
                const bool swallow = top->swallowsOutsidePress();
                closePopup();
                hovered_ = nullptr;
                if (swallow) return;
                const RootLayer* rl = dynamic_cast<const RootLayer*>(root_.get());
                const Vec2f p = e.pos;
                const bool onPopupBar = rl && ((rl->menu && rl->menu->frame().contains((int)p.x, (int)p.y)) ||
                                               (rl->options && rl->options->frame().contains((int)p.x, (int)p.y)));
                if (!onPopupBar) return;
            } else {
                Node* hitNode = top->hit(e.pos.x, e.pos.y);
                if (hitNode && hitNode != top) {
                    if (e.pressed && hitNode->focusable()) focusNode(hitNode);
                    if (hitNode->onPointer(e)) {
                        if (e.pressed && hitNode == canvasNode()) capturePointer(hitNode);
                        return;
                    }
                }
                if (top->onPointer(e)) {
                    if (e.pressed && top == canvasNode()) capturePointer(top);
                    return;
                }
            }
        }
    }

    if (splitDrag_ >= 0) {
        if (e.released) endSplitDrag();
        else if (e.down) dragSplitTo(e.pos);
        return;
    }
    if (e.pressed) {
        const int sp = splitHit(e.pos.x, e.pos.y);
        if (sp >= 0) {
            beginSplitDrag(sp);
            return;
        }
    }

    Node* hit = root_->hit(e.pos.x, e.pos.y);

    if (hit != hovered_) {
        if (hovered_) {
            PointerEvent out = e;
            out.pos = Vec2f(-1000.0f, -1000.0f);
            out.pressed = out.released = out.down = false;
            hovered_->onPointer(out);
        }
        hovered_ = hit;
    }

    if (hit) {
        if (e.pressed) focusNode(hit->focusable() ? hit : nullptr);
        if (hit->onPointer(e)) {
            if (e.pressed && hit == canvasNode()) capturePointer(hit);
            return;
        }
    }
}

static void lwKeyLog(const char* tag, int sc, bool ctrl)
{
    FILE* f = std::fopen("lwkeys.log", "a");
    if (!f) return;
    std::fprintf(f, "%s sc=%d ctrl=%d\n", tag, sc, ctrl ? 1 : 0);
    std::fclose(f);
}

bool Host::panelTyping() const
{
    for (Node* n : chrome_)
        if (n && n->editingText()) return true;
    return false;
}

void Host::dispatchKey(const KeyEvent& e)
{
    input_.key = e.scancode;
    input_.keyPressed = e.pressed;
    input_.keyRepeat = e.repeat;
    input_.ctrl = e.ctrl;
    input_.shift = e.shift;
    input_.alt = e.alt;
    input_.space = spaceDown_;

    if (!e.pressed && consumedKey_ == e.scancode) {
        consumedKey_ = -1;
        return;
    }

    if (e.pressed && e.scancode == KeyEsc && colorPick_) {
        cancelColorPick();
        return;
    }

    if (e.pressed && listStep_ && (e.scancode == KeyUp || e.scancode == KeyDown)) {
        consumedKey_ = e.scancode;
        listStep_(e.scancode == KeyUp ? -1 : 1);
        return;
    }

    if (e.pressed && e.scancode == KeyEsc) {
        if (popupOpen()) {
            closePopup();
            return;
        }
    }

    if (popupOpen()) {
        if (e.pressed) consumedKey_ = e.scancode;
        if (Node* top = overlay_->top()) top->onKey(e);
        return;
    }

    if (listStep_ && e.pressed && e.scancode == KeyEsc) {
        disarmList();
        return;
    }

    if (e.pressed && e.scancode == KeyEnter && canvas_ &&
        canvas_->frame().contains((int)lastMouse_.x, (int)lastMouse_.y) &&
        (ed().meshWarpActive() || ed().localWarpActive())) {
        for (Node* n : chrome_)
            if (n) n->cancelTransientEdit(lastMouse_.x, lastMouse_.y);
        consumedKey_ = e.scancode;
        if (ed().meshWarpActive())
            ed().commitMeshWarp();
        else if (ed().localWarpActive())
            ed().commitLocalWarp();
        if (canvas_) {
            canvas_->syncDocument();
            canvas_->syncOverlay();
        }
        return;
    }

    if (e.pressed && (e.scancode == KeyEnter || e.scancode == KeyEsc)) {
        if (!panelTyping()) {
            consumedKey_ = e.scancode;
            tool().onKey(ed(), input_);
            if (canvas_) {
                canvas_->syncDocument();
                canvas_->syncOverlay();
            }
            return;
        }
    }

    if (focus_) {
        const bool taken = focus_->onKey(e);
        if (taken) {
            if (e.pressed) consumedKey_ = e.scancode;
            return;
        }
    }

    bool lwPointerInside = false;
    if (canvas_ && ed().localWarpActive()) {
        const Vec2i dp = ed().canvasPixel(lastMouse_.x, lastMouse_.y);
        lwPointerInside = ed().localWarpContains(Vec2f{(float)dp.x, (float)dp.y});
    }
    if (e.pressed && lwPointerInside &&
        ((e.ctrl && (e.scancode == KeyC || e.scancode == KeyV)) || e.scancode == KeyDelete)) {
        lwKeyLog("host-gate", e.scancode, e.ctrl);
        consumedKey_ = e.scancode;
        tool().onKey(ed(), input_);
        if (canvas_) {
            canvas_->syncDocument();
            canvas_->syncOverlay();
        }
        return;
    }

    const bool toolClaimsKey = e.pressed && tool().consumesKey(e.scancode);
    if (!toolClaimsKey && layerPanel_ && layerPanel_->onKey(e)) return;

    {
        const KeyAct act = e.pressed ? resolveGlobalKey(e) : KeyAct::None;
        if (act != KeyAct::None) {
            const bool bare = (keymap::modsOf(e) & (keymap::Ctrl | keymap::Alt)) == 0;
            const bool cmdKeysAllowed = !panelTyping();
            if (bare && !cmdKeysAllowed) return;
            const int toolSlot = shortcuts::toolSlot(act);
            if (toolSlot >= 0) {
                setTool(toolSlot);
                return;
            }
            if (act == KeyAct::Delete && e.pressed && tool().consumesKey(e.scancode)) {
            } else {
                switch (act) {
                    case KeyAct::Undo: execute(Cmd::Undo); return;
                    case KeyAct::Redo: execute(Cmd::Redo); return;
                    case KeyAct::NewDoc: execute(Cmd::NewDoc); return;
                    case KeyAct::NewLayer: execute(Cmd::NewLayer); return;
                    case KeyAct::Transform: execute(Cmd::FreeTransform); return;
                    case KeyAct::TrimCanvas: execute(Cmd::TrimCanvas); return;
                    case KeyAct::CloseTab: promptCloseTab(activeTab_); return;
                    case KeyAct::NextTab: nextTab(); return;
                    case KeyAct::Open: execute(Cmd::Open); return;
                    case KeyAct::Save: execute(Cmd::Save); return;
                    case KeyAct::SaveAs: execute(Cmd::SaveAs); return;
                    case KeyAct::SelectAll: execute(Cmd::SelectAll); return;
                    case KeyAct::Deselect: execute(Cmd::Deselect); return;
                    case KeyAct::Copy: execute(Cmd::Copy); return;
                    case KeyAct::Cut: execute(Cmd::Cut); return;
                    case KeyAct::Paste: execute(Cmd::Paste); return;
                    case KeyAct::MergeSelected: execute(Cmd::MergeSelected); return;
                    case KeyAct::DuplicateLayer: execute(Cmd::DuplicateLayer); return;
                    case KeyAct::Delete: execute(Cmd::Delete); return;
                    case KeyAct::FitView: execute(Cmd::FitView); return;
                    case KeyAct::ActualPixels: execute(Cmd::ActualPixels); return;
                    case KeyAct::ZoomIn: execute(Cmd::ZoomIn); return;
                    case KeyAct::ZoomOut: execute(Cmd::ZoomOut); return;
                    default: break;
                }
            }
        }
    }

    const bool cmdKeysAllowed = !panelTyping();
    if (!cmdKeysAllowed) return;
    if (!e.pressed) return;
    consumedKey_ = e.scancode;
    tool().onKey(ed(), input_);
}

void Host::applyToolCursor()
{
    if (!window_) return;
    CursorShape c = CursorShape::Arrow;
    if (splitDrag_ >= 0) {
        c = splitDrag_ < 2 ? CursorShape::ResizeEW : CursorShape::ResizeNS;
    } else if (popupOpen()) {
        c = CursorShape::Arrow;
    } else if (splitHit(lastMouse_.x, lastMouse_.y) >= 0) {
        c = splitHit(lastMouse_.x, lastMouse_.y) < 2 ? CursorShape::ResizeEW : CursorShape::ResizeNS;
    } else if (root_ && root_->cursorAt(lastMouse_.x, lastMouse_.y, c)) {
        (void)c;
    } else {
        c = CursorShape::Arrow;
    }
    if (c == CursorShape::Rotate) {
        os::CursorRef cur = rotateCursor();
        if (cur && lastCursor_ != -2) {
            lastCursor_ = -2;
            customCursor_ = cur;
            window_->setCursor(cur);
        }
        return;
    }
    const auto native = nativeCursor(c);
    if ((int)native != lastCursor_) {
        lastCursor_ = (int)native;
        customCursor_ = nullptr;
        window_->setCursor(native);
    }
}

os::CursorRef Host::rotateCursor()
{
    if (customCursor_) return customCursor_;
    if (!system_) return nullptr;
    os::Ref<os::Surface> s = system_->makeSurface(24, 24);
    if (!s) return nullptr;
    s->lock();
    s->clear();
    const float cx = 12.0f, cy = 12.0f, r = 7.5f;
    const gfx::Color kInk = gfx::rgba(0, 0, 0, 255);
    const gfx::Color kPaper = gfx::rgba(255, 255, 255, 255);
    const auto arc = [&](float radius, float thick, gfx::Color col) {
        os::Paint p;
        p.style(os::Paint::Stroke);
        p.strokeWidth(thick);
        p.antialias(true);
        p.color(col);
        float px = 0, py = 0;
        bool first = true;
        for (float a = 200.0f; a <= 460.0f; a += 6.0f) {
            const float rad = a * 3.14159265f / 180.0f;
            const float x = cx + radius * std::cos(rad), y = cy + radius * std::sin(rad);
            if (!first) s->drawLine(px, py, x, y, p);
            px = x;
            py = y;
            first = false;
        }
    };
    arc(r, 1.0f, kInk);
    arc(r + 0.9f, 1.0f, kInk);
    arc(r - 0.9f, 1.0f, kInk);
    arc(r, 2.0f, kPaper);
    {
        const float rad = 460.0f * 3.14159265f / 180.0f;
        const float tx = cx + r * std::cos(rad), ty = cy + r * std::sin(rad);
        const float dx = -std::sin(rad), dy = std::cos(rad);
        os::Paint ink;
        ink.style(os::Paint::Stroke);
        ink.strokeWidth(1.0f);
        ink.antialias(true);
        ink.color(kInk);
        os::Paint paper = ink;
        paper.strokeWidth(2.0f);
        paper.color(kPaper);
        s->drawLine(tx, ty, tx + dx * 4.0f, ty + dy * 4.0f, ink);
        s->drawLine(tx, ty, tx + dx * 3.5f + dy * 3.0f, ty + dy * 3.5f - dx * 3.0f, ink);
        s->drawLine(tx, ty, tx + dx * 3.5f - dy * 3.0f, ty + dy * 3.5f + dx * 3.0f, ink);
        s->drawLine(tx, ty, tx + dx * 2.6f, ty + dy * 2.6f, paper);
    }
    s->unlock();
    os::CursorRef cur = system_->makeCursor(s.get(), gfx::Point(12, 12), 1);
    customCursor_ = cur;
    return cur;
}

void Host::consumeTextEditRequest()
{
    int idx = -1;
    Vec2f at{};
    if (!ed().takeTextEditRequest(idx, at)) return;
    int ti = -1;
    for (size_t i = 0; i < tools_.size() && ti < 0; ++i)
        if (tools_[i]->id() == ToolId::Text) ti = (int)i;
    if (ti < 0) return;
    setTool(ti);
    if (tool().beginEditAt(ed(), at)) markChromeDirty();
}

void Host::setTool(int index)
{
    if (index < 0 || index >= (int)tools_.size()) return;
    if (activeTool_ == index) return;
    if (ed().meshWarpActive()) ed().cancelMeshWarp();
    if (ed().localWarpActive()) ed().cancelLocalWarp();
    tools_[(size_t)activeTool_]->onLeave(ed());
    activeTool_ = index;
    tools_[(size_t)activeTool_]->onEnter(ed());
    if (canvas_) canvas_->setTool(&tool());
    markChromeDirty();
    relayout();
}

void Host::setHoverHint(std::string s, const Rect& anchor)
{
    const bool same = hoverHint_ == s && hoverHintAnchor_.x == anchor.x &&
                      hoverHintAnchor_.y == anchor.y && hoverHintAnchor_.w == anchor.w &&
                      hoverHintAnchor_.h == anchor.h;
    if (same) return;
    const Rect oldTip = ui::hoverTipRect(*this);
    hoverHint_ = std::move(s);
    hoverHintAnchor_ = anchor;
    const Rect newTip = ui::hoverTipRect(*this);
    if (!oldTip.empty()) compositor_.add(oldTip);
    if (!newTip.empty()) compositor_.add(newTip);
    if (status_) status_->markDirty(status_->frame());
}

void Host::selectTab(int index)
{
    if (index < 0 || index >= (int)tabs_.size() || index == activeTab_) return;
    tools_[(size_t)activeTool_]->onLeave(ed());
    activeTab_ = index;
    tool().onEnter(ed());
    afterTabChanged();
}

void Host::addTab()
{
    addEditor();
    activeTab_ = (int)tabs_.size() - 1;
    tool().onEnter(ed());
    afterTabChanged();
    relayout();
}

void Host::copyLayerToTab(int tab)
{
    if (tab < 0 || tab >= (int)tabs_.size() || tab == activeTab_) return;
    Editor& src = ed();
    if (src.doc.active < 0 || !src.wholeLayerMoveActive()) return;
    src.cancelMoveRestore();
    const Layer copy = src.doc.layers[(size_t)src.doc.active];
    post([this, tab, copy] {
        if (tab < 0 || tab >= (int)tabs_.size()) return;
        tabs_[(size_t)tab]->importLayer(copy);
        selectTab(tab);
        refreshCanvas();
    });
}

void Host::requestCloseTab(int index)
{
    if (index < 0 || index >= (int)tabs_.size()) return;
    if (tabs_.size() <= 1) return;
    if (tabs_[(size_t)index]->modified) return;

    const bool wasActive = (index == activeTab_);
    if (wasActive) tools_[(size_t)activeTool_]->onLeave(*tabs_[(size_t)index]);

    tabs_.erase(tabs_.begin() + index);
    if (tabs_.empty()) { running_ = false; return; }

    if (index < activeTab_) --activeTab_;
    else if (activeTab_ >= (int)tabs_.size()) activeTab_ = (int)tabs_.size() - 1;

    if (wasActive) tools_[(size_t)activeTool_]->onEnter(ed());
    afterTabChanged();
    relayout();
}

void Host::nextTab()
{
    if (tabs_.size() < 2) return;
    selectTab((activeTab_ + 1) % (int)tabs_.size());
}

void Host::afterTabChanged()
{
    canvas_->setEditor(&ed());
    canvas_->setTool(&tool());
    ed().fitIfPending();
    canvas_->syncDocument();
    compositor_.invalidateAll(Rect{0, 0, root_->frame().w, root_->frame().h});
    window_->setTitle(ed().title + " - ImageEditor");
}

int Host::zoomPercent() const
{
    return (int)(ed().view.zoom * 100.0f + 0.5f);
}

void Host::newTabCanvas(int w, int h)
{
    addTab();
    ed().newDocument(w, h, Color(255, 255, 255, 255));
    ed().pendingFit = true;
    ed().fitIfPending();
    canvas_->syncDocument();
    compositor_.invalidateAll(Rect{0, 0, root_->frame().w, root_->frame().h});
    window_->setTitle(ed().title + " - ImageEditor");
}

void Host::applyCanvasSize(int w, int h, int ax, int ay)
{
    ed().resizeCanvasAligned(w, h, ax, ay);
    refreshCanvas();
}

void Host::applyImageSize(int w, int h)
{
    ed().resizeImage(w, h);
    refreshCanvas();
}

void Host::applyStroke(int width, Color color)
{
    ed().strokeSelection(width, color);
    refreshCanvas();
}

bool Host::openFile(const std::string& path)
{
    for (int i = 0; i < (int)tabs_.size(); ++i) {
        if (tabs_[(size_t)i]->filePath == path) {
            selectTab(i);
            return true;
        }
    }

    if (ed().filePath.empty() && !ed().modified) {
        if (!ed().open(path)) return false;
        ed().pendingFit = true;
        ed().fitIfPending();
        canvas_->syncDocument();
        compositor_.invalidateAll(Rect{0, 0, root_->frame().w, root_->frame().h});
        window_->setTitle(ed().title + " - ImageEditor");
        return true;
    }

    addTab();
    if (!ed().open(path)) {
        tabs_.erase(tabs_.end() - 1);
        if (tabs_.empty()) {
            addTab();
        } else {
            activeTab_ = (int)tabs_.size() - 1;
            tool().onEnter(ed());
            afterTabChanged();
            relayout();
        }
        return false;
    }
    ed().pendingFit = true;
    ed().fitIfPending();
    canvas_->syncDocument();
    compositor_.invalidateAll(Rect{0, 0, root_->frame().w, root_->frame().h});
    window_->setTitle(ed().title + " - ImageEditor");
    return true;
}

void Host::placeImageLayer(const std::string& path)
{
    ed().placeImageAsLayer(path);
    canvas_->syncDocument();
    compositor_.invalidateAll(Rect{0, 0, root_->frame().w, root_->frame().h});
    relayout();
}

void Host::openProjectFile()
{
    dlgs::FileDialogRef dlg = dlgs::FileDialog::make({});
    if (!dlg) return;
    dlg->setType(dlgs::FileDialog::Type::OpenFile);
    dlg->setTitle("打开图像");
    for (const char* ext : {"png", "jpg", "jpeg", "webp", "gif", "tif", "tiff", "ora"})
        dlg->addFilter(ext, ext);
    void* hwnd = window_->nativeHandle();
    if (dlg->show(hwnd) == dlgs::FileDialog::Result::OK) openFile(dlg->fileName());
}

void Host::saveDialog(int closeTabAfter, const std::string& initialPath)
{
    openDialogNode(ui::makeSaveDialog(*this, closeTabAfter, initialPath));
}

void Host::promptCloseTab(int index)
{
    if (index < 0 || index >= (int)tabs_.size()) return;
    if (tabs_.size() > 1 && tabs_[(size_t)index]->modified)
        openDialogNode(ui::makeCloseDialog(*this, index));
    else
        requestCloseTab(index);
}

int Host::unsavedTabCount() const
{
    int n = 0;
    for (const std::unique_ptr<Editor>& t : tabs_)
        if (t && t->modified) ++n;
    return n;
}

void Host::requestQuit()
{
    if (unsavedTabCount() == 0) {
        running_ = false;
        return;
    }
    openDialogNode(ui::makeQuitDialog(*this));
}

void Host::quitNoSave()
{
    for (std::unique_ptr<Editor>& t : tabs_)
        if (t) t->modified = false;
    running_ = false;
}

void Host::quitSaveAll()
{
    for (int i = 0; i < (int)tabs_.size(); ++i) {
        Editor& t = *tabs_[(size_t)i];
        if (!t.modified) continue;
        if (t.filePath.empty()) {
            selectTab(i);
            saveDialog(-2);
            return;
        }
        if (!t.saveTo(t.filePath)) {
            selectTab(i);
            return;
        }
    }
    running_ = false;
}

void Host::continueQuitAfterSave() { quitSaveAll(); }

void Host::closeTabNoSave(int index)
{
    if (index < 0 || index >= (int)tabs_.size()) return;
    tabs_[(size_t)index]->modified = false;
    requestCloseTab(index);
}

void Host::saveAndCloseTab(int index)
{
    if (index < 0 || index >= (int)tabs_.size()) return;
    selectTab(index);
    if (!ed().filePath.empty()) {
        if (ed().saveTo(ed().filePath)) requestCloseTab(index);
        return;
    }
    saveDialog(index);
}

std::string Host::defaultSaveDir() const
{
    const std::string& fp = ed().filePath;
    if (!fp.empty()) return dirNameOf(fp);
    if (!settings.lastDir.empty()) return settings.lastDir;
    const char* home = std::getenv("USERPROFILE");
    return home ? std::string(home) + "\\Pictures" : std::string(".");
}

std::string Host::defaultSaveName() const
{
    return ed().title.empty() ? "untitled" : ed().title;
}

void Host::browseSaveDir(std::string& dir)
{
    dlgs::FileDialogRef dlg = dlgs::FileDialog::make({});
    if (!dlg) return;
    dlg->setType(dlgs::FileDialog::Type::OpenFolder);
    dlg->setTitle("选择输出目录");
    const std::string start = dir.empty() ? settings.lastDir : dir;
    if (!start.empty()) dlg->setFileName(start + "\\_");
    void* hwnd = window_->nativeHandle();
    if (dlg->show(hwnd) == dlgs::FileDialog::Result::OK) {
        dir = dlg->fileName();
        if (!dir.empty() && dir != settings.lastDir) {
            settings.lastDir = dir;
            saveSettings();
        }
    }
}

bool Host::saveTo(const std::string& path)
{
    return saveTo(path, ed().saveOpts);
}

bool Host::saveTo(const std::string& path, const imageio::SaveOptions& opt)
{
    if (!ed().saveTo(path, opt)) {
        compositor_.invalidateAll(Rect{0, 0, root_->frame().w, root_->frame().h});
        return false;
    }
    window_->setTitle(ed().title + " - ImageEditor");
    compositor_.invalidateAll(Rect{0, 0, root_->frame().w, root_->frame().h});
    return true;
}

void Host::saveProjectFile(bool saveAs)
{
    const std::string init = (!saveAs && !ed().filePath.empty()) ? ed().filePath : std::string();
    saveDialog(-1, init);
}

void Host::openDialog()
{
    openDialogNode(ui::makeNewDialog(*this));
}

void Host::openDialogNode(std::unique_ptr<Node> dialog)
{
    popPopup();
    pushDialogNode(std::move(dialog));
}

void Host::pushDialogNode(std::unique_ptr<Node> dialog)
{
    if (!overlay_) return;
    disarmList();
    Node* raw = dialog.get();
    overlay_->addFill(std::move(dialog));
    if (raw->focusable()) focusNode(raw);
    relayout();
}

bool Host::popPopup()
{
    Node* top = overlay_ ? overlay_->top() : nullptr;
    if (!top) return false;
    if (captured_ == top) captured_ = nullptr;
    const bool wasFocused = (focus_ == top);
    if (wasFocused) { top->setFocus(false); focus_ = nullptr; }
    overlay_->pop();
    if (wasFocused) {
        Node* nowTop = overlay_ ? overlay_->top() : nullptr;
        if (nowTop && nowTop->focusable()) focusNode(nowTop);
    }
    return true;
}

void Host::closePopup()
{
    ed().cancelPixelPreview();
    if (popPopup()) relayout();
}

void Host::focusNode(Node* n)
{
    if (focus_ == n) return;
    if (focus_) focus_->setFocus(false);
    focus_ = n;
    if (n) n->setFocus(true);
}

bool Host::popupOpen() const
{
    return overlay_ && overlay_->top() != nullptr;
}

Rect Host::popupFrame() const
{
    const Node* top = overlay_ ? overlay_->top() : nullptr;
    return top ? top->frame() : Rect{};
}

void Host::armList(const Rect& anchor, std::function<void(int)> step)
{
    if (listStep_) ed().hist.endMerge();
    ed().hist.beginMerge();
    listArmedRect_ = anchor;
    listStep_ = std::move(step);
}

void Host::disarmList()
{
    if (!listStep_) return;
    listStep_ = nullptr;
    listArmedRect_ = Rect{};
    ed().hist.endMerge();
    markChromeDirty();
}

bool Host::listArmed(const Rect& r) const
{
    return listStep_ && r.x == listArmedRect_.x && r.y == listArmedRect_.y &&
           r.w == listArmedRect_.w && r.h == listArmedRect_.h;
}

int Host::barMenuIndex() const
{
    const Node* top = (overlay_ ? overlay_->top() : nullptr);
    return top ? top->barMenuIndex() : -1;
}

void Host::post(std::function<void()> fn)
{
    if (fn) posted_.push_back(std::move(fn));
}

void Host::drainPosts()
{
    if (drainingPosts_ || posted_.empty()) return;
    drainingPosts_ = true;
    std::vector<std::function<void()>> batch;
    batch.swap(posted_);
    for (auto& fn : batch) fn();
    drainingPosts_ = false;
}

void Host::openMenu(int menuIndex, float x, float y)
{
    if (!overlay_) return;
    popPopup();
    std::unique_ptr<Node> menu = ui::makeMenuPopup(*this, menuIndex);
    if (!menu) {
        relayout();
        return;
    }
    Node* raw = menu.get();
    overlay_->addAt(std::move(menu), x, y);
    focusNode(raw);
    relayout();
}

void Host::openCanvasMenu(float x, float y)
{
    if (!overlay_) return;
    popPopup();
    std::unique_ptr<Node> menu = ui::makeCanvasMenu(*this);
    Node* raw = menu.get();
    overlay_->addAt(std::move(menu), x, y);
    focusNode(raw);
    relayout();
}

void Host::openListPopup(const std::vector<std::string>& items, int current, float x, float y,
                         std::function<void(int)> pick, std::vector<char> disabled)
{
    if (!overlay_) return;
    std::unique_ptr<Node> popup =
        ui::makeListPopup(*this, items, current, std::move(pick), std::move(disabled));
    Node* raw = popup.get();
    overlay_->addAt(std::move(popup), x, y);
    focusNode(raw);
    relayout();
}

void Host::openCmdListPopup(const std::vector<std::string>& items, const std::vector<Cmd>& cmds,
                            float x, float y, std::function<void(int)> pick)
{
    if (!overlay_) return;
    std::unique_ptr<Node> popup = ui::makeCmdListPopup(*this, items, cmds, std::move(pick));
    Node* raw = popup.get();
    overlay_->addAt(std::move(popup), x, y);
    focusNode(raw);
    relayout();
}

void Host::replaceWithListPopup(const std::vector<std::string>& items, float x, float y,
                                std::function<void(int)> pick)
{
    popPopup();
    openListPopup(items, -1, x, y, std::move(pick));
}

Host::PanelLayout Host::computePanelRects(int W, int top, int bottom, int tabH) const
{
    PanelLayout ly;
    const int contentH = std::max(0, bottom - top);
    const int colR = std::clamp(settings.colR, 180, std::max(180, W / 3));
    const int railW = ui::dims::railW;
    const int imgH = std::clamp((int)std::lround((float)(contentH - kSplitT) * settings.imgFrac), 120,
                                std::max(120, contentH - 120));

    ly.splitRW = Rect{W - colR - kSplitT, top, kSplitT, contentH};
    ly.tools = Rect{0, top, railW, contentH};
    ly.image = Rect{W - colR, top, colR, imgH};
    ly.splitRH = Rect{W - colR, ly.image.bottom(), colR, kSplitT};
    ly.layers = Rect{W - colR, ly.splitRH.bottom(), colR, std::max(0, contentH - imgH - kSplitT)};
    const int cw = std::max(0, W - railW - colR - kSplitT);
    const int tab = std::min(std::max(0, tabH), contentH);
    ly.tabbar = Rect{railW, top, cw, tab};
    ly.canvas = Rect{railW, top + tab, cw, std::max(0, contentH - tab)};
    return ly;
}

Rect Host::optionsRect(const Rect& canvas, int w, int h) const
{
    const Size v = viewSize();
    if (settings.optX >= 0 && settings.optY >= 0) {
        int x = settings.optX;
        int y = settings.optY;
        if (v.w > 0 && v.h > 0) {
            x = std::clamp(x, 0, std::max(0, v.w - w));
            y = std::clamp(y, 0, std::max(0, v.h - h));
        }
        return Rect{x, y, w, h};
    }
    return Rect{canvas.x + 8, canvas.y + 8, w, h};
}

int Host::splitHit(float x, float y) const
{
    const Rect r[3] = {panelRects_.splitLW, panelRects_.splitRW, panelRects_.splitRH};
    for (int i = 0; i < 3; ++i) {
        Rect t = r[i];
        if (t.w <= 0 || t.h <= 0) continue;
        t.x -= 2;
        t.y -= 2;
        t.w += 4;
        t.h += 4;
        if (t.contains((int)x, (int)y)) return i;
    }
    return -1;
}

void Host::beginSplitDrag(int which)
{
    if (which < 0 || which > 2) return;
    splitDrag_ = which;
    splitGhost_ = Rect{};
    splitPend_ = -1;
    splitStartP_ = lastMouse_;
    splitStartA_ = which == 0 ? settings.colL : (which == 1 ? settings.colR : panelRects_.image.h);
}

int Host::splitTrackValue(int which, int startA, int dx, int dy, int viewW, int contentH)
{
    switch (which) {
        case 0: return std::clamp(startA + dx, 140, std::max(140, viewW / 3));
        case 1: return std::clamp(startA - dx, 180, std::max(180, viewW / 3));
        case 2: return std::clamp(startA + dy, 140, std::max(140, contentH - 120));
        default: return startA;
    }
}

void Host::dragSplitTo(const Vec2f& pos)
{
    if (splitDrag_ < 0) return;
    const Size v = viewSize();
    const int dx = (int)std::lround(pos.x - splitStartP_.x);
    const int dy = (int)std::lround(pos.y - splitStartP_.y);
    const int contentH = std::max(200, panelRects_.tools.h);
    const int val = splitTrackValue(splitDrag_, splitStartA_, dx, dy, v.w, contentH);
    if (val == splitPend_) return;

    const Rect bar[3] = {panelRects_.splitLW, panelRects_.splitRW, panelRects_.splitRH};
    Rect g = bar[splitDrag_];
    const int delta = val - splitStartA_;
    if (splitDrag_ < 2)
        g.x += splitDrag_ == 0 ? delta : -delta;
    else
        g.y += delta;
    g.w = std::max(1, g.w);
    g.h = std::max(1, g.h);

    splitPend_ = val;
    if (root_) {
        if (!splitGhost_.empty()) root_->markDirty(ghostDirty(splitGhost_));
        root_->markDirty(ghostDirty(g));
    }
    splitGhost_ = g;
}

void Host::endSplitDrag()
{
    if (splitDrag_ < 0) return;
    const int which = splitDrag_;
    const int val = splitPend_;
    splitDrag_ = -1;
    splitPend_ = -1;
    if (root_ && !splitGhost_.empty()) root_->markDirty(ghostDirty(splitGhost_));
    splitGhost_ = Rect{};
    if (val >= 0) {
        const int contentH = std::max(200, panelRects_.tools.h);
        switch (which) {
            case 0: settings.colL = val; break;
            case 1: settings.colR = val; break;
            case 2: settings.imgFrac = (float)val / (float)std::max(1, contentH - kSplitT); break;
            default: break;
        }
    }
    relayout();
    saveSettings();
}

void Host::refreshCanvas()
{
    ed().fitIfPending();
    if (canvas_) canvas_->syncDocument();
    markChromeDirty();
}

void Host::beginColorPick(std::function<void(float, float, float, float)> sink)
{
    colorPick_ = std::move(sink);
    if (canvas_) canvas_->setPickMode(colorPick_ != nullptr);
}

void Host::cancelColorPick()
{
    colorPick_ = nullptr;
    if (canvas_) canvas_->setPickMode(false);
}

void Host::execute(Cmd cmd)
{
    Editor& e = ed();
    closePopup();
    if (!ui::menuCmdEnabled(*this, cmd)) return;

    switch (cmd) {
        case Cmd::NewDoc: openDialog(); break;
        case Cmd::NewTab: addTab(); break;
        case Cmd::Open: openProjectFile(); break;
        case Cmd::Save: saveProjectFile(false); break;
        case Cmd::SaveAs:
        case Cmd::Export: saveProjectFile(true); break;
        case Cmd::Quit: requestQuit(); break;

        case Cmd::Undo: e.undo(); break;
        case Cmd::Redo: e.redo(); break;
        case Cmd::Cut: e.cutSelection(); break;
        case Cmd::Copy: e.copySelection(); break;
        case Cmd::Paste: e.paste(); break;
        case Cmd::Delete:
            e.deleteByKey();
            break;
        case Cmd::SelectAll: e.selectAll(); break;
        case Cmd::Deselect: e.deselect(); break;
        case Cmd::InvertSelection: e.invertSelection(); break;

        case Cmd::NewLayer: e.addEmptyLayer(); break;
        case Cmd::DuplicateLayer:
            if (e.doc.active >= 0) e.duplicateLayer(e.doc.active);
            break;
        case Cmd::DeleteLayer:
            e.deleteSelectedLayers();
            break;
        case Cmd::MergeSelected: e.mergeSelectedLayers(); break;
        case Cmd::MergeDown:
            if (e.doc.active >= 0) e.mergeLayerDown(e.doc.active);
            break;
        case Cmd::MergeImage: e.mergeImageLayer(); break;
        case Cmd::ImageRotate180: e.rotateCanvas(0); break;
        case Cmd::ImageRotateCW: e.rotateCanvas(1); break;
        case Cmd::ImageRotateCCW: e.rotateCanvas(2); break;
        case Cmd::ImageFlipH: e.rotateCanvas(3); break;
        case Cmd::ImageFlipV: e.rotateCanvas(4); break;
        case Cmd::LayerRotate180: e.rotateLayer(0); break;
        case Cmd::LayerRotateCW: e.rotateLayer(1); break;
        case Cmd::LayerRotateCCW: e.rotateLayer(2); break;
        case Cmd::LayerFlipH: e.rotateLayer(3); break;
        case Cmd::LayerFlipV: e.rotateLayer(4); break;
        case Cmd::LayerCenter: e.centerActiveLayer(); break;

        case Cmd::ComposeLines:
            e.toggleComposeLines();
            if (canvas_) canvas_->markDirty(canvas_->frame());
            break;

        case Cmd::ImageSize: openDialogNode(ui::makeImageSizeDialog(*this)); break;
        case Cmd::Shortcuts: openDialogNode(ui::makeShortcutsDialog(*this)); break;
        case Cmd::CanvasSize: openDialogNode(ui::makeCanvasSizeDialog(*this)); break;
        case Cmd::TrimCanvas:
            if (e.sel.active && !e.sel.bounds().empty()) e.cropToSelection();
            else e.trimCanvas();
            break;
        case Cmd::InvertColors: e.applyInvert(); break;
        case Cmd::Desaturate: e.applyDesaturate(); break;
        case Cmd::Threshold: openDialogNode(ui::makeThresholdDialog(*this)); break;
        case Cmd::BrightContrast: openDialogNode(ui::makeBrightContrastDialog(*this)); break;
        case Cmd::HueShift: openDialogNode(ui::makeHueDialog(*this)); break;

        case Cmd::FitView: e.fitView(); break;
        case Cmd::ActualPixels: e.actualPixels(); break;
        case Cmd::ZoomIn: e.zoomIn(); break;
        case Cmd::ZoomOut: e.zoomOut(); break;
        case Cmd::MeshWarp: {
            for (int i = 0; i < toolsCount(); ++i) {
                if (toolId(i) == ToolId::Move) {
                    setTool(i);
                    break;
                }
            }
            canvas_->setTool(&tool());
            e.beginMeshWarp(e.doc.active);
            relayout();
            break;
        }
        case Cmd::LocalWarp: {
            for (int i = 0; i < toolsCount(); ++i) {
                if (toolId(i) == ToolId::Move) {
                    setTool(i);
                    break;
                }
            }
            canvas_->setTool(&tool());
            focus_ = static_cast<Node*>(canvas_);
            e.beginLocalWarp(e.doc.active);
            relayout();
            break;
        }
        case Cmd::FreeTransform: {
            for (int i = 0; i < toolsCount(); ++i) {
                if (toolId(i) == ToolId::Move) {
                    setTool(i);
                    break;
                }
            }
            canvas_->setTool(&tool());
            e.beginFreeTransform(e.doc.active);
            break;
        }

        case Cmd::NextTab: nextTab(); break;
        case Cmd::CloseTab: promptCloseTab(activeTab_); break;
        case Cmd::About: openDialogNode(ui::makeAboutDialog(*this)); break;

        case Cmd::MoveLayerUp:
            if (e.doc.active >= 0 && e.doc.active + 1 < (int)e.doc.layers.size()) e.moveLayer(e.doc.active, e.doc.active + 1);
            break;
        case Cmd::MoveLayerDown:
            if (e.doc.active > 0) e.moveLayer(e.doc.active, e.doc.active - 1);
            break;
        default: break;
    }

    if (tabs_.empty() || !canvas_) return;
    canvas_->syncDocument();
    markChromeDirty();
}

}

