#pragma once

#include <string>

namespace ie {
namespace utf8 {

inline int prev(const std::string& s, int i)
{
    if (i <= 0) return 0;
    if (i > (int)s.size()) i = (int)s.size();
    int j = i - 1;
    int back = 0;
    while (j > 0 && (s[(size_t)j] & 0xC0) == 0x80 && back < 3) {
        --j;
        ++back;
    }
    return j;
}

inline int next(const std::string& s, int i)
{
    const int n = (int)s.size();
    if (i >= n) return n;
    if (i < 0) i = 0;
    int j = i + 1;
    while (j < n && (s[(size_t)j] & 0xC0) == 0x80) ++j;
    return j;
}

inline int snap(const std::string& s, int v)
{
    const int n = (int)s.size();
    if (v <= 0) return 0;
    if (v >= n) return n;
    int j = v;
    while (j < n && (s[(size_t)j] & 0xC0) == 0x80) ++j;
    if (j < n || (j == n && v > 0)) return j;
    return n;
}

inline std::string encode(int cp)
{
    std::string out;
    if (cp < 0) return out;
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return out;
}

}
}
