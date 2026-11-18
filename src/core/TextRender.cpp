#include "core/TextRender.h"

#include "core/Blend.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkTypeface_win.h"

extern "C" {
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
}

#ifndef STB_TRUETYPE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#endif
#include "imstb_truetype.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace ie {

static int g_skiaPathHits = 0;
static int g_ftOutlineHits = 0;
int textSkiaPathHitsImpl() { return g_skiaPathHits; }
int textFtOutlineHitsImpl() { return g_ftOutlineHits; }

namespace {

struct FontFace {
    stbtt_fontinfo info{};
    std::vector<unsigned char> data;
    std::string path;
    bool ok = false;

    sk_sp<SkTypeface> tf;
    float upem = 0.0f;
    FT_Face ft = nullptr;

    bool load(const std::string& p)
    {
        ok = false;
        FILE* f = nullptr;
#ifdef _WIN32
        fopen_s(&f, p.c_str(), "rb");
#else
        f = std::fopen(p.c_str(), "rb");
#endif
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n <= 0) {
            std::fclose(f);
            return false;
        }
        data.resize((size_t)n);
        const size_t rd = std::fread(data.data(), 1, data.size(), f);
        std::fclose(f);
        if (rd != data.size()) return false;

        const int offset = stbtt_GetFontOffsetForIndex(data.data(), 0);
        if (offset < 0) return false;
        ok = stbtt_InitFont(&info, data.data(), offset) != 0;
        if (ok) {
            path = p;
            static sk_sp<SkFontMgr> mgr = SkFontMgr_New_DirectWrite();
            tf = mgr ? mgr->makeFromFile(p.c_str(), 0) : nullptr;
            upem = readUpem(data.data(), (size_t)data.size(), (size_t)offset);
            ft = openFtFace(p);
        }
        return ok;
    }

    static FT_Face openFtFace(const std::string& p)
    {
        static FT_Library lib = [] {
            FT_Library l = nullptr;
            return FT_Init_FreeType(&l) == 0 ? l : nullptr;
        }();
        if (!lib) return nullptr;
        FT_Face f = nullptr;
        return FT_New_Face(lib, p.c_str(), 0, &f) == 0 ? f : nullptr;
    }

private:
    static float readUpem(const unsigned char* d, size_t size, size_t faceOffset)
    {
        auto be16 = [](const unsigned char* p) { return (unsigned)((p[0] << 8) | p[1]); };
        auto be32 = [](const unsigned char* p) {
            return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
        };
        if (size < faceOffset + 12) return 0.0f;
        const unsigned numTables = be16(d + faceOffset + 4);
        for (unsigned i = 0; i < numTables; ++i) {
            const size_t e = faceOffset + 12 + (size_t)i * 16;
            if (e + 16 > size) return 0.0f;
            if (std::memcmp(d + e, "head", 4) != 0) continue;
            const uint32_t off = be32(d + e + 8);
            if ((size_t)off + 18 >= size) return 0.0f;
            return (float)be16(d + off + 18);
        }
        return 0.0f;
    }
};

struct FontEntry {
    const char* family;
    const char* path;
};

const FontEntry kFonts[] = {
    {"微软雅黑", "C:/Windows/Fonts/msyh.ttc"},
    {"微软雅黑 Bold", "C:/Windows/Fonts/msyhbd.ttc"},
    {"微软雅黑 Light", "C:/Windows/Fonts/msyhl.ttc"},
    {"宋体", "C:/Windows/Fonts/simsun.ttc"},
    {"黑体", "C:/Windows/Fonts/simhei.ttf"},
    {"楷体", "C:/Windows/Fonts/simkai.ttf"},
    {"仿宋", "C:/Windows/Fonts/simfang.ttf"},
    {"等线", "C:/Windows/Fonts/deng.ttf"},
    {"等线 Light", "C:/Windows/Fonts/dengl.ttf"},
    {"Segoe UI", "C:/Windows/Fonts/segoeui.ttf"},
    {"Arial", "C:/Windows/Fonts/arial.ttf"},
    {"Times New Roman", "C:/Windows/Fonts/times.ttf"},
    {"Consolas", "C:/Windows/Fonts/consola.ttf"},
    {"Courier New", "C:/Windows/Fonts/cour.ttf"},
    {"Verdana", "C:/Windows/Fonts/verdana.ttf"},
    {"Tahoma", "C:/Windows/Fonts/tahoma.ttf"},
};

bool fileExists(const char* p)
{
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, p, "rb");
#else
    f = std::fopen(p, "rb");
#endif
    if (!f) return false;
    std::fclose(f);
    return true;
}

struct Registry {
    std::vector<FontEntry> list;
    std::vector<std::unique_ptr<FontFace>> faces;
};

Registry& registry()
{
    static Registry r;
    static bool init = false;
    if (!init) {
        init = true;
        for (const FontEntry& e : kFonts) {
            if (!fileExists(e.path)) continue;
            r.list.push_back(e);
            r.faces.emplace_back(nullptr);
        }
    }
    return r;
}

int indexOfFamily(const std::string& family)
{
    Registry& r = registry();
    for (int i = 0; i < (int)r.list.size(); ++i)
        if (family == r.list[(size_t)i].family) return i;
    return -1;
}

const FontFace* faceForFamily(const std::string& family)
{
    Registry& r = registry();
    if (r.list.empty()) return nullptr;
    int i = family.empty() ? 0 : indexOfFamily(family);
    if (i < 0 || i >= (int)r.list.size()) i = 0;
    auto& slot = r.faces[(size_t)i];
    if (!slot) {
        auto f = std::make_unique<FontFace>();
        f->load(r.list[(size_t)i].path);
        slot = std::move(f);
    }
    return slot->ok ? slot.get() : nullptr;
}

const FontFace* defaultFace()
{
    Registry& r = registry();
    if (r.list.empty()) return nullptr;
    return faceForFamily(r.list[0].family);
}

float scaleFor(const FontFace& f, float px) { return stbtt_ScaleForPixelHeight(&f.info, px); }

unsigned nextCp(const std::string& s, std::size_t& i)
{
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x80) {
        ++i;
        return c;
    }
    if ((c >> 5) == 0x6) {
        const unsigned v = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu);
        i += 2;
        return v;
    }
    if ((c >> 4) == 0xE) {
        const unsigned v = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu);
        i += 3;
        return v;
    }
    const unsigned v = ((c & 0x07u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) |
                       (s[i + 3] & 0x3Fu);
    i += 4;
    return v;
}

const FontFace* pickFace(const FontFace* main, unsigned cp)
{
    if (!main) return cp ? defaultFace() : nullptr;
    if (stbtt_FindGlyphIndex(&main->info, (int)cp) != 0) return main;
    const FontFace* fb = defaultFace();
    if (fb && fb != main && stbtt_FindGlyphIndex(&fb->info, (int)cp) != 0) return fb;
    return main;
}

struct Glyph {
    const FontFace* face = nullptr;
    float scale = 0.0f;
    unsigned cp = 0;
    unsigned gid = 0;
    float advance = 0.0f;
    bool newline = false;
};

template <class Fn>
void walkGlyphs(const TextData& t, Fn fn)
{
    const float px = (float)std::max(1, t.size);
    const FontFace* main = faceForFamily(t.font);
    if (!main) return;
    std::size_t i = 0;
    unsigned prevCp = 0;
    unsigned prevGid = 0;
    const FontFace* prevFace = nullptr;
    while (i < t.text.size()) {
        const std::size_t start = i;
        const unsigned cp = nextCp(t.text, i);
        Glyph g;
        g.cp = cp;
        if (cp == '\r') continue;
        if (cp == '\n') {
            g.newline = true;
            fn(start, i, g);
            prevCp = 0;
            prevFace = nullptr;
            continue;
        }
        const FontFace* f = pickFace(main, cp);
        g.face = f;
        g.scale = scaleFor(*f, px);
        const float em = f->upem * g.scale;
        bool skiaOk = false;
        if (f->tf && f->upem > 0.0f) {
            const SkGlyphID gid = f->tf->unicharToGlyph((SkUnichar)cp);
            if (gid) {
                g.gid = (unsigned)gid;
                SkFont font(f->tf, em);
                font.setHinting(SkFontHinting::kNone);
                font.setSubpixel(true);
                font.setLinearMetrics(true);
                SkScalar w = 0.0f;
                font.getWidths(&gid, 1, &w);
                g.advance = (float)w;
                if (prevFace == f && prevCp && prevGid) {
                    const SkGlyphID pair[2] = {(SkGlyphID)prevGid, gid};
                    int32_t adj[1] = {0};
                    if (f->tf->getKerningPairAdjustments(pair, 2, adj))
                        g.advance += (float)adj[0] * g.scale;
                }
                skiaOk = true;
            }
        }
        if (!skiaOk) {
            g.gid = (unsigned)stbtt_FindGlyphIndex(&f->info, (int)cp);
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&f->info, (int)cp, &adv, &lsb);
            g.advance = adv * g.scale;
            if (prevFace == f && prevCp)
                g.advance += stbtt_GetCodepointKernAdvance(&f->info, (int)prevCp, (int)cp) * g.scale;
        }
        fn(start, i, g);
        prevCp = cp;
        prevGid = g.gid;
        prevFace = f;
    }
}

}

namespace {

struct FtCtx {
    SkPath* p;
};
int ftMove(const FT_Vector* to, void* u)
{
    ((FtCtx*)u)->p->moveTo((float)to->x / 64.0f, -(float)to->y / 64.0f);
    return 0;
}
int ftLine(const FT_Vector* to, void* u)
{
    ((FtCtx*)u)->p->lineTo((float)to->x / 64.0f, -(float)to->y / 64.0f);
    return 0;
}
int ftConic(const FT_Vector* c, const FT_Vector* to, void* u)
{
    ((FtCtx*)u)->p->quadTo((float)c->x / 64.0f, -(float)c->y / 64.0f, (float)to->x / 64.0f,
                           -(float)to->y / 64.0f);
    return 0;
}
int ftCubic(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* u)
{
    ((FtCtx*)u)->p->cubicTo((float)c1->x / 64.0f, -(float)c1->y / 64.0f, (float)c2->x / 64.0f,
                            -(float)c2->y / 64.0f, (float)to->x / 64.0f, -(float)to->y / 64.0f);
    return 0;
}

static bool skiaGlyphPath(const FontFace& face, unsigned gid, float emPx, SkPath& out)
{
    if (!face.tf || !gid || emPx <= 0.0f) return false;
    SkFont font(face.tf, emPx);
    font.setHinting(SkFontHinting::kNone);
    font.setSubpixel(true);
    font.setLinearMetrics(true);
    font.setEdging(SkFont::Edging::kAntiAlias);
    out.reset();
    if (!font.getPath((SkGlyphID)gid, &out) || out.isEmpty()) return false;
    ++g_skiaPathHits;
    return true;
}

bool hintedGlyphPath(const FontFace& face, unsigned gid, float emPx, SkPath& out)
{
    if (skiaGlyphPath(face, gid, emPx, out)) return true;
    if (!face.ft || emPx <= 0.0f) return false;
    const FT_F26Dot6 size = (FT_F26Dot6)std::lround(emPx * 64.0f);
    if (FT_Set_Char_Size(face.ft, 0, size, 72, 72) != 0) return false;
    if (FT_Load_Glyph(face.ft, (FT_UInt)gid, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) return false;
    if (face.ft->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return false;
    FT_Outline_Funcs f{};
    f.move_to = ftMove;
    f.line_to = ftLine;
    f.conic_to = ftConic;
    f.cubic_to = ftCubic;
    f.shift = 0;
    f.delta = 0;
    FtCtx ctx{&out};
    const bool ok = FT_Outline_Decompose(&face.ft->glyph->outline, &f, &ctx) == 0 && !out.isEmpty();
    if (ok) g_ftOutlineHits++;
    return ok;
}

}

int textFtOutlineHits() { return textFtOutlineHitsImpl(); }
int textSkiaPathHits() { return textSkiaPathHitsImpl(); }
bool textFontReady() { return defaultFace() != nullptr; }
const char* textFontPath()
{
    Registry& r = registry();
    if (r.list.empty()) return "";
    return r.list[0].path;
}

int textFontCount() { return (int)registry().list.size(); }
const char* textFontName(int index)
{
    Registry& r = registry();
    if (index < 0 || index >= (int)r.list.size()) return "";
    return r.list[(size_t)index].family;
}
const char* textFontFile(int index)
{
    Registry& r = registry();
    if (index < 0 || index >= (int)r.list.size()) return "";
    return r.list[(size_t)index].path;
}
int textFontIndex(const std::string& family) { return indexOfFamily(family); }

ShapedText shapeText(const TextData& t)
{
    ShapedText s;
    const FontFace* main = faceForFamily(t.font);
    if (!main) return s;

    const float px = (float)std::max(1, t.size);
    const float mainScale = scaleFor(*main, px);
    if (main->tf && main->upem > 0.0f) {
        SkFont font(main->tf, main->upem * mainScale);
        font.setHinting(SkFontHinting::kNone);
        font.setSubpixel(true);
        font.setLinearMetrics(true);
        SkFontMetrics fm;
        font.getMetrics(&fm);
        s.ascent = (float)(-fm.fAscent);
        s.descent = (float)fm.fDescent;
        s.lineH = s.ascent + s.descent + (float)fm.fLeading + t.lineSpacing;
    } else {
        int asc = 0, dsc = 0, gap = 0;
        stbtt_GetFontVMetrics(&main->info, &asc, &dsc, &gap);
        s.ascent = (float)asc * mainScale;
        s.descent = (float)(-dsc) * mainScale;
        s.lineH = (float)(asc - dsc) * mainScale + t.lineSpacing;
    }

    float penX = 0.0f;
    float baseline = s.ascent;
    float lineW = 0.0f;
    bool any = false;
    ShapedLine first;
    first.baselineY = baseline;
    first.top = baseline - s.ascent;
    first.bottom = baseline + s.descent;
    first.startByte = 0;
    s.lines.push_back(first);

    walkGlyphs(t, [&](std::size_t start, std::size_t end, const Glyph& g) {
        if (g.newline) {
            ShapedLine& L = s.lines.back();
            L.endByte = (int)start;
            s.width = std::max(s.width, lineW);
            lineW = 0.0f;
            penX = 0.0f;
            baseline += s.lineH;
            ShapedLine nl;
            nl.baselineY = baseline;
            nl.top = baseline - s.ascent;
            nl.bottom = baseline + s.descent;
            nl.startByte = (int)end;
            s.lines.push_back(nl);
            return;
        }
        ShapedGlyph sg;
        sg.face = g.face;
        sg.gid = g.gid;
        sg.cp = g.cp;
        sg.x = penX;
        sg.y = baseline;
        sg.adv = g.advance;
        sg.em = g.face ? g.face->upem * g.scale : 0.0f;
        sg.i0 = (int)start;
        sg.i1 = (int)end;
        s.glyphs.push_back(sg);
        penX += g.advance;
        lineW += g.advance;
        any = true;
    });

    s.width = std::max(s.width, lineW);
    s.lines.back().endByte = (int)t.text.size();
    for (std::size_t li = 0; li < s.lines.size(); ++li) {
        const ShapedLine& L = s.lines[li];
        for (std::size_t gi = 0; gi < s.glyphs.size(); ++gi) {
            ShapedGlyph& g = s.glyphs[gi];
            if (g.line >= 0) continue;
            if (g.i0 >= L.startByte && g.i0 < L.endByte) g.line = (int)li;
        }
    }
    s.ok = true;
    return s;
}

bool shapedOutline(const ShapedText& s, SkPath& out)
{
    out.reset();
    bool any = false;
    for (std::size_t i = 0; i < s.glyphs.size(); ++i) {
        const ShapedGlyph& g = s.glyphs[i];
        const FontFace* f = (const FontFace*)g.face;
        if (!f || !g.gid || !f->tf || f->upem <= 0.0f || g.em <= 0.0f) continue;
        SkPath gp;
        if (!hintedGlyphPath(*f, g.gid, g.em, gp)) continue;
        gp.offset(g.x, g.y);
        out.addPath(gp);
        any = true;
    }
    return any;
}

TextMetrics measureText(const TextData& t)
{
    TextMetrics m;
    if (t.text.empty()) return m;
    const ShapedText s = shapeText(t);
    if (!s.ok) return m;
    m.ascent = s.ascent;
    m.descent = s.descent;
    m.lineH = s.lineH;
    m.width = s.width;
    m.lines = (int)s.lines.size();
    m.ok = true;
    return m;
}

bool drawText(Image& img, const TextData& t, Rect& dirty, const uint8_t* mask, const SkMatrix* xf)
{
    const ShapedText s = shapeText(t);
    if (!s.ok || img.empty()) return false;

    const uint32_t col = t.color.packed();
    const float op = std::clamp(t.opacity, 0.0f, 1.0f);
    if (op <= 0.0f) return false;
    const int W = img.width(), H = img.height();

    SkImageInfo info = SkImageInfo::Make(W, H, kN32_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surf = SkSurfaces::Raster(info);
    if (!surf) return false;
    SkCanvas* cv = surf->getCanvas();
    cv->clear(SK_ColorTRANSPARENT);
    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    paint.setAntiAlias(true);

    const float px = (float)std::max(1, t.size);
    bool any = false;

    std::vector<uint8_t> stbCov;
    auto drawGlyphStb = [&](const FontFace& face, int cp, float sc, float pen, float base) {
        if (stbCov.empty()) stbCov.assign((size_t)W * (size_t)H, 0);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBoxSubpixel(&face.info, cp, sc, sc, 0.0f, 0.0f, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        if (gw <= 0 || gh <= 0) return;
        std::vector<unsigned char> bmp((size_t)gw * (size_t)gh, 0);
        stbtt_MakeCodepointBitmap(&face.info, bmp.data(), gw, gh, gw, sc, sc, cp);
        const int bx0 = (int)std::floor(pen) + x0, by0 = (int)std::floor(base) + y0;
        for (int y = std::max(0, by0); y < std::min(H, by0 + gh); ++y) {
            for (int x = std::max(0, bx0); x < std::min(W, bx0 + gw); ++x) {
                const unsigned char v = bmp[(size_t)(y - by0) * (size_t)gw + (size_t)(x - bx0)];
                uint8_t& d = stbCov[(size_t)y * (size_t)W + (size_t)x];
                if (v > d) d = v;
            }
        }
        any = true;
    };

    for (std::size_t i = 0; i < s.glyphs.size(); ++i) {
        const ShapedGlyph& g = s.glyphs[i];
        const FontFace* f = (const FontFace*)g.face;
        if (!f) continue;
        const float pen = t.x + g.x, base = t.y + g.y;
        SkPath gp;
        if (g.gid && g.em > 0.0f && hintedGlyphPath(*f, g.gid, g.em, gp)) {
            gp.offset(pen, base);
            if (xf) gp.transform(*xf);
            cv->drawPath(gp, paint);
            any = true;
            continue;
        }
        drawGlyphStb(*f, (int)g.cp, scaleFor(*f, px), pen, base);
    }

    if (!any) return false;
    SkPixmap pm;
    if (!surf->peekPixels(&pm)) return false;

    int minx = W, miny = H, maxx = -1, maxy = -1;
    for (int y = 0; y < H; ++y) {
        const uint32_t* row = pm.addr32(0, y);
        uint32_t* dst = img.pixels() + (size_t)y * W;
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t)y * W + x;
            uint32_t a8 = row[x] >> 24;
            if (!stbCov.empty() && stbCov[i] > (uint8_t)a8) a8 = stbCov[i];
            if (a8 == 0u) continue;
            float a = (float)a8 / 255.0f * op;
            if (mask) a *= (float)mask[i] / 255.0f;
            if (a <= 0.004f) continue;
            dst[x] = blend::overPixel(dst[x], col, a);
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
    }
    if (maxx >= 0 && maxy >= 0)
        dirty = dirty.unite(Rect{minx, miny, maxx - minx + 1, maxy - miny + 1});
    return true;
}

bool drawGlyphPath(Image& img, const SkPath& path, Color color, float opacity, Rect& dirty, const uint8_t* mask)
{
    if (img.empty() || path.isEmpty()) return false;
    const int W = img.width(), H = img.height();
    const uint32_t col = color.packed();
    const float op = std::clamp(opacity, 0.0f, 1.0f);
    if (op <= 0.0f) return false;

    SkImageInfo info = SkImageInfo::Make(W, H, kN32_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surf = SkSurfaces::Raster(info);
    if (!surf) return false;
    SkCanvas* cv = surf->getCanvas();
    cv->clear(SK_ColorTRANSPARENT);
    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    paint.setAntiAlias(true);
    cv->drawPath(path, paint);

    SkPixmap pm;
    if (!surf->peekPixels(&pm)) return false;
    int minx = W, miny = H, maxx = -1, maxy = -1;
    for (int y = 0; y < H; ++y) {
        const uint32_t* row = pm.addr32(0, y);
        uint32_t* dst = img.pixels() + (size_t)y * W;
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t)y * W + x;
            uint8_t a8 = (uint8_t)(row[x] >> 24);
            if (a8 == 0u) continue;
            float a = (float)a8 / 255.0f * op;
            if (mask) a *= (float)mask[i] / 255.0f;
            if (a <= 0.004f) continue;
            dst[x] = blend::overPixel(dst[x], col, a);
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
    }
    if (maxx >= 0 && maxy >= 0)
        dirty = dirty.unite(Rect{minx, miny, maxx - minx + 1, maxy - miny + 1});
    return true;
}

bool textGlyphPath(const TextData& t, SkPath& out)
{
    out.reset();
    const ShapedText s = shapeText(t);
    if (!s.ok) return false;
    if (!shapedOutline(s, out)) return false;
    out.offset(t.x, t.y);
    return true;
}

namespace {

int clampOffset(const TextData& t, int off)
{
    if (off <= 0) return 0;
    const int len = (int)t.text.size();
    if (off >= len) return len;
    while (off < len && ((unsigned char)t.text[(size_t)off] & 0xC0) == 0x80) ++off;
    return off;
}

}

Rect textBoxRect(const TextData& t)
{
    const TextMetrics m = measureText(t);
    if (!m.ok || m.lines <= 0 || m.width <= 0.0f) return Rect{};
    const int x = (int)std::floor((double)t.x);
    const int y = (int)std::floor((double)t.y);
    const int w = std::max(1, (int)std::ceil((double)m.width));
    const int h = std::max(1, (int)std::ceil((double)(m.lineH * (float)m.lines)));
    return Rect{x, y, w, h};
}

TextCaret textCaretAt(const TextData& t, int byteOffset)
{
    TextCaret c;
    const ShapedText s = shapeText(t);
    if (!s.ok || s.lines.empty()) return c;
    const int off = clampOffset(t, byteOffset);

    int li = 0;
    for (int i = 0; i < (int)s.lines.size(); ++i)
        if (off >= s.lines[(size_t)i].startByte) li = i;
    const ShapedLine& L = s.lines[(size_t)li];

    float x = 0.0f;
    for (std::size_t i = 0; i < s.glyphs.size(); ++i) {
        const ShapedGlyph& g = s.glyphs[i];
        if (g.line != li) continue;
        if (off >= g.i1)
            x = g.x + g.adv;
        else if (off > g.i0) {
            x = g.x;
            break;
        } else
            break;
    }
    c.x = t.x + x;
    c.y = t.y + L.top;
    c.h = std::max(2.0f, L.bottom - L.top);
    return c;
}

int textOffsetAt(const TextData& t, float x, float y)
{
    const ShapedText s = shapeText(t);
    if (!s.ok || s.lines.empty()) return 0;

    const float relY = y - t.y;
    int want = 0;
    for (int i = 0; i < (int)s.lines.size(); ++i)
        if (relY >= s.lines[(size_t)i].top) want = i;

    const float relX = x - t.x;
    int result = s.lines[(size_t)want].endByte;
    for (std::size_t i = 0; i < s.glyphs.size(); ++i) {
        const ShapedGlyph& g = s.glyphs[i];
        if (g.line != want) continue;
        if (relX < g.x + g.adv * 0.5f) {
            result = g.i0;
            break;
        }
        result = g.i1;
    }
    return clampOffset(t, result);
}

int textSelectionRects(const TextData& t, int a, int b, Rect* out, int max)
{
    if (!out || max <= 0) return 0;
    if (a > b) std::swap(a, b);
    a = clampOffset(t, a);
    b = clampOffset(t, b);
    if (a >= b) return 0;

    const ShapedText s = shapeText(t);
    if (!s.ok || s.lines.empty()) return 0;

    int n = 0;
    for (std::size_t li = 0; li < s.lines.size() && n < max; ++li) {
        const ShapedLine& L = s.lines[li];
        float xs = -1.0f, xe = 0.0f;
        for (std::size_t gi = 0; gi < s.glyphs.size(); ++gi) {
            const ShapedGlyph& g = s.glyphs[gi];
            if (g.line != (int)li) continue;
            if (g.i1 <= a || g.i0 >= b) continue;
            if (xs < 0.0f) xs = g.x;
            xe = g.x + g.adv;
        }
        if (xs < 0.0f) continue;
        out[n++] = Rect{(int)std::floor(t.x + xs), (int)std::floor(t.y + L.top),
                        std::max(1, (int)std::ceil(xe - xs)),
                        std::max(1, (int)std::ceil(L.bottom - L.top))};
    }
    return n;
}

}
