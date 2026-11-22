#pragma once

#include <cctype>
#include <functional>
#include <string>

namespace ie {

inline std::string saveExtFor(int type)
{
    switch (type) {
        case 1: return ".jpg";
        case 2: return ".webp";
        case 3: return ".ora";
        default: return ".png";
    }
}

// 只认真正的图片扩展名：名字里的点不等于扩展名。
// "v1.2"、"img.webp-1" 这类末段不是扩展名，必须照常补后缀；
// 否则会得到无扩展名的路径，保存直接失败、标签一直挂着"未保存"。
inline bool isSaveExtSuffix(const std::string& tail)
{
    std::string e = tail;
    for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    return e == "png" || e == "jpg" || e == "jpeg" || e == "webp" || e == "ora";
}

inline bool hasSaveExt(const std::string& base)
{
    const size_t dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= base.size()) return false;
    return isSaveExtSuffix(base.substr(dot + 1));
}

inline std::string composeSavePath(const std::string& dir, const std::string& name, const std::string& ext)
{
    std::string d = dir.empty() ? std::string(".") : dir;
    if (d.back() != '\\' && d.back() != '/') d.push_back('\\');

    std::string full = d + name;

    const size_t sep = full.find_last_of("\\/");
    const std::string base = (sep == std::string::npos) ? full : full.substr(sep + 1);
    if (!hasSaveExt(base)) full += ext;

    return full;
}

inline std::string uniquePathName(const std::string& dir, const std::string& name,
                                  const std::string& ext,
                                  const std::function<bool(const std::string&)>& exists)
{
    const std::string first = composeSavePath(dir, name, ext);
    if (!exists(first)) return first;

    std::string base = name;
    const size_t sep = base.find_last_of("\\/");
    const size_t start = (sep == std::string::npos) ? 0 : sep + 1;
    if (hasSaveExt(base.substr(start))) base = base.substr(0, base.find_last_of('.'));
    const size_t dash = base.find_last_of('-');
    if (dash != std::string::npos) {
        bool tailDigits = dash + 1 < base.size();
        for (size_t i = dash + 1; i < base.size(); ++i)
            if (base[i] < '0' || base[i] > '9') { tailDigits = false; break; }
        if (tailDigits) base = base.substr(0, dash);
    }

    for (int n = 1;; ++n) {
        const std::string cand = composeSavePath(dir, base + "-" + std::to_string(n), ext);
        if (!exists(cand)) return cand;
    }
}

inline std::string dirNameOf(const std::string& path)
{
    const size_t sep = path.find_last_of("\\/");
    if (sep == std::string::npos) return std::string(".");
    return path.substr(0, sep);
}

}
