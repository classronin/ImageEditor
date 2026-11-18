#pragma once

#include "core/Keymap.h"
#include "host/Shortcuts.h"

namespace ie {

inline KeyAct resolveGlobalKey(const KeyEvent& e)
{
    return shortcuts::resolve(e.scancode, keymap::modsOf(e));
}

}
