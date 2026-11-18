#include "core/Clipboard.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <vector>

namespace ie {
namespace clipboard {
namespace {

UINT trustFormat()
{
    static const UINT fmt = ::RegisterClipboardFormatA("ImageEditor.AlphaTrust");
    return fmt;
}

struct ScopedClipboard {
    bool ok = false;
    ScopedClipboard()
    {
        for (int i = 0; i < 8 && !ok; ++i) {
            ok = ::OpenClipboard(nullptr) != 0;
            if (!ok) ::Sleep(4);
        }
    }
    ~ScopedClipboard()
    {
        if (ok) ::CloseClipboard();
    }
};

HGLOBAL makeDib(const Image& img, bool v5)
{
    const int w = img.width(), h = img.height();
    const size_t rowBytes = (size_t)w * 4;
    const size_t bitsSize = rowBytes * (size_t)h;
    const size_t headerSize = v5 ? sizeof(BITMAPV5HEADER) : sizeof(BITMAPINFOHEADER);

    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, headerSize + bitsSize);
    if (!hMem) return nullptr;

    void* p = ::GlobalLock(hMem);
    if (!p) {
        ::GlobalFree(hMem);
        return nullptr;
    }
    std::memset(p, 0, headerSize);

    auto* bi = static_cast<BITMAPINFOHEADER*>(p);
    bi->biSize = (DWORD)headerSize;
    bi->biWidth = w;
    bi->biHeight = h;
    bi->biPlanes = 1;
    bi->biBitCount = 32;
    bi->biCompression = v5 ? BI_BITFIELDS : BI_RGB;
    bi->biSizeImage = (DWORD)bitsSize;

    if (v5) {
        auto* v5h = static_cast<BITMAPV5HEADER*>(p);
        v5h->bV5RedMask = 0x00FF0000u;
        v5h->bV5GreenMask = 0x0000FF00u;
        v5h->bV5BlueMask = 0x000000FFu;
        v5h->bV5AlphaMask = 0xFF000000u;
        v5h->bV5CSType = LCS_sRGB;
    }

    auto* bits = static_cast<uint8_t*>(p) + headerSize;
    const uint8_t* src = img.data();
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src + (size_t)(h - 1 - y) * (size_t)w * 4;
        uint8_t* d = bits + (size_t)y * rowBytes;
        for (int x = 0; x < w; ++x) {
            d[x * 4 + 0] = s[x * 4 + 2];
            d[x * 4 + 1] = s[x * 4 + 1];
            d[x * 4 + 2] = s[x * 4 + 0];
            d[x * 4 + 3] = s[x * 4 + 3];
        }
    }

    ::GlobalUnlock(hMem);
    return hMem;
}

}

bool hasImage()
{
    ScopedClipboard cb;
    if (!cb.ok) return false;
    return ::IsClipboardFormatAvailable(CF_DIBV5) || ::IsClipboardFormatAvailable(CF_DIB);
}

bool getImage(Image& out)
{
    ScopedClipboard cb;
    if (!cb.ok) return false;

    const bool trusted = ::IsClipboardFormatAvailable(trustFormat()) != 0;

    UINT fmt = CF_DIB;
    if (::IsClipboardFormatAvailable(CF_DIBV5)) fmt = CF_DIBV5;
    else if (!::IsClipboardFormatAvailable(CF_DIB))
        return false;

    HANDLE h = ::GetClipboardData(fmt);
    if (!h) return false;
    const void* p = ::GlobalLock(h);
    if (!p) return false;

    const auto* bi = static_cast<const BITMAPINFOHEADER*>(p);
    if (bi->biSize < sizeof(BITMAPINFOHEADER)) {
        ::GlobalUnlock(h);
        return false;
    }

    const int w = bi->biWidth;
    const int hAbs = std::abs(bi->biHeight);
    const bool topDown = bi->biHeight < 0;
    const int bpp = bi->biBitCount;
    if (w <= 0 || hAbs <= 0 || (bpp != 24 && bpp != 32)) {
        ::GlobalUnlock(h);
        return false;
    }

    const size_t headerSize = bi->biSize >= sizeof(BITMAPV5HEADER)
                                  ? bi->biSize
                                  : bi->biSize + (bi->biCompression == BI_BITFIELDS ? 12 : 0);
    const size_t rowBytes = ((size_t)w * bpp + 31) / 32 * 4;
    const auto* bits = static_cast<const uint8_t*>(p) + headerSize;

    out.reset(w, hAbs);
    uint8_t* dst = out.data();
    bool anyAlpha = false;

    for (int y = 0; y < hAbs; ++y) {
        const int srcY = topDown ? y : (hAbs - 1 - y);
        const uint8_t* row = bits + (size_t)srcY * rowBytes;
        uint8_t* d = dst + (size_t)y * (size_t)w * 4;
        if (bpp == 32) {
            for (int x = 0; x < w; ++x) {
                d[x * 4 + 0] = row[x * 4 + 2];
                d[x * 4 + 1] = row[x * 4 + 1];
                d[x * 4 + 2] = row[x * 4 + 0];
                d[x * 4 + 3] = row[x * 4 + 3];
                if (row[x * 4 + 3]) anyAlpha = true;
            }
        } else {
            for (int x = 0; x < w; ++x) {
                d[x * 4 + 0] = row[x * 3 + 2];
                d[x * 4 + 1] = row[x * 3 + 1];
                d[x * 4 + 2] = row[x * 3 + 0];
                d[x * 4 + 3] = 255;
            }
        }
    }

    if (!anyAlpha && bpp == 32 && !trusted) {
        for (size_t i = 0, n = (size_t)w * hAbs; i < n; ++i) dst[i * 4 + 3] = 255;
    }

    ::GlobalUnlock(h);
    return true;
}

bool hasText()
{
    ScopedClipboard cb;
    if (!cb.ok) return false;
    return ::IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
}

bool getText(std::string& out)
{
    ScopedClipboard cb;
    if (!cb.ok) return false;
    if (!::IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
    HANDLE h = ::GetClipboardData(CF_UNICODETEXT);
    if (!h) return false;
    const wchar_t* w = static_cast<const wchar_t*>(::GlobalLock(h));
    if (!w) return false;
    const int n = (int)std::wcslen(w);
    std::string s;
    if (n > 0) {
        s.resize((size_t)n * 4);
        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, w, n, s.data(), (int)s.size(), nullptr, nullptr);
        if (bytes <= 0) {
            ::GlobalUnlock(h);
            return false;
        }
        s.resize((size_t)bytes);
    }
    ::GlobalUnlock(h);
    out = std::move(s);
    return true;
}

void setText(const std::string& utf8)
{
    ScopedClipboard cb;
    if (!cb.ok) return;
    ::EmptyClipboard();

    const int n = utf8.empty() ? 0 : ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    const size_t bytes = ((size_t)n + 1) * sizeof(wchar_t);
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return;
    if (wchar_t* dst = static_cast<wchar_t*>(::GlobalLock(mem))) {
        if (n > 0) ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), dst, n);
        dst[n] = 0;
        ::GlobalUnlock(mem);
        if (::SetClipboardData(CF_UNICODETEXT, mem)) return;
    }
    ::GlobalFree(mem);
}

void setImage(const Image& img)
{
    if (img.empty()) return;
    ScopedClipboard cb;
    if (!cb.ok) return;

    ::EmptyClipboard();

    if (HGLOBAL mark = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD))) {
        if (void* mp = ::GlobalLock(mark)) {
            *static_cast<DWORD*>(mp) = 0x49454131u;
            ::GlobalUnlock(mark);
        }
        if (!::SetClipboardData(trustFormat(), mark)) ::GlobalFree(mark);
    }

    if (HGLOBAL v5 = makeDib(img, true)) {
        if (!::SetClipboardData(CF_DIBV5, v5)) ::GlobalFree(v5);
    }
    if (HGLOBAL dib = makeDib(img, false)) {
        if (!::SetClipboardData(CF_DIB, dib)) ::GlobalFree(dib);
    }
}

}
}
