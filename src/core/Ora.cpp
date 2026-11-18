#include "core/Ora.h"

#include "core/ImageIo.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace ie {
namespace ora {
namespace {

std::string g_error;
void fail(const std::string& msg) { g_error = msg; }

uint16_t rd16(const std::vector<uint8_t>& b, size_t p) { return (uint16_t)(b[p] | (b[p + 1] << 8)); }
uint32_t rd32(const std::vector<uint8_t>& b, size_t p)
{
    return (uint32_t)b[p] | ((uint32_t)b[p + 1] << 8) | ((uint32_t)b[p + 2] << 16) | ((uint32_t)b[p + 3] << 24);
}
void wr16(std::vector<uint8_t>& b, uint16_t v)
{
    b.push_back((uint8_t)(v & 0xff));
    b.push_back((uint8_t)((v >> 8) & 0xff));
}
void wr32(std::vector<uint8_t>& b, uint32_t v)
{
    wr16(b, (uint16_t)(v & 0xffff));
    wr16(b, (uint16_t)((v >> 16) & 0xffff));
}

bool inflateData(const uint8_t* src, size_t srcLen, size_t usize, std::vector<uint8_t>& out)
{
    out.resize(usize);
    if (usize == 0) return true;
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = (uInt)srcLen;
    zs.next_out = out.data();
    zs.avail_out = (uInt)usize;
    const int rc = inflate(&zs, Z_FINISH);
    out.resize((size_t)zs.total_out);
    inflateEnd(&zs);
    return rc == Z_STREAM_END;
}

struct ZipEntry {
    std::string name;
    std::vector<uint8_t> data;
};

bool writeZip(const std::string& path, const std::vector<ZipEntry>& entries)
{
    std::vector<uint8_t> out;
    out.reserve(1u << 16);

    struct Central {
        std::string name;
        uint32_t crc, size, offset;
    };
    std::vector<Central> cd;

    const uint16_t dosDate = 0x5021;

    for (const ZipEntry& e : entries) {
        const uint32_t crc = e.data.empty() ? 0u : (uint32_t)::crc32(0L, e.data.data(), (uInt)e.data.size());
        const uint32_t offset = (uint32_t)out.size();
        const uint16_t nlen = (uint16_t)e.name.size();

        wr32(out, 0x04034b50u);
        wr16(out, 20);
        wr16(out, 0x0800);
        wr16(out, 0);
        wr16(out, 0);
        wr16(out, dosDate);
        wr32(out, crc);
        wr32(out, (uint32_t)e.data.size());
        wr32(out, (uint32_t)e.data.size());
        wr16(out, nlen);
        wr16(out, 0);
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), e.data.begin(), e.data.end());

        cd.push_back(Central{e.name, crc, (uint32_t)e.data.size(), offset});
    }

    const uint32_t cdStart = (uint32_t)out.size();
    for (const Central& e : cd) {
        wr32(out, 0x02014b50u);
        wr16(out, 20);
        wr16(out, 20);
        wr16(out, 0x0800);
        wr16(out, 0);
        wr16(out, 0);
        wr16(out, dosDate);
        wr32(out, e.crc);
        wr32(out, e.size);
        wr32(out, e.size);
        wr16(out, (uint16_t)e.name.size());
        wr16(out, 0);
        wr16(out, 0);
        wr16(out, 0);
        wr16(out, 0);
        wr32(out, 0);
        wr32(out, e.offset);
        out.insert(out.end(), e.name.begin(), e.name.end());
    }
    const uint32_t cdSize = (uint32_t)out.size() - cdStart;

    wr32(out, 0x06054b50u);
    wr16(out, 0);
    wr16(out, 0);
    wr16(out, (uint16_t)cd.size());
    wr16(out, (uint16_t)cd.size());
    wr32(out, cdSize);
    wr32(out, cdStart);
    wr16(out, 0);

    if (!imageio::writeBytes(path, out)) {
        fail("写入文件失败：" + path);
        return false;
    }
    return true;
}

bool readZip(const std::vector<uint8_t>& b, std::vector<ZipEntry>& out)
{
    if (b.size() < 22) return false;
    int64_t eocd = -1;
    for (int64_t i = (int64_t)b.size() - 22; i >= 0; --i) {
        if (rd32(b, (size_t)i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) return false;

    const uint16_t count = rd16(b, (size_t)eocd + 10);
    const uint32_t cdOffset = rd32(b, (size_t)eocd + 16);

    size_t p = cdOffset;
    for (uint16_t i = 0; i < count; ++i) {
        if (p + 46 > b.size() || rd32(b, p) != 0x02014b50u) return false;
        const uint16_t method = rd16(b, p + 10);
        const uint32_t csize = rd32(b, p + 20);
        const uint32_t usize = rd32(b, p + 24);
        const uint16_t nlen = rd16(b, p + 28);
        const uint16_t elen = rd16(b, p + 30);
        const uint16_t clen = rd16(b, p + 32);
        const uint32_t loff = rd32(b, p + 42);
        if (p + 46 + nlen > b.size()) return false;
        std::string name(reinterpret_cast<const char*>(b.data() + p + 46), nlen);
        p += 46 + nlen + elen + clen;

        if ((size_t)loff + 30 > b.size() || rd32(b, (size_t)loff) != 0x04034b50u) return false;
        const uint16_t lnlen = rd16(b, (size_t)loff + 26);
        const uint16_t lelen = rd16(b, (size_t)loff + 28);
        const size_t dpos = (size_t)loff + 30 + lnlen + lelen;
        if (dpos + csize > b.size()) return false;

        ZipEntry e;
        e.name = std::move(name);
        if (method == 0) {
            e.data.assign(b.begin() + (ptrdiff_t)dpos, b.begin() + (ptrdiff_t)(dpos + csize));
        } else if (method == 8) {
            if (!inflateData(b.data() + dpos, csize, usize, e.data)) continue;
        } else {
            continue;
        }
        out.push_back(std::move(e));
    }
    return true;
}

const ZipEntry* findEntry(const std::vector<ZipEntry>& es, const std::string& name)
{
    for (const ZipEntry& e : es)
        if (e.name == name) return &e;
    return nullptr;
}

std::string xmlEscape(const std::string& s)
{
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        case '\'': o += "&apos;"; break;
        default: o += c;
        }
    }
    return o;
}

std::string xmlUnescape(const std::string& s)
{
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;") == 0) { o += '&'; i += 5; continue; }
            if (s.compare(i, 4, "&lt;") == 0) { o += '<'; i += 4; continue; }
            if (s.compare(i, 4, "&gt;") == 0) { o += '>'; i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { o += '"'; i += 6; continue; }
            if (s.compare(i, 6, "&apos;") == 0) { o += '\''; i += 6; continue; }
        }
        o += s[i++];
    }
    return o;
}

std::string xmlEscapeText(const std::string& s)
{
    std::string o = xmlEscape(s);
    std::string r;
    r.reserve(o.size());
    for (char c : o) {
        if (c == '\n') r += "&#10;";
        else if (c != '\r') r += c;
    }
    return r;
}

std::string xmlUnescapeText(const std::string& s)
{
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s.compare(i, 5, "&#10;") == 0) { o += '\n'; i += 5; continue; }
        if (s.compare(i, 5, "&#13;") == 0) { i += 5; continue; }
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;") == 0) { o += '&'; i += 5; continue; }
            if (s.compare(i, 4, "&lt;") == 0) { o += '<'; i += 4; continue; }
            if (s.compare(i, 4, "&gt;") == 0) { o += '>'; i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { o += '"'; i += 6; continue; }
            if (s.compare(i, 6, "&apos;") == 0) { o += '\''; i += 6; continue; }
        }
        o += s[i++];
    }
    return o;
}

Color parseHexColor(const std::string& s, Color fallback)
{
    std::string h;
    for (char c : s) {
        if (c == '#') continue;
        if (std::isxdigit((unsigned char)c)) h += c;
    }
    if (h.size() < 6) return fallback;
    const auto byteAt = [&h](size_t at) { return (uint8_t)std::strtoul(h.substr(at, 2).c_str(), nullptr, 16); };
    Color c = fallback;
    c.r = byteAt(0);
    c.g = byteAt(2);
    c.b = byteAt(4);
    c.a = h.size() >= 8 ? byteAt(6) : 255;
    return c;
}

std::string hexColor(const float c[4])
{
    const auto byteOf = [](float v) {
        const int i = (int)(v * 255.0f + (v >= 0.0f ? 0.5f : -0.5f));
        return (unsigned)std::clamp(i, 0, 255);
    };
    char b[16];
    std::snprintf(b, sizeof(b), "#%02X%02X%02X%02X", byteOf(c[0]), byteOf(c[1]), byteOf(c[2]), byteOf(c[3]));
    return std::string(b);
}

void colorToFloats(const Color& c, float out[4])
{
    out[0] = c.r / 255.0f;
    out[1] = c.g / 255.0f;
    out[2] = c.b / 255.0f;
    out[3] = c.a / 255.0f;
}

std::string attr(const std::string& tag, const char* key){
    const std::string pat = std::string(" ") + key + "=";
    size_t p = tag.find(pat);
    if (p == std::string::npos) return {};
    size_t v = p + pat.size();
    if (v >= tag.size()) return {};
    const char q = tag[v];
    if (q != '"' && q != '\'') return {};
    ++v;
    const size_t e = tag.find(q, v);
    if (e == std::string::npos) return {};
    return tag.substr(v, e - v);
}

std::string styleAttrs(const LayerStyle& s)
{
    const auto f = [](float v) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.3f", (double)v);
        return std::string(b);
    };
    std::string o;
    o += " style-stroke=\"" + std::string(s.strokeOn ? "1" : "0") + "\"";
    o += " style-stroke-w=\"" + f(s.strokeWidth) + "\"";
    o += " style-stroke-pos=\"" + std::to_string((int)s.strokePos) + "\"";
    o += " style-stroke-op=\"" + f(s.strokeOpacity) + "\"";
    o += " style-stroke-color=\"" + hexColor(s.strokeColor) + "\"";
    o += " style-shadow=\"" + std::string(s.shadowOn ? "1" : "0") + "\"";
    o += " style-shadow-d=\"" + f(s.shadowDist) + "\"";
    o += " style-shadow-size=\"" + f(s.shadowSize) + "\"";
    o += " style-shadow-angle=\"" + f(s.shadowAngle) + "\"";
    o += " style-shadow-op=\"" + f(s.shadowOpacity) + "\"";
    o += " style-shadow-color=\"" + hexColor(s.shadowColor) + "\"";
    o += " style-overlay=\"" + std::string(s.overlayOn ? "1" : "0") + "\"";
    o += " style-overlay-op=\"" + f(s.overlayOpacity) + "\"";
    o += " style-overlay-color=\"" + hexColor(s.overlayColor) + "\"";
    return o;
}

std::string childAttrs(const std::vector<LayerChild>& kids)
{
    std::string o;
    int n = 0;
    for (const auto& c : kids) {
        if (c.id == 0) continue;
        if (c.kind == ChildKind::Lift) {
            const std::string p = " lift" + std::to_string(n);
            const auto kv2 = [&o, &p](const char* k, const std::string& v) { o += p + k + "=\"" + v + "\""; };
            char lb[48];
            std::snprintf(lb, sizeof(lb), "%d", c.id);
            kv2("-id", lb);
            kv2("-on", c.on ? "1" : "0");
            std::snprintf(lb, sizeof(lb), "%.3f", (double)c.liftStrength);
            kv2("-s", lb);
            std::snprintf(lb, sizeof(lb), "%.3f", (double)c.liftRadius);
            kv2("-r", lb);
            std::snprintf(lb, sizeof(lb), "%.3f", (double)c.liftSensitivity);
            kv2("-k", lb);
            std::snprintf(lb, sizeof(lb), "%.3f", (double)c.liftSoftness);
            kv2("-f", lb);
            std::snprintf(lb, sizeof(lb), "%.3f", (double)c.liftMaxGain);
            kv2("-g", lb);
            ++n;
            continue;
        }
        if (c.kind != ChildKind::Tri) continue;
        const std::string p = " tri" + std::to_string(n);
        const auto kv = [&o, &p](const char* k, const std::string& v) { o += p + k + "=\"" + v + "\""; };
        char b[48];
        std::snprintf(b, sizeof(b), "%d", c.id);
        kv("-id", b);
        kv("-on", c.on ? "1" : "0");
        std::snprintf(b, sizeof(b), "%.3f", (double)c.r);
        kv("-r", b);
        std::snprintf(b, sizeof(b), "%.3f", (double)c.g);
        kv("-g", b);
        std::snprintf(b, sizeof(b), "%.3f", (double)c.b);
        kv("-b", b);
        kv("-luma", c.keepLuma ? "1" : "0");
        ++n;
    }
    return o;
}

std::vector<LayerChild> parseChildAttrs(const std::string& tag)
{
    std::vector<LayerChild> out;
    for (int n = 0; n < 8; ++n) {
        const std::string p = "tri" + std::to_string(n);
        if (tag.find(" " + p + "-id=") == std::string::npos) break;
        LayerChild c;
        c.kind = ChildKind::Tri;
        c.id = std::atoi(attr(tag, (p + "-id").c_str()).c_str());
        c.on = attr(tag, (p + "-on").c_str()) != "0";
        const auto num = [&tag, &p](const char* k, float dflt) {
            const std::string v = attr(tag, (p + k).c_str());
            return v.empty() ? dflt : (float)std::atof(v.c_str());
        };
        c.r = std::clamp(num("-r", 1.0f), 0.0f, 2.0f);
        c.g = std::clamp(num("-g", 1.0f), 0.0f, 2.0f);
        c.b = std::clamp(num("-b", 1.0f), 0.0f, 2.0f);
        c.keepLuma = attr(tag, (p + "-luma").c_str()) == "1";
        if (c.id != 0) out.push_back(c);
    }
    for (int n = 0; n < 8; ++n) {
        const std::string p = "lift" + std::to_string(n);
        if (tag.find(" " + p + "-id=") == std::string::npos) break;
        LayerChild c;
        c.kind = ChildKind::Lift;
        c.id = std::atoi(attr(tag, (p + "-id").c_str()).c_str());
        c.on = attr(tag, (p + "-on").c_str()) != "0";
        const auto num = [&tag, &p](const char* k, float dflt) {
            const std::string v = attr(tag, (p + k).c_str());
            return v.empty() ? dflt : (float)std::atof(v.c_str());
        };
        c.liftStrength = std::clamp(num("-s", 0.0f), 0.0f, 1.0f);
        c.liftRadius = std::clamp(num("-r", 0.0f), 0.0f, 200.0f);
        c.liftSensitivity = std::clamp(num("-k", 0.5f), 0.2f, 0.9f);
        c.liftSoftness = std::clamp(num("-f", 0.0f), 0.0f, 200.0f);
        c.liftMaxGain = std::clamp(num("-g", 3.0f), 1.5f, 6.0f);
        if (c.id != 0) out.push_back(c);
    }
    return out;
}

LayerStyle parseStyleAttrs(const std::string& tag)
{
    LayerStyle s;
    if (tag.find(" style-stroke=") == std::string::npos) return s;
    const auto num = [&tag](const char* k, float dflt) {
        const std::string v = attr(tag, k);
        return v.empty() ? dflt : (float)std::atof(v.c_str());
    };
    s.strokeOn = attr(tag, "style-stroke") == "1";
    s.strokeWidth = num("style-stroke-w", s.strokeWidth);
    s.strokePos = (StrokePos)std::clamp(std::atoi(attr(tag, "style-stroke-pos").c_str()), 0, 2);
    s.strokeOpacity = num("style-stroke-op", s.strokeOpacity);
    colorToFloats(parseHexColor(attr(tag, "style-stroke-color"), Color(0, 0, 0, 255)), s.strokeColor);

    s.shadowOn = attr(tag, "style-shadow") == "1";
    s.shadowDist = num("style-shadow-d", s.shadowDist);
    s.shadowSize = num("style-shadow-size", s.shadowSize);
    s.shadowAngle = num("style-shadow-angle", s.shadowAngle);
    s.shadowOpacity = num("style-shadow-op", s.shadowOpacity);
    colorToFloats(parseHexColor(attr(tag, "style-shadow-color"), Color(0, 0, 0, 255)), s.shadowColor);

    s.overlayOn = attr(tag, "style-overlay") == "1";
    s.overlayOpacity = num("style-overlay-op", s.overlayOpacity);
    colorToFloats(parseHexColor(attr(tag, "style-overlay-color"), Color(255, 0, 0, 255)), s.overlayColor);
    return s;
}

const char* compositeOp(BlendMode m)
{
    switch (m) {
    case BlendMode::Multiply: return "svg:multiply";
    case BlendMode::Screen: return "svg:screen";
    case BlendMode::Overlay: return "svg:overlay";
    case BlendMode::Darken: return "svg:darken";
    case BlendMode::Lighten: return "svg:lighten";
    case BlendMode::Add: return "svg:plus";
    case BlendMode::ColorDodge: return "svg:color-dodge";
    case BlendMode::ColorBurn: return "svg:color-burn";
    case BlendMode::HardLight: return "svg:hard-light";
    case BlendMode::SoftLight: return "svg:soft-light";
    case BlendMode::Difference: return "svg:difference";
    case BlendMode::Exclusion: return "svg:exclusion";
    case BlendMode::Modulate: return "svg:modulate";
    case BlendMode::Hue: return "svg:hue";
    case BlendMode::Saturation: return "svg:saturation";
    case BlendMode::Color: return "svg:color";
    case BlendMode::Luminosity: return "svg:luminosity";
    default: return "svg:src-over";
    }
}

BlendMode parseComposite(const std::string& s)
{
    if (s.find("multiply") != std::string::npos) return BlendMode::Multiply;
    if (s.find("screen") != std::string::npos) return BlendMode::Screen;
    if (s.find("overlay") != std::string::npos) return BlendMode::Overlay;
    if (s.find("darken") != std::string::npos) return BlendMode::Darken;
    if (s.find("lighten") != std::string::npos) return BlendMode::Lighten;
    if (s.find("plus") != std::string::npos || s.find("add") != std::string::npos) return BlendMode::Add;
    if (s.find("color-dodge") != std::string::npos || s.find("colordodge") != std::string::npos)
        return BlendMode::ColorDodge;
    if (s.find("color-burn") != std::string::npos || s.find("colorburn") != std::string::npos)
        return BlendMode::ColorBurn;
    if (s.find("hard-light") != std::string::npos || s.find("hardlight") != std::string::npos)
        return BlendMode::HardLight;
    if (s.find("soft-light") != std::string::npos || s.find("softlight") != std::string::npos)
        return BlendMode::SoftLight;
    if (s.find("difference") != std::string::npos) return BlendMode::Difference;
    if (s.find("exclusion") != std::string::npos) return BlendMode::Exclusion;
    if (s.find("modulate") != std::string::npos) return BlendMode::Modulate;
    if (s.find("luminosity") != std::string::npos) return BlendMode::Luminosity;
    if (s.find("saturation") != std::string::npos) return BlendMode::Saturation;
    if (s.find("hue") != std::string::npos) return BlendMode::Hue;
    if (s.find("color") != std::string::npos) return BlendMode::Color;
    return BlendMode::Normal;
}

}

bool save(const std::string& path, Document& doc)
{
    g_error.clear();
    if (!doc.valid()) {
        fail("没有可保存的文档");
        return false;
    }
    doc.syncTextLayers();

    std::vector<ZipEntry> entries;

    {
        ZipEntry e;
        e.name = "mimetype";
        const char* m = "image/openraster";
        e.data.assign(m, m + std::strlen(m));
        entries.push_back(std::move(e));
    }

    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<image version=\"0.0.4\" w=\"" + std::to_string(doc.width) + "\" h=\"" + std::to_string(doc.height) +
           "\">\n";
    xml += "  <stack>\n";

    for (int i = (int)doc.layers.size() - 1; i >= 0; --i) {
        const Layer& l = doc.layers[(size_t)i];
        if (l.img.empty()) continue;

        const std::string src = "data/layer" + std::to_string(i) + ".png";
        std::vector<uint8_t> png;
        if (!imageio::encodePng(l.img, png)) {
            fail("图层 PNG 编码失败：" + l.name);
            return false;
        }
        ZipEntry e;
        e.name = src;
        e.data = std::move(png);
        entries.push_back(std::move(e));

        std::string maskSrc;
        if (!l.uMask.empty() && l.uMaskW == l.img.width() && l.uMaskH == l.img.height()) {
            Image mi(l.uMaskW, l.uMaskH);
            uint32_t* dst = mi.pixels();
            const size_t n = (size_t)l.uMaskW * (size_t)l.uMaskH;
            for (size_t k = 0; k < n; ++k) {
                const uint32_t m = l.uMask[k];
                dst[k] = 0xff000000u | (m << 16) | (m << 8) | m;
            }
            std::vector<uint8_t> mp;
            if (imageio::encodePng(mi, mp)) {
                maskSrc = "data/mask" + std::to_string(i) + ".png";
                ZipEntry me;
                me.name = maskSrc;
                me.data = std::move(mp);
                entries.push_back(std::move(me));
            }
        }

        char op[32];
        std::snprintf(op, sizeof(op), "%.4f", (double)l.opacity);
        xml += "    <layer name=\"" + xmlEscape(l.name) + "\" src=\"" + src + "\"";
        xml += " x=\"" + std::to_string(l.x) + "\" y=\"" + std::to_string(l.y) + "\"";
        xml += " visibility=\"" + std::string(l.visible ? "visible" : "hidden") + "\"";
        xml += " opacity=\"" + std::string(op) + "\"";
        xml += " composite-op=\"" + std::string(compositeOp(l.blend)) + "\"";
        if (l.lockAlpha) xml += " lock-alpha=\"1\"";
        if (!maskSrc.empty()) xml += " mask-src=\"" + maskSrc + "\"";
        xml += styleAttrs(l.style);
        xml += childAttrs(l.children);
        if (l.kind == LayerKind::Text) {
            char tb[160];
            xml += " text=\"" + xmlEscapeText(l.text.text) + "\"";
            std::snprintf(tb, sizeof(tb),
                          " text-x=\"%.2f\" text-y=\"%.2f\" text-size=\"%d\" text-lineh=\"%.2f\"",
                          (double)l.text.x, (double)l.text.y, l.text.size, (double)l.text.lineSpacing);
            xml += tb;
            std::snprintf(tb, sizeof(tb), " text-color=\"#%02X%02X%02X%02X\" text-opacity=\"%.4f\"",
                          (unsigned)l.text.color.r, (unsigned)l.text.color.g, (unsigned)l.text.color.b,
                          (unsigned)l.text.color.a, (double)l.text.opacity);
            xml += tb;
            if (!l.text.font.empty()) xml += " text-font=\"" + xmlEscape(l.text.font) + "\"";
            if (!l.textXf.isIdentity()) {
                SkScalar m9[9];
                l.textXf.get9(m9);
                char xb[320];
                std::snprintf(xb, sizeof(xb),
                              " text-xf=\"%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f\"",
                              (double)m9[0], (double)m9[1], (double)m9[2], (double)m9[3], (double)m9[4],
                              (double)m9[5], (double)m9[6], (double)m9[7], (double)m9[8]);
                xml += xb;
            }
        }
        xml += "/>\n";
    }

    xml += "  </stack>\n</image>\n";
    {
        ZipEntry e;
        e.name = "stack.xml";
        e.data.assign(xml.begin(), xml.end());
        entries.push_back(std::move(e));
    }

    {
        const Image merged = doc.flatten();
        std::vector<uint8_t> png;
        if (!imageio::encodePng(merged, png)) {
            fail("合并图 PNG 编码失败");
            return false;
        }
        ZipEntry e;
        e.name = "mergedimage.png";
        e.data = std::move(png);
        entries.push_back(std::move(e));
    }

    return writeZip(path, entries);
}

bool load(const std::string& path, Document& doc)
{
    g_error.clear();

    std::vector<uint8_t> bytes;
    if (!imageio::readBytes(path, bytes) || bytes.empty()) {
        fail("无法打开文件：" + path);
        return false;
    }
    std::vector<ZipEntry> entries;
    if (!readZip(bytes, entries)) {
        fail("不是有效的 ORA（ZIP 结构损坏）：" + path);
        return false;
    }

    const ZipEntry* stack = findEntry(entries, "stack.xml");
    if (!stack) {
        fail("不是 ORA 文件（缺少 stack.xml）：" + path);
        return false;
    }
    std::string xml(stack->data.begin(), stack->data.end());

    int w = 0, h = 0;
    if (const size_t p = xml.find("<image"); p != std::string::npos) {
        if (const size_t e = xml.find('>', p); e != std::string::npos) {
            const std::string tag = xml.substr(p, e - p);
            w = std::atoi(attr(tag, "w").c_str());
            h = std::atoi(attr(tag, "h").c_str());
        }
    }
    if (w <= 0 || h <= 0) {
        fail("ORA 缺少有效的画布尺寸");
        return false;
    }

    struct Ref {
        std::string src, name;
        int x = 0, y = 0;
        bool visible = true;
        float opacity = 1.0f;
        BlendMode blend = BlendMode::Normal;
        bool isText = false;
        TextData text;
        SkMatrix textXf;
        LayerStyle style;
        std::vector<LayerChild> kids;
        bool lockAlpha = false;
        std::string maskSrc;
    };
    std::vector<Ref> refs;
    size_t p = 0;
    while ((p = xml.find("<layer", p)) != std::string::npos) {
        const size_t e = xml.find('>', p);
        if (e == std::string::npos) break;
        const size_t after = p + 6;
        if (after < xml.size() && !std::isspace((unsigned char)xml[after])) {
            p = after;
            continue;
        }
        const std::string tag = xml.substr(p, e - p);
        Ref r;
        r.src = attr(tag, "src");
        r.name = xmlUnescape(attr(tag, "name"));
        r.x = std::atoi(attr(tag, "x").c_str());
        r.y = std::atoi(attr(tag, "y").c_str());
        r.visible = attr(tag, "visibility") != "hidden";
        const std::string op = attr(tag, "opacity");
        r.opacity = op.empty() ? 1.0f : (float)std::atof(op.c_str());
        r.blend = parseComposite(attr(tag, "composite-op"));
        r.style = parseStyleAttrs(tag);
        r.kids = parseChildAttrs(tag);
        r.lockAlpha = attr(tag, "lock-alpha") == "1";
        r.maskSrc = attr(tag, "mask-src");
        r.isText = tag.find(" text=\"") != std::string::npos;
        if (r.isText) {
            r.text.text = xmlUnescapeText(attr(tag, "text"));
            const std::string tx = attr(tag, "text-x"), ty = attr(tag, "text-y");
            r.text.x = tx.empty() ? 0.0f : (float)std::atof(tx.c_str());
            r.text.y = ty.empty() ? 0.0f : (float)std::atof(ty.c_str());
            const std::string ts = attr(tag, "text-size");
            r.text.size = ts.empty() ? 64 : std::atoi(ts.c_str());
            const std::string tl = attr(tag, "text-lineh");
            r.text.lineSpacing = tl.empty() ? 6.0f : (float)std::atof(tl.c_str());
            r.text.color = parseHexColor(attr(tag, "text-color"), Color(0, 0, 0, 255));
            const std::string to = attr(tag, "text-opacity");
            r.text.opacity = to.empty() ? 1.0f : (float)std::atof(to.c_str());
            r.text.font = xmlUnescape(attr(tag, "text-font"));
            const std::string xf = attr(tag, "text-xf");
            if (!xf.empty()) {
                double m9[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
                int got = std::sscanf(xf.c_str(), "%lf %lf %lf %lf %lf %lf %lf %lf %lf", &m9[0], &m9[1], &m9[2],
                                      &m9[3], &m9[4], &m9[5], &m9[6], &m9[7], &m9[8]);
                if (got == 9) {
                    SkScalar s9[9];
                    for (int k = 0; k < 9; ++k) s9[k] = (SkScalar)m9[k];
                    r.textXf.set9(s9);
                }
            }
        }
        if (!r.src.empty()) refs.push_back(std::move(r));
        p = e + 1;
    }

    Document tmp;
    bool baseSet = false;
    for (auto it = refs.rbegin(); it != refs.rend(); ++it) {
        const ZipEntry* e = findEntry(entries, it->src);
        if (!e) continue;
        Image img;
        if (!imageio::decode(e->data, img) || img.empty()) continue;

        const std::string name = it->name.empty() ? "图层" : it->name;
        if (!baseSet) {
            tmp.adopt(w, h, std::move(img), name);
            baseSet = true;
            if (it->isText) {
                Layer& L = tmp.layers.back();
                L.kind = LayerKind::Text;
                L.text = it->text;
                L.img = Image();
            }
        } else if (it->isText) {
            const int at = tmp.addTextLayer(it->text, name);
            if (at < 0) continue;
        } else {
            tmp.addLayerAt(std::move(img), it->x, it->y, name);
        }
        Layer& L = tmp.layers[(size_t)tmp.active];
        if (!it->isText) {
            L.x = it->x;
            L.y = it->y;
        }
        L.visible = it->visible;
        L.opacity = std::clamp(it->opacity, 0.0f, 1.0f);
        L.blend = it->blend;
        L.style = it->style;
        L.textXf = it->textXf;
        L.lockAlpha = it->lockAlpha;
        L.children = it->kids;
        L.styleCache = Image{};
        L.styleKey = StyleCompositeKey{};
        L.maskSrcKey = StyleGridKey{};
        L.maskStrokeKey = StyleStrokeKey{};
        L.maskShadowKey = StyleShadowKey{};

        if (!it->maskSrc.empty()) {
            const ZipEntry* me = findEntry(entries, it->maskSrc);
            if (me) {
                Image mi;
                if (imageio::decode(me->data, mi) && !mi.empty()) {
                    const size_t n = (size_t)mi.width() * (size_t)mi.height();
                    L.uMask.assign(n, 255u);
                    const uint32_t* sp = mi.pixels();
                    for (size_t k = 0; k < n; ++k) L.uMask[k] = (uint8_t)((sp[k] >> 16) & 0xffu);
                    L.uMaskW = mi.width();
                    L.uMaskH = mi.height();
                    ++L.uMaskRev;
                }
            }
        }
    }
    if (!baseSet) tmp.createEmpty(w, h, Color(0, 0, 0, 0));

    int maxId = 0;
    for (const auto& l : tmp.layers) {
        maxId = std::max(maxId, l.id);
        for (const auto& c : l.children) maxId = std::max(maxId, c.id);
    }
    tmp.raiseIdsAbove(maxId);

    tmp.active = (int)tmp.layers.size() - 1;
    tmp.selIds.clear();
    tmp.refreshTextLayers();
    tmp.markAllDirty();
    doc = std::move(tmp);
    return true;
}

std::string lastError() { return g_error; }

}
}
