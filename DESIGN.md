# HelloNeighorBot — Design Contract

An **injectable internal speedrun bot** for *Hello Neighbor* (Old Patch 1.1.6),
an Unreal Engine 4 (x64 shipping) build. The bot is a DLL injected into
`HelloNeighbor-Win64-Shipping.exe`. Once inside it reads/writes the player's
transform through configurable pointer chains and drives the player along a
saved waypoint route as fast as possible — a TAS-style positional mover with an
in-game ImGui control/authoring menu.

This file is the authoritative contract. Every `.cpp` implements the matching
`.h` **exactly** as declared — do not change public signatures. Read the
relevant headers in `src/dll/` before implementing.

## Why internal C++ (not external / not C#)

The content is locked in a 5 GB `.pak`; there is no editor or mod API. Direct
memory access from inside the process is the only reliable way to read the
player's position and teleport it. C++ is the standard for UE4 internals
(native UObjects, D3D11 Present hooking, MinHook); a managed C# DLL would need a
hosted CLR and painful native interop for no benefit.

## Reality of offsets

Exact memory offsets are build-specific and are **not** shipped. The bot is
fully config-driven: the user finds `GWorld` and the pointer chains to the
player's location FVector with Cheat Engine / Dumper-7 (see
`docs/FINDING_OFFSETS.md`) and puts them in `config/config.json`. The example
values are placeholders. The code must degrade gracefully when chains are
unset/invalid (log, keep running) rather than crash.

## Layout & responsibilities

```
src/dll/
  dllmain.cpp            DllMain -> spawn bot thread; defines g_bot.
  Bot.{h,cpp}            Orchestrator: load config, init subsystems, main loop, hotkeys, unload.
  Config.{h,cpp}         Parse config.json (nlohmann/json); VK-name parsing; config file search.
  Logger.{h,cpp}         Console + file logging (printf-style, thread-safe), Tail() for overlay.
  Hotkeys.{h,cpp}        Edge-detecting GetAsyncKeyState poller.
  mem/Pattern.{h,cpp}    Module info, AOB scan, RIP-relative resolve, SEH read/write, ptr chains.
  ue4/UE4.h              FVector/FRotator math (header-only).
  ue4/SdkAccess.{h,cpp}  Resolve GWorld; get/set player location/velocity/rotation via chains.
  route/RouteEngine.{h,cpp}  Waypoint state machine; load/save JSON routes; timer + splits.
  ui/Overlay.{h,cpp}     D3D11 Present hook + ImGui init (MinHook, kiero-style vtable hook), WndProc hook.
  ui/Menu.{h,cpp}        ImGui menu content: live coords, controls, waypoint editor, timer.
src/injector/injector.cpp   Standalone exe: LoadLibrary injection via CreateRemoteThread.
```

## Control flow

1. `DllMain(DLL_PROCESS_ATTACH)`: `DisableThreadLibraryCalls`, `CreateThread(Bot::Run)`.
2. `Bot::Run(self)`:
   - `logger::Init(config.settings.enable_console)` (console may not exist yet → init after config load; bootstrap a console first so early errors are visible, that's fine).
   - Find + load config (`Config::FindConfigFile`, `Config::Load`). Keep defaults on failure.
   - `sdk_.Init(config_)` — resolve GWorld. Log success/failure; continue either way.
   - `route_.SetDefaults(...)`; `route_.LoadRoute(config_.route_path)` if set.
   - If `settings.enable_overlay`: `overlay::Init(this)` (non-fatal).
   - `mainLoop()`: each iteration → `handleHotkeys()`; if SDK ready, `route_.Tick(sdk_, dtMs)`; sleep `tick_interval_ms`; break when `unloadRequested_`.
   - `shutdown()`: `overlay::Shutdown()`, `logger::Shutdown()`, then `FreeLibraryAndExitThread(self_, 0)`.
3. Hotkeys (defaults; all configurable): INSERT toggle menu, F5 start, F6 stop, F7 pause, F8 capture waypoint at current location, F9 teleport to current waypoint, F10 reload config, END unload DLL.

## Pointer-chain semantics (critical — implement exactly)

`mem::ReadChain(base, offsets)`: `cur = base`; for each offset `i`: `cur += offsets[i]`;
if it is **not** the last offset, `cur = *(uintptr_t*)cur` (SEH-guarded; return 0 on fault).
Return the final `cur` (an **address**, not dereferenced).

`base` is the **GWorld variable address** = `moduleBase + resolvedGWorldRVA`. So a
typical `location_chain` starts with `0x0` (deref GWorld → UWorld*) and ends with
the field offset of the location FVector (added, not dereferenced).
`SdkAccess::GetLocation` reads an `FVector` (12 bytes) at that address;
`SetLocation` writes it. Velocity/rotation use their own chains; if a chain is
empty those getters/setters return false.

## RouteEngine behaviour

- `Tick`: if not `Running` or no SDK location, return. Hold while `now < waitUntil_`.
  For the current waypoint:
  - `Teleport`: `SetLocation(wp.pos)` once, set `waitUntil_ = now + wp.wait_ms`, then advance.
  - `Walk`: read current location; `step = min(moveSpeed * dtMs/1000, teleportStep)`;
    move toward `wp.pos` by `step`; `SetLocation(new)`. When `distance <= tolerance`,
    set `waitUntil_`, then advance.
  - On advance: if `wp.action == "split"` (or always, your choice — record a split per
    completed waypoint), `recordSplit()`. At end: if `loop_` reset to index 0 else `Finished`.
- Timer: `GetTickCount64()` ms; `ElapsedMs` = now - start - paused. `Pause` toggles
  Running/Paused and accumulates paused time. `Reset` clears everything.
- `CaptureWaypoint`: append `{auto-name "wp<N>", pos, mode, tolerance}` to `waypoints_`.
- Default mode/tolerance for new/loaded waypoints come from `SetDefaults`.

## JSON: config.json

See `config/config.example.json`. Keys: `process_name`, `module_name`,
`resolve.gworld` (`ResolveSpec`), `player.{location,velocity,rotation}_chain`
(arrays of hex strings like `"0x180"` → parse with base 16/0x-aware),
`settings.*`, `route_path`. Hex strings AND numbers must both parse. Unknown
keys ignored; missing keys keep struct defaults. `_comment` keys are ignored.

## JSON: route file

See `config/routes/act1.example.json`. `{ name, loop, waypoints: [{name,x,y,z,
mode("teleport"|"walk"), tolerance, wait_ms, action?}] }`. `SaveRoute` writes the
same shape (pretty-printed). Ignore `_comment`.

## Overlay / Menu

- `overlay::Init(Bot*)`: create a dummy swapchain to get the `IDXGISwapChain::Present`
  vtable address (kiero-style), hook it with MinHook. In the hook: lazily init
  ImGui (`ImGui_ImplWin32_Init` on the swapchain's `OutputWindow`, `ImGui_ImplDX11_Init`
  with the device/context from the swapchain), subclass the window's WndProc to feed
  ImGui input (and swallow input when the menu is visible). Each frame: new frame →
  `menu::Render(*g_bot)` → render draw data. Toggle visibility with `Bot::MenuVisible()`.
  Must be safe if D3D objects aren't ready yet (bail that frame).
- `menu::Render`: window titled "HelloNeighorBot". Show: SDK ready state + live
  location (`sdk.GetLocation`), route name, state, current index / count, run timer,
  splits. Buttons: Start/Stop/Pause/Reset, Capture Waypoint, Teleport→Current,
  Save Route, Reload Config. An editable list of waypoints (name, xyz, mode combo,
  tolerance, delete button, "Go" button). A scrolling log pane from `logger::Tail`.
  Use only stable ImGui (v1.90.x) API.

## Injector

`injector.exe [process_name] [dll_path]` (defaults: the game exe name, and
`HelloNeighorBot.dll` next to the injector). Find PID by name (Toolhelp snapshot),
`OpenProcess`, `VirtualAllocEx` + `WriteProcessMemory` the DLL path,
`CreateRemoteThread(LoadLibraryW)`, wait, free remote memory, report clearly.

## Build

CMake, x64, C++17. FetchContent pulls MinHook (v1.3.3), nlohmann/json (v3.11.3),
Dear ImGui (v1.90.9). `target_include_directories` for the DLL is `src/dll`, so
**all quoted includes are root-relative** (`"ue4/UE4.h"`, `"Config.h"`,
`"mem/Pattern.h"`, ...). Define `WIN32_LEAN_AND_MEAN`, `NOMINMAX`.

## Conventions

- 4-space indent, `#pragma once`. No exceptions across the DLL boundary.
- Never let a faulting read crash the game: route everything through
  `mem::SafeRead/SafeWrite` (or `mem::Read<T>/Write<T>`).
- This targets a single-player game for personal speedrunning. No networking,
  no anti-cheat evasion, no other processes touched besides the injector's target.
