#include "core/ImageIo.h"

#include <vips/vips.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ie {
namespace imageio {
namespace {

std::string g_error;
bool g_inited = false;

std::string lowerExt(const std::string& path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot + 1);
    for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

void fail(const std::string& msg)
{
    g_error = msg;
}

void failVips(const std::string& what)
{
    const char* buf = vips_error_buffer();
    g_error = what + (buf && *buf ? ("：" + std::string(buf)) : std::string());
    vips_error_clear();
}

#ifdef _WIN32
std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
#endif

struct Chain {
    VipsImage* img = nullptr;
    ~Chain()
    {
        if (img) g_object_unref(img);
    }
    void reset(VipsImage* next)
    {
        if (img) g_object_unref(img);
        img = next;
    }
};

bool toRgba8(VipsImage* in, Image& out)
{
    Chain a, b, c, d, icc;

    if (vips_autorot(in, &a.img, nullptr)) {
        failVips("读取图像方向失败");
        return false;
    }

    const void* iccBlob = nullptr;
    size_t iccLen = 0;
    VipsImage* src = a.img;
    if (vips_image_get_bands(a.img) >= 3 &&
        vips_image_get_blob(a.img, VIPS_META_ICC_NAME, &iccBlob, &iccLen) == 0 && iccLen >= 128) {
        VipsImage* xyz = nullptr;
        if (vips_icc_import(src, &xyz, "embedded", TRUE, "pcs", VIPS_PCS_XYZ, nullptr) == 0) {
            icc.img = xyz;
            src = xyz;
        }
    }

    const int bands = vips_image_get_bands(src);
    if (bands == 1 || bands == 2) {
        VipsImage* grey = nullptr;
        if (vips_extract_band(src, &grey, 0, nullptr)) {
            failVips("拆分灰度通道失败");
            return false;
        }
        b.img = grey;
        VipsImage* rgb = nullptr;
        if (vips_colourspace(b.img, &rgb, VIPS_INTERPRETATION_sRGB, nullptr)) {
            failVips("灰度转 sRGB 失败");
            return false;
        }
        c.reset(rgb);
        if (bands == 2) {
            VipsImage* alpha = nullptr;
            if (vips_extract_band(src, &alpha, 1, nullptr)) {
                failVips("拆分 alpha 通道失败");
                return false;
            }
            d.img = alpha;
            VipsImage* joined = nullptr;
            if (vips_bandjoin2(c.img, d.img, &joined, nullptr)) {
                failVips("合成灰度+alpha 失败");
                return false;
            }
            c.reset(joined);
        }
    } else {
        VipsImage* rgb = nullptr;
        if (vips_colourspace(src, &rgb, VIPS_INTERPRETATION_sRGB, nullptr)) {
            failVips("转 sRGB 失败");
            return false;
        }
        c.reset(rgb);
    }

    if (vips_cast(c.img, &d.img, VIPS_FORMAT_UCHAR, nullptr)) {
        failVips("位深转 8bit 失败");
        return false;
    }
    c.reset(d.img);
    d.img = nullptr;

    if (vips_image_get_bands(c.img) == 3) {
        VipsImage* rgba = nullptr;
        if (vips_bandjoin_const1(c.img, &rgba, 255.0, nullptr)) {
            failVips("补 alpha 通道失败");
            return false;
        }
        c.reset(rgba);
    } else if (vips_image_get_bands(c.img) < 3) {
        fail("不支持的通道数");
        return false;
    } else if (vips_image_get_bands(c.img) > 4) {
        VipsImage* rgba = nullptr;
        if (vips_extract_band(c.img, &rgba, 0, "n", 4, nullptr)) {
            failVips("多余通道裁剪失败");
            return false;
        }
        c.reset(rgba);
    }

    size_t sz = 0;
    void* mem = vips_image_write_to_memory(c.img, &sz);
    if (!mem) {
        failVips("解码到内存失败");
        return false;
    }

    const int w = vips_image_get_width(c.img);
    const int h = vips_image_get_height(c.img);
    out.reset(w, h);
    std::memcpy(out.data(), mem, (size_t)w * (size_t)h * 4);
    g_free(mem);
    return true;
}

VipsImage* fromRgba(const Image& img, int bands, const std::vector<uint8_t>& pixels)
{
    return vips_image_new_from_memory_copy(pixels.data(), (size_t)img.width() * img.height() * 4,
                                           img.width(), img.height(), bands,
                                           VIPS_FORMAT_UCHAR);
}

}

bool readBytes(const std::string& path, std::vector<uint8_t>& bytes)
{
#ifdef _WIN32
    FILE* f = _wfopen(utf8ToWide(path).c_str(), L"rb");
#else
    FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len < 0) {
        std::fclose(f);
        return false;
    }
    bytes.resize((size_t)len);
    const size_t got = std::fread(bytes.data(), 1, (size_t)len, f);
    std::fclose(f);
    return got == (size_t)len;
}

bool writeBytes(const std::string& path, const std::vector<uint8_t>& bytes)
{
#ifdef _WIN32
    FILE* f = _wfopen(utf8ToWide(path).c_str(), L"wb");
#else
    FILE* f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) return false;
    const size_t wrote = std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    return wrote == bytes.size();
}

bool exists(const std::string& path)
{
#ifdef _WIN32
    const DWORD a = ::GetFileAttributesW(utf8ToWide(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
#endif
}

bool init()
{
    if (g_inited) return true;
    if (vips_init("ImageEditor")) {
        failVips("vips 初始化失败");
        return false;
    }
    g_inited = true;
    return true;
}

std::string lastError() { return g_error; }

bool decode(const std::vector<uint8_t>& bytes, Image& out)
{
    g_error.clear();
    if (bytes.empty()) {
        fail("空图像数据");
        return false;
    }
    if (!init()) return false;

    VipsImage* in = vips_image_new_from_buffer(bytes.data(), bytes.size(), "", nullptr);
    if (!in) {
        failVips("无法解码图像数据");
        return false;
    }
    const bool ok = toRgba8(in, out);
    g_object_unref(in);
    return ok;
}

bool load(const std::string& path, Image& out)
{
    g_error.clear();
    std::vector<uint8_t> bytes;
    if (!readBytes(path, bytes) || bytes.empty()) {
        fail("无法打开文件：" + path);
        return false;
    }
    if (!decode(bytes, out)) {
        g_error = "无法解码文件：" + path;
        return false;
    }
    return true;
}

bool encodePng(const Image& img, std::vector<uint8_t>& out, int compression)
{
    g_error.clear();
    out.clear();
    if (img.empty()) {
        fail("没有可编码的像素");
        return false;
    }
    if (!init()) return false;

    const std::vector<uint8_t> pixels(img.data(), img.data() + (size_t)img.width() * img.height() * 4);
    VipsImage* v = fromRgba(img, 4, pixels);
    if (!v) {
        failVips("构造 PNG 编码输入失败");
        return false;
    }

    void* buf = nullptr;
    size_t len = 0;
    const int rc = vips_pngsave_buffer(v, &buf, &len,
                                       "compression", std::clamp(compression, 0, 9),
                                       "strip", TRUE,
                                       nullptr);
    g_object_unref(v);
    if (rc || !buf) {
        failVips("PNG 编码失败");
        if (buf) g_free(buf);
        return false;
    }
    const uint8_t* p = (const uint8_t*)buf;
    out.assign(p, p + len);
    g_free(buf);
    return true;
}

bool save(const std::string& path, const Image& img, const SaveOptions& opt)
{
    g_error.clear();
    if (img.empty()) {
        fail("没有可保存的图像");
        return false;
    }
    if (!init()) return false;

    const std::string ext = lowerExt(path);
    if (ext != "png" && ext != "jpg" && ext != "jpeg" && ext != "webp") {
        fail(ext.empty() ? ("缺少文件扩展名：" + path) : ("不支持的保存格式：." + ext));
        return false;
    }

    const int w = img.width(), h = img.height();
    const size_t n = (size_t)w * (size_t)h;
    const uint8_t* src = img.data();

    std::vector<uint8_t> rgb;
    if (ext == "jpg" || ext == "jpeg") {
        rgb.resize(n * 3);
        for (size_t i = 0; i < n; ++i) {
            const float a = src[i * 4 + 3] / 255.0f;
            for (int k = 0; k < 3; ++k)
                rgb[i * 3 + k] = (uint8_t)(src[i * 4 + k] * a + 255.0f * (1.0f - a) + 0.5f);
        }
    } else {
        rgb.assign(src, src + n * 4);
    }

    VipsImage* v = vips_image_new_from_memory_copy(rgb.data(), rgb.size(), w, h,
                                                  (ext == "png" || ext == "webp") ? 4 : 3,
                                                  VIPS_FORMAT_UCHAR);
    if (!v) {
        failVips("构造编码输入失败");
        return false;
    }

    std::vector<uint8_t> bytes;
    void* buf = nullptr;
    size_t len = 0;
    int rc = -1;
    const int q = std::clamp(opt.quality, 1, 100);

    if (ext == "jpg" || ext == "jpeg") {
        rc = vips_jpegsave_buffer(v, &buf, &len,
                                  "Q", q,
                                  "optimize_coding", TRUE,
                                  "strip", TRUE,
                                  nullptr);
    } else if (ext == "png") {
        rc = vips_pngsave_buffer(v, &buf, &len,
                                 "compression", std::clamp(opt.compression, 0, 9),
                                 "strip", TRUE,
                                 nullptr);
    } else {
        rc = vips_webpsave_buffer(v, &buf, &len,
                                  "Q", q,
                                  "lossless", opt.lossless ? TRUE : FALSE,
                                  "effort", std::clamp(opt.effort, 0, 6),
                                  "strip", TRUE,
                                  nullptr);
    }
    if (rc || !buf) {
        failVips("编码失败：" + path);
        if (buf) g_free(buf);
        g_object_unref(v);
        return false;
    }
    bytes.assign((uint8_t*)buf, (uint8_t*)buf + len);
    g_free(buf);
    g_object_unref(v);

    if (!writeBytes(path, bytes)) {
        fail("写入文件失败：" + path);
        return false;
    }
    return true;
}

}
}
