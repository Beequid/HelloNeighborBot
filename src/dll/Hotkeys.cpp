#include "Hotkeys.h"

#include <windows.h>

bool Hotkeys::Pressed(int vk) {
    const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool was = prev_[vk];
    prev_[vk] = now;
    return now && !was;
}

bool Hotkeys::Down(int vk) const {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
