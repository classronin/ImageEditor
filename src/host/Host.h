#pragma once

#include "app/Editor.h"
#include "app/ViewState.h"
#include "host/Commands.h"
#include "host/Compositor.h"
#include "host/Input.h"
#include "nodes/Container.h"
#include "nodes/Panels.h"
#include "tools/Tools.h"

#include "os/os.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ie {

struct ImeWindowForm {
    int caretX = 0, caretY = 0;
    int areaL = 0, areaT = 0, areaR = 0, areaB = 0;
};
ImeWindowForm imeWindowForm(int caretX, int lineTop, int lineW, int lineH, int clientW, int clientH);

class CanvasNode;
class OverlayLayer;

class Host {
public:
    Host();
    ~Host();

    bool init();
    void run();

    Editor& ed() { return *tabs_[(size_t)activeTab_]; }
    const Editor& ed() const { return *tabs_[(size_t)activeTab_]; }
    Tool& tool() { return *tools_[(size_t)activeTool_]; }
    ToolId activeId() const { return tools_[(size_t)activeTool_]->id(); }

    int tabCount() const { return (int)tabs_.size(); }
    int activeTabIndex() const { return activeTab_; }
    Editor& tab(int i) { return *tabs_[(size_t)i]; }

    int toolsCount() const { return (int)tools_.size(); }
    int activeToolIndex() const { return activeTool_; }
    std::string toolName(int i) const { return tools_[(size_t)i]->name(); }
    ToolId toolId(int i) const { return tools_[(size_t)i]->id(); }

    void setHoverHint(std::string s, const Rect& anchor = Rect{});
    const std::string& hoverHint() const { return hoverHint_; }
    const Rect& hoverHintAnchor() const { return hoverHintAnchor_; }

    void setLayerDragGhost(const Rect& r, std::string name);
    void clearLayerDragGhost();
    bool layerGhostOn() const { return ghostOn_; }
    const Rect& layerGhostRect() const { return ghostRect_; }
    const std::string& layerGhostText() const { return ghostText_; }

    int zoomPercent() const;

    void execute(Cmd cmd);
    void setTool(int index);

    void newTabCanvas(int w, int h);
    bool openFile(const std::string& path);
    void placeImageLayer(const std::string& path);
    void openDialog();
    void saveDialog(int closeTabAfter = -1, const std::string& initialPath = {});

    void addTab();
    void requestCloseTab(int index);
    void promptCloseTab(int index);
    void requestQuit();
    void quitSaveAll();
    void quitNoSave();
    void continueQuitAfterSave();
    int unsavedTabCount() const;
    void closeTabNoSave(int index);
    void saveAndCloseTab(int index);
    void selectTab(int index);
    void nextTab();

    void setTabProbe(std::function<int(float, float)> fn) { tabProbe_ = std::move(fn); }
    int tabIndexAtPoint(float x, float y) const { return tabProbe_ ? tabProbe_(x, y) : -1; }
    int dropTabTarget() const { return dropTabTarget_; }

    void openMenu(int menuIndex, float x, float y);
    void openCanvasMenu(float x, float y);
    void openListPopup(const std::vector<std::string>& items, int current, float x, float y,
                       std::function<void(int)> pick, std::vector<char> disabled = {});
    void openCmdListPopup(const std::vector<std::string>& items, const std::vector<Cmd>& cmds,
                          float x, float y, std::function<void(int)> pick);
    void replaceWithListPopup(const std::vector<std::string>& items, float x, float y,
                              std::function<void(int)> pick);
    void openDialogNode(std::unique_ptr<Node> dialog);
    void persistSettings() { saveSettings(); }
    void pushDialogNode(std::unique_ptr<Node> dialog);
    void closePopup();
    bool popupOpen() const;
    Rect popupFrame() const;

    void armList(const Rect& anchor, std::function<void(int dir)> step);
    void disarmList();
    bool listArmed(const Rect& r) const;
    int barMenuIndex() const;
    void focusNode(Node* n);

    void capturePointer(Node* n) { captured_ = n; }
    void releasePointer(Node* n)
    {
        if (captured_ == n) captured_ = nullptr;
    }

    void post(std::function<void()> fn);

    void beginColorPick(std::function<void(float, float, float, float)> sink);
    bool colorPickActive() const { return (bool)colorPick_; }
    void cancelColorPick();


    void refreshCanvas();

    enum class Pane { Rail, Layers };
    static constexpr int kPaneCount = 2;

    struct PanelLayout {
        Rect tools{};
        Rect options{};
        Rect tabbar{};
        Rect canvas{};
        Rect image{};
        Rect layers{};
        Rect splitLW{};
        Rect splitRW{};
        Rect splitRH{};
    };
    static constexpr int kSplitT = 6;
    PanelLayout panelRects_{};
    bool propsShown_ = false;
    PanelLayout computePanelRects(int W, int top, int bottom, int tabH = 0) const;
    Rect optionsRect(const Rect& canvas, int w, int h) const;
    int splitDrag() const { return splitDrag_; }
    Rect splitGhost() const { return splitGhost_; }
    static int splitTrackValue(int which, int startA, int dx, int dy, int viewW, int contentH);
    int splitHit(float x, float y) const;
    void beginSplitDrag(int which);
    void dragSplitTo(const Vec2f& pos);
    void endSplitDrag();

    struct Settings {
        int winX = -1, winY = -1;
        int winW = 1360, winH = 860;
        bool winMaximized = false;
        int triX = -1;
        int triY = -1;
        int triW = 300;
        int triH = 0;
        int optX = -1;
        int optY = -1;
        int colL = 300;
        int colR = 300;
        float toolsFrac = 0.25f;
        float propsFrac = 0.25f;
        float imgFrac = 0.3333f;
        std::string lastDir;
    };
    Settings settings;
    void applyScale();

    void applyCanvasSize(int w, int h, int ax, int ay);
    void applyImageSize(int w, int h);
    void applyStroke(int width, Color color);

    std::string defaultSaveDir() const;
    std::string defaultSaveName() const;
    void browseSaveDir(std::string& dir);
    bool saveTo(const std::string& path);
    bool saveTo(const std::string& path, const imageio::SaveOptions& opt);

    CanvasNode* canvasNode() { return canvas_; }
    InputFrame& input() { return input_; }
    void relayout();
    void markChromeDirty();
    void dispatchKey(const KeyEvent& e);
    Size viewSize() const;

private:
    bool createWindow();
    void handleEvent(os::Event& ev);
    void tick();
    bool popPopup();
    void drainPosts();
    bool panelTyping() const;
    void dispatchPointer(const PointerEvent& e);
    void applyToolCursor();
    void consumeTextEditRequest();
    void syncTextInput();
    void placeImeWindows(const ImeWindowForm& f);
    bool imeComposingNow();
    os::CursorRef rotateCursor();

    void buildUI();
    Editor& addEditor();
    void afterTabChanged();
    void copyLayerToTab(int tab);

    void loadSettings();
    void saveSettings();

    void openProjectFile();
    void saveProjectFile(bool saveAs);

    os::SystemRef system_;
    os::WindowRef window_;
    Compositor compositor_;
    std::vector<Rect> dirtyScratch_;

    std::vector<std::function<void()>> posted_;
    bool drainingPosts_ = false;

    std::unique_ptr<Container> root_;
    OverlayLayer* overlay_ = nullptr;
    CanvasNode* canvas_ = nullptr;
    Node* status_ = nullptr;
    Node* layerPanel_ = nullptr;
    Node* toolRail_ = nullptr;
    std::vector<Node*> chrome_;

    Rect listArmedRect_{};
    std::function<void(int)> listStep_;
    int consumedKey_ = -1;

    int splitDrag_ = -1;
    int splitStartA_ = 0;
    int splitPend_ = -1;
    Rect splitGhost_{};
    Vec2f splitStartP_{0, 0};

    int lastZoom_ = -1;
    int lastDocW_ = -1;
    bool lastLiveOn_ = false;
    int lastLiveW_ = -1;
    int lastLiveH_ = -1;
    int lastDocH_ = -1;
    std::string hoverHint_;
    Rect hoverHintAnchor_{};
    bool ghostOn_ = false;
    Rect ghostRect_{};
    std::string ghostText_;
    int lastStructRev_ = -1;
    int lastActiveLayer_ = -1;
    int lastSelCount_ = -1;

    bool imeOn_ = false;
    int imeX_ = -1, imeY_ = -1;
    bool imeCaretOwned_ = false;

    std::vector<std::unique_ptr<Editor>> tabs_;
    int activeTab_ = 0;
    std::function<int(float, float)> tabProbe_;
    int dropTabTarget_ = -1;
    std::vector<std::unique_ptr<Tool>> tools_;
    int activeTool_ = 0;

    InputFrame input_{};
    Vec2f lastMouse_{0, 0};
    Node* hovered_ = nullptr;
    Node* captured_ = nullptr;
    int lastCursor_ = -1;
    os::CursorRef customCursor_;
    Node* focus_ = nullptr;
    bool pointerDown_[3] = {false, false, false};
    bool spaceDown_ = false;
    std::function<void(float, float, float, float)> colorPick_;
    bool running_ = true;
    bool firstFrame_ = true;
    float time_ = 0.0f;
    double lastTick_ = 0.0;
    std::string settingsPath_;
    bool settingsFresh_ = true;
public:
    bool settingsFresh() const { return settingsFresh_; }
    void consumeSettingsFresh() { settingsFresh_ = false; }
private:
};

}
