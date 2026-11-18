#pragma once

#include "host/Input.h"

#include <string>
#include <vector>

namespace ie {

enum class KeyAct {
    None = 0,
    Undo,
    Redo,
    NewDoc,
    NewLayer,
    CloseTab,
    NextTab,
    Open,
    Save,
    SaveAs,
    SelectAll,
    Deselect,
    Copy,
    Cut,
    Paste,
    MergeSelected,
    DuplicateLayer,
    Delete,
    FitView,
    ActualPixels,
    ZoomIn,
    ZoomOut,
    Transform,
    ToolMove,
    ToolSelect,
    ToolLasso,
    ToolWand,
    ToolCrop,
    ToolBrush,
    ToolEraser,
    ToolMask,
    ToolBucket,
    ToolGradient,
    ToolText,
    ToolShape,
    TrimCanvas,
};

namespace shortcuts {

struct Row {
    KeyAct act = KeyAct::None;
    int slot = 0;
    int key = 0;
    unsigned mods = 0;
};

std::vector<Row>& rows();
void resetDefaults();
std::vector<Row> defaults();

std::string actionId(KeyAct act);
KeyAct actFromId(const std::string& id);
std::string actionName(KeyAct act);
int toolSlot(KeyAct act);
std::string comboText(int key, unsigned mods);
bool parseCombo(const std::string& text, int& key, unsigned& mods);
std::string rowCombo(const Row& r);
std::string toolCombo(int slot);

KeyAct resolve(int key, unsigned mods);
int rowIndex(KeyAct act, int slot);
int rowIndexIn(const std::vector<Row>& list, KeyAct act, int slot);
void bindRowIn(std::vector<Row>& list, int index, int key, unsigned mods);
void bindRow(int index, int key, unsigned mods);
void applyRows(const std::vector<Row>& list);
bool keyHasName(int key);

}
}
