#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Types.h"
#include "host/Commands.h"
#include "tools/Tool.h"

class SkCanvas;

namespace ie {

class Host;
class Node;

namespace ui {

namespace dims {
constexpr int menuH = 30;
constexpr int tabH = 30;
constexpr int optionsH = 34;
constexpr int railW = 56;
constexpr int rightW = 286;
constexpr int statusH = 28;
}

std::unique_ptr<Node> makeMenuBar(Host& host);std::unique_ptr<Node> makeTabBar(Host& host);
std::unique_ptr<Node> makeOptionsBar(Host& host);
int propsLabelCol(const Props& props, int rowW);
std::unique_ptr<Node> makeToolRail(Host& host);
std::unique_ptr<Node> makeLayerPanel(Host& host);
std::unique_ptr<Node> makeStatusBar(Host& host);
std::unique_ptr<Node> makeImagePanel(Host& host);

struct RailGrid {
    int cols = 1, rows = 1, cell = 28, gap = 2;
    Rect area{};
};
Rect railSwatch(const Rect& frame);
void railSwatchCells(const Rect& frame, Rect& fg, Rect& bg);
void railSwatchCells(const Rect& frame, Rect& fg, Rect& bg, Rect& swap);
void swatchCellsIn(const Rect& a, Rect& fg, Rect& bg, Rect& swap);
Rect railSwatchDocked(const Rect& frame, int count);
RailGrid railGrid(const Rect& frame, int count);
int railNeededH(int width, int count);

constexpr int kLayerBtnCount = 3;
const char* layerBtnLabel(int i);
int layerBtnW(int i);
int layerBtnOffset(int i);
int layerBtnTotalW();

Rect hoverTipRect(Host& host);void paintHoverTip(SkCanvas& canvas, Host& host);
void paintDragGhost(SkCanvas& canvas, Host& host);

std::unique_ptr<Node> makeMenuPopup(Host& host, int menuIndex);
std::unique_ptr<Node> makeCanvasMenu(Host& host);
std::vector<std::string> menuItemTitles(const std::string& title);
std::vector<std::string> menuTitles();
bool menuCmdEnabled(Host& host, ie::Cmd cmd);
std::unique_ptr<Node> makeListPopup(Host& host, std::vector<std::string> items, int current,
                                    std::function<void(int)> pick, std::vector<char> disabled = {});
std::unique_ptr<Node> makeCmdListPopup(Host& host, const std::vector<std::string>& items,
                                       const std::vector<ie::Cmd>& cmds,
                                       std::function<void(int)> pick);
std::unique_ptr<Node> makeColorPopup(Host& host, const float* rgba,
                                     std::function<void(const float[4])> onChange);

std::unique_ptr<Node> makeNewDialog(Host& host);
std::unique_ptr<Node> makeCanvasSizeDialog(Host& host);
std::unique_ptr<Node> makeImageSizeDialog(Host& host);
std::unique_ptr<Node> makeThresholdDialog(Host& host);
std::unique_ptr<Node> makeBrightContrastDialog(Host& host);
std::unique_ptr<Node> makeHueDialog(Host& host);
std::unique_ptr<Node> makeSaveDialog(Host& host, int closeTabAfter = -1, const std::string& initialPath = {});
std::unique_ptr<Node> makeCloseDialog(Host& host, int tab);
std::unique_ptr<Node> makeQuitDialog(Host& host);
std::unique_ptr<Node> makeAboutDialog(Host& host);
std::unique_ptr<Node> makeShortcutsDialog(Host& host);

}
}
