#pragma once

#include "host/Input.h"

namespace ie {
namespace keymap {

enum : unsigned {
    None = 0u,
    Ctrl = 1u << 0,
    Shift = 1u << 1,
    Alt = 1u << 2,
};

inline unsigned modsOf(const KeyEvent& e)
{
    return (e.ctrl ? Ctrl : 0u) | (e.shift ? Shift : 0u) | (e.alt ? Alt : 0u);
}

template <class Row, size_t N>
inline auto resolve(const Row (&table)[N], int key, unsigned mods) -> decltype(table[0].cmd)
{
    for (size_t i = 0; i < N; ++i)
        if (table[i].key == key && table[i].mods == mods) return table[i].cmd;
    return (decltype(table[0].cmd))0;
}

}
}
