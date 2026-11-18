#pragma once

#include "core/Image.h"
#include "core/Types.h"

#include "include/core/SkPath.h"

#include <string>
#include <vector>

namespace ie {

struct TextData {
    std::string text;
    float x = 0.0f, y = 0.0f;
    int size = 64;
    float lineSpacing = 6.0f;
    Color color{0, 0, 0, 255};
    float opacity = 1.0f;
    std::string font;
};

struct TextMetrics {
    float lineH = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float width = 0.0f;
    int lines = 0;
    bool ok = false;
};

struct ShapedGlyph {
    const void* face = nullptr;
    unsigned gid = 0;
    unsigned cp = 0;
    float x = 0.0f, y = 0.0f, adv = 0.0f, em = 0.0f;
    int i0 = 0, i1 = 0, line = -1;
};

struct ShapedLine {
    float top = 0.0f, bottom = 0.0f, baselineY = 0.0f;
    int startByte = 0, endByte = 0;
};

struct ShapedText {
    std::vector<ShapedGlyph> glyphs;
    std::vector<ShapedLine> lines;
    float width = 0.0f, ascent = 0.0f, descent = 0.0f, lineH = 0.0f;
    bool ok = false;
};

ShapedText shapeText(const TextData& t);
bool shapedOutline(const ShapedText& s, SkPath& out);

bool textFontReady();
const char* textFontPath();

int textFontCount();
const char* textFontName(int index);
const char* textFontFile(int index);
int textFontIndex(const std::string& family);

TextMetrics measureText(const TextData& t);

Rect textBoxRect(const TextData& t);

bool drawText(Image& img, const TextData& t, Rect& dirty, const uint8_t* mask = nullptr,
              const SkMatrix* xf = nullptr);

bool drawGlyphPath(Image& img, const SkPath& path, Color color, float opacity, Rect& dirty,
                   const uint8_t* mask = nullptr);

int textFtOutlineHits();
int textSkiaPathHits();
bool textGlyphPath(const TextData& t, SkPath& out);

struct TextCaret {
    float x = 0.0f, y = 0.0f;
    float h = 0.0f;
};
TextCaret textCaretAt(const TextData& t, int byteOffset);
int textOffsetAt(const TextData& t, float x, float y);

int textSelectionRects(const TextData& t, int a, int b, Rect* out, int max);

}
