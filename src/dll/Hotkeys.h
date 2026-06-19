#pragma once
#include <unordered_map>

// Edge-detecting hotkey poller built on GetAsyncKeyState. Call the methods
// once per main-loop tick.
class Hotkeys {
public:
    // True exactly once per physical press (down now, up on the previous poll).
    bool Pressed(int vk);
    // True while the key is currently held.
    bool Down(int vk) const;

private:
    std::unordered_map<int, bool> prev_;
};
