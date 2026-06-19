#pragma once
// D3D11 Present-hook based ImGui overlay. Init() is non-fatal: if hooking
// fails the bot still runs headless (console + hotkeys), so callers should
// treat a false return as "no overlay", not a hard error.
class Bot;

namespace overlay {

bool Init(Bot* bot);
void Shutdown();
bool IsInitialized();

} // namespace overlay
