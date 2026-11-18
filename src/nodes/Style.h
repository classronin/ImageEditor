#pragma once

#include "core/Types.h"
#include "nodes/Overlay.h"

#include <string>

class SkCanvas;

namespace ie {
namespace ui {

struct Theme {
    Color32 bg = rgba(20, 20, 22);
    Color32 panel = rgba(30, 30, 34);
    Color32 panelHi = rgba(40, 40, 46);
    Color32 surface = rgba(24, 24, 28);
    Color32 border = rgba(56, 56, 64);
    Color32 boxLine = rgba(255, 255, 255);
    Color32 text = rgba(230, 230, 235);
    Color32 dim = rgba(150, 150, 160);
    Color32 accent = rgba(72, 140, 240);
    Color32 accentPress = rgba(56, 112, 200);
    Color32 hover = rgba(52, 52, 60);
    Color32 pressed = rgba(64, 64, 74);
    Color32 canvas = rgba(48, 48, 54);
    Color32 checkA = rgba(58, 58, 66);
    Color32 checkB = rgba(70, 70, 80);
    Color32 danger = rgba(220, 90, 90);

    int fontPx = 16;
    int rowH = 26;
    int gap = 6;
    int pad = 8;
    int radius = 4;
};

const Theme& theme();

bool initFonts();

float lineHeight();
float textWidth(const std::string& s);

void drawText(SkCanvas& c, float x, float yTop, Color32 col, const std::string& s);
void fill(SkCanvas& c, const Rect& r, Color32 col, float radius = 0.0f);
void stroke(SkCanvas& c, const Rect& r, Color32 col, float width = 1.0f, float radius = 0.0f);
void controlLine(SkCanvas& c, const Rect& r, float radius = 2.0f);


inline Rect checkBoxIn(const Rect& f)
{
    return Rect{f.x, f.y + (f.h - 14) / 2, 14, 14};
}

}
}
