#pragma once

#include "core/Types.h"

namespace ie {

struct PointerEvent {
    Vec2f pos{0, 0};
    int button = 0;
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool doubleClick = false;
    bool ctrl = false, shift = false, alt = false;
};

struct WheelEvent {
    Vec2f pos{0, 0};
    float dy = 0.0f;
    bool ctrl = false, shift = false, alt = false;
};

struct KeyEvent {
    int scancode = 0;
    int unicode = 0;
    bool pressed = true;
    bool repeat = false;
    bool ctrl = false, shift = false, alt = false;
};

enum KeyCode {
    KeyEsc = 59,
    KeyBackspace = 63,
    KeyTab = 64,
    KeyEnter = 67,
    KeyDelete = 77,
    KeyHome = 78,
    KeyEnd = 79,
    KeyLeft = 82,
    KeyRight = 83,
    KeyUp = 84,
    KeyDown = 85,
    KeySpace = 75,
    KeyA = 1,
    KeyB = 2,
    KeyC = 3,
    KeyD = 4,
    KeyE = 5,
    KeyF = 6,
    KeyG = 7,
    KeyI = 9,
    KeyJ = 10,
    KeyM = 13,
    KeyN = 14,
    KeyO = 15,
    KeyP = 16,
    KeyR = 18,
    KeyS = 19,
    KeyT = 20,
    KeyU = 21,
    KeyV = 22,
    KeyW = 23,
    KeyX = 24,
    KeyY = 25,
    KeyZ = 26,
    Key0 = 27,
    Key1 = 28,
    KeyEqual = 62,
    KeyMinus = 61,
};

}
