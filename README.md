# HelloNeighorBot

An injectable internal C++ speedrun bot for *Hello Neighbor* (Old Patch 1.1.6), a 64-bit Unreal Engine 4 shipping build. The bot is a DLL injected into `HelloNeighbor-Win64-Shipping.exe`; once inside, it reads and writes the player's transform through configurable pointer chains and drives the player along a saved waypoint route — a TAS-style positional mover with an in-game ImGui control and route-authoring menu.

## What it is / what it is NOT

**What it is**

- An **internal C++ DLL** injected into the **single-player** game *Hello Neighbor 1.1.6*.
- A **TAS-style positional mover**: it teleports or walks the player between waypoints you record.
- A **route authoring tool**: capture coordinates live, edit waypoints in-game, and save/load routes as JSON.
- **Fully config-driven**: you supply the `GWorld` resolution method and the pointer chains for your build; nothing about the game's memory layout is hard-coded.

**What it is NOT**

- **Not a magic auto-win.** It does not understand objectives, solve puzzles, or react to the game world. It only moves the player along the route *you* author.
- **Not a computer-vision or auto-objective-detection tool.** There is no image recognition and no automatic objective detection — it works purely from memory reads/writes and your waypoints.
- **Not for multiplayer or anti-cheat-protected games.** It targets a single-player game for personal speedrunning. It does no networking and no anti-cheat evasion, and touches no process other than the injector's target.

## Features

- **Config-driven pointer chains** — read/write the player's location (and optionally velocity and rotation) via Cheat Engine-style pointer chains defined in `config/config.json`. Empty chains are simply skipped.
- **`GWorld` resolution by RVA or AOB** — locate the `GWorld` global either by a fixed module RVA (`mode: "rva"`) or by an array-of-bytes pattern scan with optional RIP-relative displacement resolution (`mode: "pattern"`).
- **Waypoint route engine** — a state machine that drives the player through waypoints in **teleport** or **walk** mode, each with its own arrival tolerance and post-arrival wait, with optional route looping.
- **In-game ImGui menu** — a D3D11 `Present`-hooked overlay (MinHook, kiero-style vtable hook) showing SDK state, controls, the waypoint editor, the timer, and a scrolling log pane.
- **Live coordinates + waypoint capture** — read the player's current location in real time and capture it as a new waypoint with one hotkey or button.
- **Speedrun timer + splits** — a run timer with start/stop/pause/reset and a split recorded per completed waypoint.
- **Configurable hotkeys** — edge-detected global hotkeys for every action (see the table below).
- **Standalone injector** — `injector.exe` loads the DLL into the game via `CreateRemoteThread(LoadLibraryW)`.

## Requirements

- **Windows x64.** The game is 64-bit; a 32-bit DLL cannot be injected into it.
- **Visual Studio 2022** with the **"Desktop development with C++"** workload and a **Windows 10/11 SDK**.
- **CMake 3.20+**.
- A copy of **Hello Neighbor 1.1.6** (the Old Patch UE4 x64 build).

Build dependencies (MinHook 1.3.3, nlohmann/json 3.11.3, Dear ImGui 1.90.9) are fetched automatically by CMake via `FetchContent` — no manual setup required.

## Quick start

1. **Build** the DLL and injector — see [docs/BUILDING.md](docs/BUILDING.md).
2. **Find your offsets** — locate `GWorld` and the player location pointer chain for your build with Cheat Engine / Dumper-7 — see [docs/FINDING_OFFSETS.md](docs/FINDING_OFFSETS.md).
3. **Configure** — copy `config/config.example.json` to `config/config.json` and fill in your real values.
4. **Inject** — run `injector.exe` with the game running, or `injector.exe [process_name] [dll_path]`. See [docs/USAGE.md](docs/USAGE.md).
5. **Capture a route** — open the menu (default `INSERT`), move in-game, and capture waypoints (default `F8`); edit and save the route. The route file format is documented in [docs/ROUTE_FORMAT.md](docs/ROUTE_FORMAT.md).
6. **Run** — start the route (default `F5`) and watch the bot drive the player along it.

> The example offsets/patterns in `config/config.example.json` are **placeholders**. The bot degrades gracefully when chains are unset or invalid — it logs and keeps running rather than crashing the game.

## Default hotkeys

All hotkeys are configurable under `settings.hotkeys` in `config/config.json`.

| Action | Default key | Config key |
| --- | --- | --- |
| Toggle menu | `INSERT` | `toggle_menu` |
| Start route | `F5` | `start` |
| Stop route | `F6` | `stop` |
| Pause / resume | `F7` | `pause` |
| Capture waypoint at current location | `F8` | `capture_waypoint` |
| Teleport to current waypoint | `F9` | `teleport_next` |
| Reload config | `F10` | `reload_config` |
| Unload DLL | `END` | `unload` |

## Project layout

```
HelloNeighorBot/
  CMakeLists.txt                 CMake build (x64, C++17); fetches MinHook, nlohmann/json, ImGui.
  DESIGN.md                      Authoritative design contract.
  config/
    config.example.json          Example config (placeholder offsets/chains/settings/hotkeys).
    routes/
      act1.example.json          Example waypoint route.
  src/
    dll/
      dllmain.cpp                DllMain -> spawns the bot thread.
      Bot.{h,cpp}               Orchestrator: config, subsystems, main loop, hotkeys, unload.
      Config.{h,cpp}            config.json parsing; VK-name parsing; config file search.
      Logger.{h,cpp}            Console + file logging; Tail() for the overlay log pane.
      Hotkeys.{h,cpp}           Edge-detecting GetAsyncKeyState poller.
      mem/Pattern.{h,cpp}       Module info, AOB scan, RIP-relative resolve, SEH read/write, pointer chains.
      ue4/UE4.h                 FVector/FRotator math (header-only).
      ue4/SdkAccess.{h,cpp}     Resolve GWorld; get/set player location/velocity/rotation.
      route/RouteEngine.{h,cpp} Waypoint state machine; load/save JSON routes; timer + splits.
      ui/Overlay.{h,cpp}        D3D11 Present hook + ImGui init (MinHook, WndProc hook).
      ui/Menu.{h,cpp}           ImGui menu: live coords, controls, waypoint editor, timer.
    injector/
      injector.cpp              Standalone exe: LoadLibrary injection via CreateRemoteThread.
  docs/
    BUILDING.md                 How to build.
    FINDING_OFFSETS.md          How to find GWorld and the player pointer chains.
    USAGE.md                    How to inject and use the bot.
    ROUTE_FORMAT.md             Route JSON format reference.
```

## Disclaimer

HelloNeighorBot is provided for **educational purposes and personal speedrunning** of a single-player game. It works by **reading and modifying the game's process memory at runtime** — you do this **at your own risk**, and it may crash the game or break with other builds/patches.

This tool targets a **single-player game only**. Do not use it with online, multiplayer, or anti-cheat-protected games. It performs no networking and no anti-cheat evasion. See [LICENSE](LICENSE) for licensing terms.
