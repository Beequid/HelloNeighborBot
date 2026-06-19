# HelloNeighorBot — Usage

HelloNeighorBot is an injectable internal speedrun bot for *Hello Neighbor*
(Old Patch 1.1.6), an Unreal Engine 4 x64 shipping build. It is a DLL injected
into `HelloNeighbor-Win64-Shipping.exe` that reads and writes the player's
transform through configurable pointer chains and drives the player along a
saved waypoint route. It provides an in-game ImGui menu, a console, and a set of
global hotkeys.

This guide covers running the bot end to end. For background see
[DESIGN.md](../DESIGN.md).

---

## How to run

### 1. Build

Build the DLL and the injector first. See [BUILDING.md](../BUILDING.md).

The build produces `HelloNeighorBot.dll` and `injector.exe`.

### 2. Configure offsets

The bot ships **no** real memory offsets — they are build-specific. You must
find `GWorld` and the pointer chain to the player's location `FVector` yourself
(with Cheat Engine / Dumper-7) and put them in `config.json`. See
[FINDING_OFFSETS.md](FINDING_OFFSETS.md).

1. Copy the example config to a real one:

   ```
   copy config\config.example.json config\config.json
   ```

2. Fill in the real values (everything in `config.example.json` is a
   placeholder). The keys are:
   - `process_name`, `module_name` — defaults are
     `HelloNeighbor-Win64-Shipping.exe`.
   - `resolve.gworld` — how to locate `GWorld`. `mode: "rva"` uses
     `module_base + rva`; `mode: "pattern"` AOB-scans the module, and with
     `rip_relative: true` resolves the displacement at `match + pattern_offset`
     as a RIP-relative address using `instruction_length`.
   - `player.location_chain` / `velocity_chain` / `rotation_chain` — Cheat
     Engine pointer chains, given as arrays of hex strings (e.g. `"0x180"`) or
     numbers. They are applied **from the GWorld variable address**
     (`module_base + resolved gworld`). The first offset is usually `0x0`
     (deref GWorld to get the `UWorld*`); the **last** offset is the field
     offset of the value and is **not** dereferenced. An empty chain (`[]`)
     disables that getter/setter.
   - `settings.*` — see [Settings reference](#settings-reference) below.
   - `route_path` — path to the route file to auto-load on startup.

   `_comment` keys and unknown keys are ignored; missing keys keep their
   defaults.

3. Place `config.json` where the DLL can find it. The DLL searches, in order:
   1. Next to the DLL.
   2. In the game directory.
   3. At the path in the `%HNBOT_CONFIG%` environment variable.

   If no config is found, the bot logs the failure and keeps running with
   built-in defaults.

### 3. Launch the game and get in-game

Start *Hello Neighbor* and load into actual gameplay (so the player and world
exist in memory) before injecting.

### 4. Inject the DLL

Use the bundled injector:

```
injector.exe
```

With no arguments it targets `HelloNeighbor-Win64-Shipping.exe` and loads
`HelloNeighorBot.dll` from next to the injector. You can override both:

```
injector.exe HelloNeighbor-Win64-Shipping.exe path\to\HelloNeighorBot.dll
```

The injector finds the process by name, then performs a standard
`LoadLibrary` injection (`VirtualAllocEx` + `WriteProcessMemory` +
`CreateRemoteThread`) and reports the result.

Any other DLL injector that does a `LoadLibrary` injection works too.

### 5. Open the menu

On injection a console window opens (if `enable_console` is on). Press
**INSERT** to toggle the in-game ImGui menu.

### 6. Author a route

1. Get to the start of the run.
2. Walk your speedrun path. At each point you want as a waypoint, press **F8**
   to capture the player's current location as a new waypoint (auto-named
   `wp<N>`, using the default mode and tolerance from your settings).
3. In the menu, tweak each waypoint as needed: name, X/Y/Z, mode
   (`teleport` / `walk`), tolerance, and reorder/delete.
4. Click **Save Route** to write the route file (pretty-printed JSON).

### 7. Run the route

| Action | Hotkey |
| --- | --- |
| Start running the route | **F5** |
| Stop | **F6** |
| Pause / resume | **F7** |
| Teleport to current waypoint | **F9** |
| Reload config | **F10** |
| Unload the DLL | **END** |

You can also use the menu buttons for the same actions.

---

## Hotkey reference

All hotkeys are configurable under `settings.hotkeys` in `config.json` using
`VK_*` names. Defaults:

| Hotkey | Default key | Setting |
| --- | --- | --- |
| Toggle menu | INSERT | `toggle_menu` |
| Start | F5 | `start` |
| Stop | F6 | `stop` |
| Pause | F7 | `pause` |
| Capture waypoint at current location | F8 | `capture_waypoint` |
| Teleport to current waypoint | F9 | `teleport_next` |
| Reload config | F10 | `reload_config` |
| Unload DLL | END | `unload` |

---

## Menu walkthrough

Press **INSERT** to show/hide the menu (window title "HelloNeighorBot"). While
the menu is visible it captures input. The menu shows:

- **SDK ready state** and the **live player location** (read each frame).
- **Route name**, current **state**, and **current index / waypoint count**.
- The **run timer** and **splits**.
- Buttons: **Start**, **Stop**, **Pause**, **Reset**, **Capture Waypoint**,
  **Teleport → Current**, **Save Route**, **Reload Config**.
- An **editable waypoint list**: each row has name, X/Y/Z, a mode combo
  (`teleport` / `walk`), tolerance, a **delete** button, and a **Go** button
  (teleport to that waypoint).
- A scrolling **log pane** mirroring the console output.

---

## Timer and splits

- The timer measures elapsed run time in milliseconds. **Elapsed = now − start
  − paused time.**
- **Pause** toggles between Running and Paused and accumulates the paused time
  so it does not count toward the run.
- **Reset** clears the timer and splits.
- A **split** is recorded as you complete each waypoint, so the splits track
  your progress through the route. Reaching the end either finishes the run or,
  if `loop_route` is enabled, loops back to the first waypoint.

---

## Route file format

Routes are JSON files. The auto-loaded route comes from `route_path`; see
[config/routes/act1.example.json](../config/routes/act1.example.json) for an
example.

```json
{
  "name": "Act 1",
  "loop": false,
  "waypoints": [
    {
      "name": "wp1",
      "x": 0.0, "y": 0.0, "z": 0.0,
      "mode": "teleport",
      "tolerance": 150.0,
      "wait_ms": 0,
      "action": "split"
    }
  ]
}
```

- `mode` is `"teleport"` (jump straight to the point) or `"walk"` (move toward
  the point at `move_speed`, arriving within `tolerance`).
- `wait_ms` holds at a waypoint after arriving before advancing.
- `action` is optional. `_comment` keys are ignored.

**Save Route** writes this same shape.

---

## Settings reference

Under `settings` in `config.json`:

| Key | Default | Meaning |
| --- | --- | --- |
| `enable_overlay` | `true` | Enable the in-game ImGui overlay/menu. |
| `enable_console` | `true` | Open a console window for logs. |
| `move_speed` | `1500.0` | Walk speed (units/sec) for `walk` waypoints. |
| `arrival_tolerance` | `150.0` | Default arrival tolerance for new waypoints. |
| `default_mode` | `"teleport"` | Default mode for captured/loaded waypoints. |
| `teleport_step` | `250.0` | Max distance moved per tick when walking. |
| `loop_route` | `false` | Loop back to the start at the end of the route. |
| `tick_interval_ms` | `8` | Bot main-loop tick interval. |
| `hotkeys` | see above | Configurable `VK_*` hotkey bindings. |

---

## Troubleshooting

- **No console appears.** Make sure `settings.enable_console` is `true`. The
  bot still runs without it — toggle the menu with INSERT and use hotkeys.
- **Coordinates show `n/a` / SDK not ready.** Your offsets are wrong or
  `GWorld` / the pointer chains did not resolve. Make sure you injected while
  in-game, then re-check `resolve.gworld` and `player.location_chain` against
  your build. See [FINDING_OFFSETS.md](FINDING_OFFSETS.md). The bot logs the
  resolve result rather than crashing — read the console/log pane. Press **F10**
  to reload after editing `config.json`.
- **The overlay does not show.** Some D3D11 setups will not display the ImGui
  overlay. The bot still works fully via the console plus the hotkeys — author
  and run routes using F8/F5/F6/F7/F9 and the log pane.
- **Wrong config loaded.** Confirm `config.json` is next to the DLL, in the
  game directory, or pointed to by `%HNBOT_CONFIG%` (searched in that order).
