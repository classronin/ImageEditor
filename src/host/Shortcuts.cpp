#include "host/Shortcuts.h"

#include "core/Keymap.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace ie {
namespace shortcuts {
namespace {

std::vector<Row> g_rows;

std::string nameOfKey(int key)
{
    if (key >= 1 && key <= 26) return std::string(1, (char)('A' + key - 1));
    if (key >= 27 && key <= 36) return std::string(1, (char)('0' + key - 27));
    if (key >= 37 && key <= 46) return "Num" + std::string(1, (char)('0' + key - 37));
    if (key >= 47 && key <= 58) return "F" + std::to_string(key - 46);
    switch (key) {
        case 59: return "Esc";
        case 60: return "`";
        case 61: return "-";
        case 62: return "=";
        case 63: return "Backspace";
        case 64: return "Tab";
        case 65: return "[";
        case 66: return "]";
        case 67: return "Enter";
        case 68: return ";";
        case 69: return "'";
        case 70: return "\\";
        case 72: return ",";
        case 73: return ".";
        case 74: return "/";
        case 75: return "Space";
        case 76: return "Insert";
        case 77: return "Delete";
        case 78: return "Home";
        case 79: return "End";
        case 80: return "PageUp";
        case 81: return "PageDown";
        case 82: return "Left";
        case 83: return "Right";
        case 84: return "Up";
        case 85: return "Down";
        case 87: return "Num*";
        case 88: return "Num-";
        case 89: return "Num+";
        case 91: return "NumEnter";
        default: return std::string();
    }
}

int keyOfName(const std::string& name)
{
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
        if (c >= 'a' && c <= 'z') return 1 + (c - 'a');
        if (c >= '0' && c <= '9') return 27 + (c - '0');
        if (c == '-') return 61;
        if (c == '=') return 62;
        if (c == '`') return 60;
        if (c == '[') return 65;
        if (c == ']') return 66;
        if (c == ';') return 68;
        if (c == '\'') return 69;
        if (c == ',') return 72;
        if (c == '.') return 73;
        if (c == '/') return 74;
        return 0;
    }
    if (name.size() == 2 && name[0] == '\\') return 70;
    if (name.size() >= 4 && name.substr(0, 3) == "Num" && name.size() == 4) {
        const char c = name[3];
        if (c >= '0' && c <= '9') return 37 + (c - '0');
        if (c == '*') return 87;
        if (c == '-') return 88;
        if (c == '+') return 89;
        return 0;
    }
    if (name.size() >= 2 && name[0] == 'F' && std::isdigit((unsigned char)name[1])) {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 12) return 46 + n;
        return 0;
    }
    if (name == "Esc") return 59;
    if (name == "Backspace") return 63;
    if (name == "Tab") return 64;
    if (name == "Enter") return 67;
    if (name == "Space") return 75;
    if (name == "Insert") return 76;
    if (name == "Delete") return 77;
    if (name == "Home") return 78;
    if (name == "End") return 79;
    if (name == "PageUp") return 80;
    if (name == "PageDown") return 81;
    if (name == "Left") return 82;
    if (name == "Right") return 83;
    if (name == "Up") return 84;
    if (name == "Down") return 85;
    if (name == "NumEnter") return 91;
    return 0;
}

std::vector<Row> defaultList()
{
    return {
        {KeyAct::Undo, 0, KeyZ, keymap::Ctrl},
        {KeyAct::Redo, 0, KeyZ, keymap::Ctrl | keymap::Shift},
        {KeyAct::Redo, 1, KeyY, keymap::Ctrl},
        {KeyAct::NewDoc, 0, KeyN, keymap::Ctrl},
        {KeyAct::NewLayer, 0, KeyN, keymap::Ctrl | keymap::Shift},
        {KeyAct::Transform, 0, KeyT, keymap::Ctrl},
        {KeyAct::CloseTab, 0, KeyW, keymap::Ctrl},
        {KeyAct::Open, 0, KeyO, keymap::Ctrl},
        {KeyAct::Save, 0, KeyS, keymap::Ctrl},
        {KeyAct::SaveAs, 0, KeyS, keymap::Ctrl | keymap::Shift},
        {KeyAct::SelectAll, 0, KeyA, keymap::Ctrl},
        {KeyAct::Deselect, 0, KeyD, keymap::Ctrl},
        {KeyAct::Copy, 0, KeyC, keymap::Ctrl},
        {KeyAct::Cut, 0, KeyX, keymap::Ctrl},
        {KeyAct::Paste, 0, KeyV, keymap::Ctrl},
        {KeyAct::MergeSelected, 0, KeyE, keymap::Ctrl},
        {KeyAct::DuplicateLayer, 0, KeyJ, keymap::Ctrl},
        {KeyAct::FitView, 0, Key0, keymap::Ctrl},
        {KeyAct::ActualPixels, 0, Key1, keymap::Ctrl},
        {KeyAct::ZoomIn, 0, KeyEqual, keymap::Ctrl},
        {KeyAct::ZoomOut, 0, KeyMinus, keymap::Ctrl},
        {KeyAct::Delete, 0, KeyDelete, keymap::None},
        {KeyAct::FitView, 1, KeyHome, keymap::None},
        {KeyAct::NextTab, 0, KeyTab, keymap::None},
        {KeyAct::ToolMove, 0, KeyV, keymap::None},
        {KeyAct::ToolSelect, 0, KeyM, keymap::None},
        {KeyAct::ToolLasso, 0, KeyA, keymap::None},
        {KeyAct::ToolWand, 0, KeyW, keymap::None},
        {KeyAct::ToolCrop, 0, KeyC, keymap::None},
        {KeyAct::ToolBrush, 0, KeyB, keymap::None},
        {KeyAct::ToolEraser, 0, KeyE, keymap::Shift},
        {KeyAct::ToolMask, 0, KeyP, keymap::None},
        {KeyAct::ToolBucket, 0, KeyG, keymap::None},
        {KeyAct::ToolGradient, 0, KeyG, keymap::Shift},
        {KeyAct::ToolText, 0, KeyT, keymap::None},
        {KeyAct::ToolShape, 0, KeyU, keymap::None},
        {KeyAct::TrimCanvas, 0, KeyR, keymap::None},
    };
}

const std::vector<KeyAct>& allActs()
{
    static const std::vector<KeyAct> v = [] {
        std::vector<KeyAct> a;
        for (int i = 1; i <= (int)KeyAct::TrimCanvas; ++i) a.push_back((KeyAct)i);
        return a;
    }();
    return v;
}

void initDefaults()
{
    g_rows = defaultList();
}

struct Init {
    Init() { initDefaults(); }
};
Init g_init;

}

std::vector<Row>& rows()
{
    return g_rows;
}

void resetDefaults()
{
    initDefaults();
}

std::vector<Row> defaults()
{
    return defaultList();
}

std::string actionId(KeyAct act)
{
    switch (act) {
        case KeyAct::Undo: return "Undo";
        case KeyAct::Redo: return "Redo";
        case KeyAct::NewDoc: return "NewDoc";
        case KeyAct::NewLayer: return "NewLayer";
        case KeyAct::CloseTab: return "CloseTab";
        case KeyAct::NextTab: return "NextTab";
        case KeyAct::Open: return "Open";
        case KeyAct::Save: return "Save";
        case KeyAct::SaveAs: return "SaveAs";
        case KeyAct::SelectAll: return "SelectAll";
        case KeyAct::Deselect: return "Deselect";
        case KeyAct::Copy: return "Copy";
        case KeyAct::Cut: return "Cut";
        case KeyAct::Paste: return "Paste";
        case KeyAct::MergeSelected: return "MergeSelected";
        case KeyAct::DuplicateLayer: return "DuplicateLayer";
        case KeyAct::Delete: return "Delete";
        case KeyAct::FitView: return "FitView";
        case KeyAct::ActualPixels: return "ActualPixels";
        case KeyAct::ZoomIn: return "ZoomIn";
        case KeyAct::ZoomOut: return "ZoomOut";
        case KeyAct::Transform: return "Transform";
        case KeyAct::ToolMove: return "ToolMove";
        case KeyAct::ToolSelect: return "ToolSelect";
        case KeyAct::ToolLasso: return "ToolLasso";
        case KeyAct::ToolWand: return "ToolWand";
        case KeyAct::ToolCrop: return "ToolCrop";
        case KeyAct::ToolBrush: return "ToolBrush";
        case KeyAct::ToolEraser: return "ToolEraser";
        case KeyAct::ToolMask: return "ToolMask";
        case KeyAct::ToolBucket: return "ToolBucket";
        case KeyAct::ToolGradient: return "ToolGradient";
        case KeyAct::ToolText: return "ToolText";
        case KeyAct::ToolShape: return "ToolShape";
        case KeyAct::TrimCanvas: return "TrimCanvas";
        default: return "None";
    }
}

KeyAct actFromId(const std::string& id)
{
    for (KeyAct a : allActs())
        if (id == actionId(a)) return a;
    return KeyAct::None;
}

std::string actionName(KeyAct act)
{
    switch (act) {
        case KeyAct::Undo: return "撤销";
        case KeyAct::Redo: return "重做";
        case KeyAct::NewDoc: return "新建画布";
        case KeyAct::NewLayer: return "新建图层";
        case KeyAct::CloseTab: return "关闭标签";
        case KeyAct::NextTab: return "下一个标签";
        case KeyAct::Open: return "打开";
        case KeyAct::Save: return "保存";
        case KeyAct::SaveAs: return "另存为";
        case KeyAct::SelectAll: return "全选";
        case KeyAct::Deselect: return "取消选择";
        case KeyAct::Copy: return "复制";
        case KeyAct::Cut: return "剪切";
        case KeyAct::Paste: return "粘贴";
        case KeyAct::MergeSelected: return "合并图层";
        case KeyAct::DuplicateLayer: return "复制图层";
        case KeyAct::Delete: return "删除";
        case KeyAct::FitView: return "适应窗口";
        case KeyAct::ActualPixels: return "实际像素";
        case KeyAct::ZoomIn: return "放大";
        case KeyAct::ZoomOut: return "缩小";
        case KeyAct::Transform: return "变形";
        case KeyAct::ToolMove: return "移动";
        case KeyAct::ToolSelect: return "选框";
        case KeyAct::ToolLasso: return "套索";
        case KeyAct::ToolWand: return "魔棒";
        case KeyAct::ToolCrop: return "裁剪";
        case KeyAct::ToolBrush: return "画笔";
        case KeyAct::ToolEraser: return "橡皮";
        case KeyAct::ToolMask: return "抠图";
        case KeyAct::ToolBucket: return "油漆桶";
        case KeyAct::ToolGradient: return "渐变";
        case KeyAct::ToolText: return "文字";
        case KeyAct::ToolShape: return "形状";
        case KeyAct::TrimCanvas: return "裁切";
        default: return std::string();
    }
}

int toolSlot(KeyAct act)
{
    const int base = (int)KeyAct::ToolMove;
    const int v = (int)act;
    if (v < base || v > (int)KeyAct::ToolShape) return -1;
    return v - base;
}

std::string comboText(int key, unsigned mods)
{
    if (key == 0) return std::string();
    std::string out;
    if (mods & keymap::Ctrl) out += "Ctrl+";
    if (mods & keymap::Alt) out += "Alt+";
    if (mods & keymap::Shift) out += "Shift+";
    out += nameOfKey(key);
    return out;
}

std::string rowCombo(const Row& r)
{
    return comboText(r.key, r.mods);
}

std::string toolCombo(int slot)
{
    if (slot < 0 || slot > toolSlot(KeyAct::ToolShape)) return std::string();
    const KeyAct act = (KeyAct)((int)KeyAct::ToolMove + slot);
    const int idx = rowIndex(act, 0);
    if (idx < 0) return std::string();
    return rowCombo(g_rows[(size_t)idx]);
}

bool parseCombo(const std::string& text, int& key, unsigned& mods)
{
    key = 0;
    mods = 0;
    if (text.empty() || text == "-") return true;
    size_t pos = 0;
    while (true) {
        size_t plus = text.find('+', pos);
        if (plus == std::string::npos) break;
        const std::string part = text.substr(pos, plus - pos);
        if (part == "Ctrl") mods |= keymap::Ctrl;
        else if (part == "Alt") mods |= keymap::Alt;
        else if (part == "Shift") mods |= keymap::Shift;
        else return false;
        pos = plus + 1;
    }
    const int k = keyOfName(text.substr(pos));
    if (k == 0) return false;
    key = k;
    return true;
}

KeyAct resolve(int key, unsigned mods)
{
    for (const Row& r : g_rows)
        if (r.key != 0 && r.key == key && r.mods == mods) return r.act;
    return KeyAct::None;
}

int rowIndex(KeyAct act, int slot)
{
    return rowIndexIn(g_rows, act, slot);
}

int rowIndexIn(const std::vector<Row>& list, KeyAct act, int slot)
{
    for (int i = 0; i < (int)list.size(); ++i)
        if (list[(size_t)i].act == act && list[(size_t)i].slot == slot) return i;
    return -1;
}

void bindRowIn(std::vector<Row>& list, int index, int key, unsigned mods)
{
    if (index < 0 || index >= (int)list.size()) return;
    if (key != 0) {
        for (int i = 0; i < (int)list.size(); ++i)
            if (i != index && list[(size_t)i].key == key && list[(size_t)i].mods == mods) {
                list[(size_t)i].key = 0;
                list[(size_t)i].mods = 0;
            }
    }
    Row& target = list[(size_t)index];
    target.key = key;
    target.mods = key == 0 ? 0u : mods;
}

void bindRow(int index, int key, unsigned mods)
{
    bindRowIn(g_rows, index, key, mods);
}

void applyRows(const std::vector<Row>& list)
{
    if (list.size() != g_rows.size()) return;
    g_rows = list;
}

bool keyHasName(int key)
{
    return !nameOfKey(key).empty();
}

}
}
